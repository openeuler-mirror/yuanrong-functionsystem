/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "keycloak_verifier.h"

#include <openssl/bn.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <algorithm>
#include <ctime>
#include <memory>
#include <nlohmann/json.hpp>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "async/try.hpp"
#include "async/uuid_generator.hpp"
#include "common/hex/hex.h"
#include "common/logs/logging.h"
#include "common/utils/actor_worker.h"
#include "constants.h"
#include "httpd/http.hpp"
#include "httpd/http_connect.hpp"

namespace functionsystem::iamserver {

namespace {
const std::string JWT_SEPARATOR = ".";
constexpr size_t URL_ENCODE_EXPANSION_FACTOR = 3;
constexpr size_t BASE64_BLOCK_SIZE = 4;
constexpr uint64_t JWKS_HTTP_TIMEOUT_MS = 5000;
constexpr uint64_t TOKEN_HTTP_TIMEOUT_MS = 10000;
constexpr int SERVICE_TOKEN_DEFAULT_EXPIRES_SECONDS = 300;
constexpr int SERVICE_TOKEN_EXPIRY_SKEW_SECONDS = 30;
constexpr uint64_t JWT_CLOCK_SKEW_SECONDS = 60;
constexpr unsigned int HEX_NIBBLE_BITS = 4;
constexpr unsigned int HEX_NIBBLE_MASK = 0x0F;
const char HEX_DIGITS[] = "0123456789ABCDEF";

using BignumPtr = std::unique_ptr<BIGNUM, decltype(&BN_free)>;
using RsaPtr = std::unique_ptr<RSA, decltype(&RSA_free)>;
using EvpPkeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;
using BioChainPtr = std::unique_ptr<BIO, decltype(&BIO_free_all)>;
using EvpMdCtxPtr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

// Convert base64url string to bytes using direct OpenSSL implementation
std::vector<unsigned char> Base64UrlToBytes(const std::string &base64url)
{
    YRLOG_DEBUG("Base64UrlToBytes: entered, input length={}", static_cast<int>(base64url.length()));
    try {
        // Convert base64url to base64
        std::string base64 = base64url;
        std::replace(base64.begin(), base64.end(), '-', '+');
        std::replace(base64.begin(), base64.end(), '_', '/');

        // Add padding if necessary
        while (base64.size() % BASE64_BLOCK_SIZE != 0) {
            base64 += '=';
        }

        // Decode using OpenSSL BIO
        BioPtr b64(BIO_new(BIO_f_base64()), BIO_free);
        BioPtr mem(BIO_new_mem_buf(base64.c_str(), static_cast<int>(base64.length())), BIO_free);
        BioChainPtr bio(BIO_push(b64.release(), mem.release()), BIO_free_all);
        BIO_set_flags(bio.get(), BIO_FLAGS_BASE64_NO_NL);

        // Calculate maximum possible output size
        size_t maxLen = (base64.length() * URL_ENCODE_EXPANSION_FACTOR) / BASE64_BLOCK_SIZE;

        // Use vector instead of raw pointer for buffer
        std::vector<unsigned char> buffer(maxLen);

        int decodedLen = BIO_read(bio.get(), buffer.data(), static_cast<int>(maxLen));
        if (decodedLen <= 0) {
            return {};
        }

        // Resize vector to actual decoded length
        buffer.resize(decodedLen);
        return buffer;
    } catch (const std::exception &e) {
        YRLOG_ERROR("Base64UrlToBytes decode failed: {}", e.what());
        return {};
    } catch (...) {
        YRLOG_ERROR("Base64UrlToBytes decode failed with unknown exception");
        return {};
    }
}

EvpPkeyPtr CreateRsaPublicPkey(const std::vector<unsigned char> &nBytes, const std::vector<unsigned char> &eBytes)
{
    BignumPtr nBn(BN_bin2bn(nBytes.data(), static_cast<int>(nBytes.size()), nullptr), BN_free);
    BignumPtr eBn(BN_bin2bn(eBytes.data(), static_cast<int>(eBytes.size()), nullptr), BN_free);
    if (!nBn || !eBn) {
        YRLOG_ERROR("Failed to create BIGNUM from modulus or exponent");
        return EvpPkeyPtr(nullptr, EVP_PKEY_free);
    }

    RsaPtr rsa(RSA_new(), RSA_free);
    if (!rsa || RSA_set0_key(rsa.get(), nBn.get(), eBn.get(), nullptr) != 1) {
        YRLOG_ERROR("Failed to create RSA public key");
        return EvpPkeyPtr(nullptr, EVP_PKEY_free);
    }
    nBn.release();
    eBn.release();

    EvpPkeyPtr pkey(EVP_PKEY_new(), EVP_PKEY_free);
    if (!pkey || EVP_PKEY_assign_RSA(pkey.get(), rsa.get()) != 1) {
        YRLOG_ERROR("Failed to assign RSA to EVP_PKEY");
        return EvpPkeyPtr(nullptr, EVP_PKEY_free);
    }
    rsa.release();
    return pkey;
}

std::string ExportPublicKeyPem(EVP_PKEY *pkey)
{
    BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio || PEM_write_bio_PUBKEY(bio.get(), pkey) != 1) {
        YRLOG_ERROR("Failed to write public key to PEM");
        return "";
    }

    BUF_MEM *bioBuffer = nullptr;
    BIO_get_mem_ptr(bio.get(), &bioBuffer);
    if (bioBuffer == nullptr || bioBuffer->data == nullptr) {
        YRLOG_ERROR("Failed to read public key PEM");
        return "";
    }
    return std::string(bioBuffer->data, bioBuffer->length);
}

JwkKey ParseJwkKey(const nlohmann::json &keyJson)
{
    JwkKey key;
    if (keyJson.contains("kid") && keyJson["kid"].is_string()) {
        key.kid = keyJson["kid"].get<std::string>();
    }
    if (keyJson.contains("kty") && keyJson["kty"].is_string()) {
        key.kty = keyJson["kty"].get<std::string>();
    }
    if (keyJson.contains("n") && keyJson["n"].is_string()) {
        key.n = keyJson["n"].get<std::string>();
    }
    if (keyJson.contains("e") && keyJson["e"].is_string()) {
        key.e = keyJson["e"].get<std::string>();
    }
    if (keyJson.contains("alg") && keyJson["alg"].is_string()) {
        key.alg = keyJson["alg"].get<std::string>();
    }
    return key;
}

std::string GetJwkUse(const nlohmann::json &keyJson)
{
    if (keyJson.contains("use") && keyJson["use"].is_string()) {
        return keyJson["use"].get<std::string>();
    }
    return "";
}

bool IsValidJwkKey(const JwkKey &key, const std::string &keyUse)
{
    bool isValidKey = (key.kty == "RSA" && !key.kid.empty() && !key.n.empty() && !key.e.empty());
    if (isValidKey && !key.alg.empty()) {
        isValidKey = (key.alg == "RS256");
    }
    if (isValidKey && !keyUse.empty()) {
        isValidKey = (keyUse == "sig");
    }
    return isValidKey;
}

}  // namespace

KeycloakVerifier::KeycloakVerifier(const KeycloakConfig &config)
    : config_(config), jwksCache_(std::make_shared<JwksCache>())
{
    jwksCache_->ttlSeconds = config_.cacheTtlSeconds;
    jwksCache_->fetchedAt = std::chrono::steady_clock::time_point{};
}

bool KeycloakVerifier::NeedRefreshJwks() const
{
    std::lock_guard<std::mutex> lock(jwksMutex_);
    if (jwksCache_->keys.empty()) {
        return true;
    }
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - jwksCache_->fetchedAt);
    return elapsed.count() >= jwksCache_->ttlSeconds;
}

Status KeycloakVerifier::RefreshJwksOnce(bool forceRefresh)
{
    if (!forceRefresh && !NeedRefreshJwks()) {
        return Status::OK();
    }

    bool expected = false;
    if (jwksRefreshInProgress_.compare_exchange_strong(expected, true)) {
        Status refreshStatus = FetchJwksSync();
        {
            std::lock_guard<std::mutex> lock(jwksRefreshMutex_);
            lastJwksRefreshStatus_ = refreshStatus;
            jwksRefreshInProgress_.store(false);
        }
        jwksRefreshCv_.notify_all();
        return refreshStatus;
    }

    std::unique_lock<std::mutex> lock(jwksRefreshMutex_);
    jwksRefreshCv_.wait(lock, [this]() { return !jwksRefreshInProgress_.load(); });
    return lastJwksRefreshStatus_;
}

Status KeycloakVerifier::FetchJwksSync()
{
    std::string jwksUrl = config_.url + "/realms/" + config_.realm + "/protocol/openid-connect/certs";
    YRLOG_INFO("Fetching JWKS from: {}", jwksUrl);

    litebus::Try<litebus::http::URL> url = litebus::http::URL::Decode(jwksUrl);
    if (url.IsError()) {
        YRLOG_ERROR("Failed to execute curl command");
        return Status(StatusCode::FAILED, "Failed to execute curl command");
    }

    litebus::Future<litebus::http::Response> response =
        litebus::http::Get(url.Get(), litebus::None(), JWKS_HTTP_TIMEOUT_MS);
    response.Wait();

    if (response.IsError()) {
        YRLOG_ERROR("curl command failed with exit code: {}", response.GetErrorCode());
        return Status(StatusCode::FAILED, "curl command failed to fetch JWKS");
    }

    const auto &httpResponse = response.Get();
    if (httpResponse.retCode != litebus::http::OK) {
        YRLOG_ERROR("curl command failed with exit code: {}", static_cast<int>(httpResponse.retCode));
        return Status(StatusCode::FAILED, "curl command failed to fetch JWKS");
    }

    return ParseJwksResponse(httpResponse.body, jwksUrl);
}

Status KeycloakVerifier::ParseJwksResponse(const std::string &responseBody, const std::string &jwksUrl)
{
    YRLOG_DEBUG("JWKS response received, length: {}", responseBody.length());
    try {
        auto json = nlohmann::json::parse(responseBody);
        if (!json.contains("keys") || !json["keys"].is_array()) {
            YRLOG_ERROR("Invalid JWKS response: missing 'keys' array");
            return Status(StatusCode::FAILED, "Invalid JWKS format");
        }

        auto newCache = std::make_shared<JwksCache>();
        newCache->ttlSeconds = config_.cacheTtlSeconds;
        newCache->fetchedAt = std::chrono::steady_clock::now();

        for (const auto &keyJson : json["keys"]) {
            JwkKey key = ParseJwkKey(keyJson);
            std::string keyUse = GetJwkUse(keyJson);
            if (IsValidJwkKey(key, keyUse)) {
                newCache->keys[key.kid] = key;
                YRLOG_DEBUG("Added JWKS key: kid={}, kty={}, use={}, alg={}", key.kid, key.kty, keyUse, key.alg);
            } else {
                YRLOG_WARN("Skipped JWKS key: kid={}, kty={}, use={}, alg={} - does not meet security requirements",
                           key.kid, key.kty, keyUse, key.alg);
            }
        }

        {
            std::lock_guard<std::mutex> lock(jwksMutex_);
            jwksCache_ = newCache;
        }

        YRLOG_INFO("Successfully fetched {} JWKS keys from {}", newCache->keys.size(), jwksUrl);
        return Status::OK();
    } catch (const nlohmann::json::exception &e) {
        YRLOG_ERROR("Failed to parse JWKS JSON: {}", e.what());
        return Status(StatusCode::FAILED, std::string("JWKS JSON parse error: ") + e.what());
    }
}

Status KeycloakVerifier::ParseJwt(const std::string &token, std::string &header, std::string &payload,
                                  std::string &signature)
{
    size_t firstDot = token.find(JWT_SEPARATOR);
    if (firstDot == std::string::npos) {
        return Status(StatusCode::FAILED, "JWT format error: first separator not found");
    }
    size_t secondDot = token.rfind(JWT_SEPARATOR);
    if (secondDot == std::string::npos || secondDot == firstDot) {
        return Status(StatusCode::FAILED, "JWT format error: second separator not found");
    }
    if (token.find(JWT_SEPARATOR, firstDot + 1) != secondDot) {
        return Status(StatusCode::FAILED, "JWT format error: too many separators");
    }

    std::string headerB64 = token.substr(0, firstDot);
    std::string payloadB64 = token.substr(firstDot + 1, secondDot - firstDot - 1);
    std::string signatureB64 = token.substr(secondDot + 1);

    header = functionsystem::Base64UrlDecode(headerB64);
    payload = functionsystem::Base64UrlDecode(payloadB64);
    signature = functionsystem::Base64UrlDecode(signatureB64);

    if (header.empty() || payload.empty()) {
        return Status(StatusCode::FAILED, "JWT header or payload is empty after decoding");
    }

    return Status::OK();
}

std::string KeycloakVerifier::BuildRsaPublicKey(const std::string &n, const std::string &e)
{
    YRLOG_DEBUG("BuildRsaPublicKey: entered, n length={}, e length={}", static_cast<int>(n.length()),
                static_cast<int>(e.length()));

    // Decode base64url encoded modulus and exponent
    std::vector<unsigned char> nBytes = Base64UrlToBytes(n);
    std::vector<unsigned char> eBytes = Base64UrlToBytes(e);
    if (nBytes.empty() || eBytes.empty()) {
        YRLOG_ERROR("Failed to decode modulus or exponent");
        return "";
    }

    EvpPkeyPtr pkey = CreateRsaPublicPkey(nBytes, eBytes);
    if (!pkey) {
        return "";
    }
    return ExportPublicKeyPem(pkey.get());
}

bool KeycloakVerifier::VerifyRs256Signature(const std::string &token, const JwkKey &jwkKey)
{
    // Find the signature position
    size_t secondDot = token.rfind(JWT_SEPARATOR);
    if (secondDot == std::string::npos) {
        YRLOG_WARN("No signature separator found in token");
        return false;
    }

    std::string signingInput = token.substr(0, secondDot);
    std::string signatureB64 = token.substr(secondDot + 1);

    std::string signature = functionsystem::Base64UrlDecode(signatureB64);

    // Build RSA public key
    std::string pem = BuildRsaPublicKey(jwkKey.n, jwkKey.e);
    if (pem.empty()) {
        YRLOG_ERROR("Failed to build RSA public key");
        return false;
    }

    // Create BIO from PEM
    BioPtr bio(BIO_new_mem_buf(pem.c_str(), static_cast<int>(pem.length())), BIO_free);
    if (!bio) {
        YRLOG_ERROR("Failed to create BIO from PEM");
        return false;
    }

    // Read public key
    EvpPkeyPtr pkey(PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    if (!pkey) {
        YRLOG_ERROR("Failed to read public key from PEM");
        return false;
    }

    // Create verification context
    EvpMdCtxPtr mdCtx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!mdCtx) {
        YRLOG_ERROR("Failed to create EVP_MD_CTX");
        return false;
    }

    // Initialize verification with SHA256
    int ret = EVP_DigestVerifyInit(mdCtx.get(), nullptr, EVP_sha256(), nullptr, pkey.get());
    if (ret != 1) {
        YRLOG_ERROR("EVP_DigestVerifyInit failed");
        return false;
    }

    // Verify signature
    auto signatureBytes = static_cast<const unsigned char *>(static_cast<const void *>(signature.data()));
    auto signingInputBytes = static_cast<const unsigned char *>(static_cast<const void *>(signingInput.data()));
    ret = EVP_DigestVerify(
        mdCtx.get(), signatureBytes, static_cast<int>(signature.length()), signingInputBytes,
        static_cast<int>(signingInput.length()));
    YRLOG_DEBUG("Signature verification result: {}", ret);

    return ret == 1;
}

std::string KeycloakVerifier::GetHighestRole(const std::vector<std::string> &roles)
{
    std::string highestRole;
    int highestPriority = 0;

    for (const auto &role : roles) {
        int priority = GetRolePriority(role);
        if (priority > highestPriority) {
            highestPriority = priority;
            highestRole = role;
        }
    }

    return highestRole;
}

Status KeycloakVerifier::EnsureEnabledAndJwks()
{
    if (!config_.enabled) {
        YRLOG_ERROR("Keycloak integration is not enabled");
        return Status(StatusCode::FAILED, "Keycloak integration is not enabled");
    }

    if (NeedRefreshJwks()) {
        Status fetchStatus = RefreshJwksOnce();
        if (fetchStatus.IsError()) {
            return Status(StatusCode::FAILED, "Failed to fetch JWKS: " + fetchStatus.ToString());
        }
    }
    return Status::OK();
}

Status KeycloakVerifier::ExtractJwtHeader(const std::string &headerJson, std::string *kid) const
{
    try {
        YRLOG_DEBUG("JWT header: {}", headerJson);
        auto header = nlohmann::json::parse(headerJson);
        if (header.contains("kid") && header["kid"].is_string()) {
            *kid = header["kid"].get<std::string>();
        }
        if (!header.contains("alg") || header["alg"] != "RS256") {
            return Status(StatusCode::FAILED, "Unsupported JWT algorithm, expected RS256");
        }
    } catch (const nlohmann::json::exception &e) {
        return Status(StatusCode::FAILED, std::string("Failed to parse JWT header: ") + e.what());
    }
    return Status::OK();
}

Status KeycloakVerifier::ResolveJwkKey(const std::string &kid, JwkKey *jwkKey)
{
    bool keyNotFound = false;
    {
        std::lock_guard<std::mutex> lock(jwksMutex_);
        auto it = jwksCache_->keys.find(kid);
        if (it == jwksCache_->keys.end()) {
            keyNotFound = true;
        } else {
            *jwkKey = it->second;
        }
    }

    if (!keyNotFound) {
        return Status::OK();
    }

    YRLOG_WARN("JWT key ID not found in JWKS cache: {}, attempting refresh", kid);
    Status fetchStatus = RefreshJwksOnce(true);
    if (fetchStatus.IsError()) {
        return Status(StatusCode::FAILED, "Failed to fetch JWKS after key miss: " + fetchStatus.ToString());
    }

    std::lock_guard<std::mutex> lock(jwksMutex_);
    auto it = jwksCache_->keys.find(kid);
    if (it == jwksCache_->keys.end()) {
        return Status(StatusCode::FAILED, "JWT key ID not found in JWKS after refresh: " + kid);
    }
    *jwkKey = it->second;
    return Status::OK();
}

Status KeycloakVerifier::ExtractSubjectAndTenant(const nlohmann::json &payload, ExternalUserInfo& info) const
{
    if (!payload.contains("sub") || !payload["sub"].is_string()) {
        return Status(StatusCode::FAILED, "JWT payload missing 'sub' claim");
    }
    info.userId = payload["sub"].get<std::string>();
    if (info.userId.empty()) {
        return Status(StatusCode::FAILED, "JWT sub claim is empty");
    }

    if (payload.contains("tenant_id") && payload["tenant_id"].is_string()) {
        info.tenantId = payload["tenant_id"].get<std::string>();
    }
    if (info.tenantId.empty() && payload.contains("preferred_username") &&
        payload["preferred_username"].is_string()) {
        info.tenantId = payload["preferred_username"].get<std::string>();
    }
    if (info.tenantId.empty() && payload.contains("email") && payload["email"].is_string()) {
        const std::string email = payload["email"].get<std::string>();
        const size_t atPos = email.find('@');
        if (atPos != std::string::npos && atPos + 1 < email.size()) {
            info.tenantId = email.substr(atPos + 1);
        }
    }
    if (info.tenantId.empty()) {
        return Status(StatusCode::FAILED,
                      "JWT payload missing usable tenant identifier: no tenant_id, preferred_username or email");
    }
    return Status::OK();
}

Status KeycloakVerifier::ValidateTokenClaims(const nlohmann::json &payload, ExternalUserInfo& info) const
{
    if (!payload.contains("iss") || !payload["iss"].is_string()) {
        return Status(StatusCode::FAILED, "JWT missing iss claim");
    }
    if (payload["iss"].get<std::string>() != config_.issuer) {
        return Status(StatusCode::FAILED, "JWT issuer validation failed");
    }

    if (!payload.contains("aud")) {
        return Status(StatusCode::FAILED, "JWT missing aud claim");
    }
    bool audValid = false;
    if (payload["aud"].is_string()) {
        audValid = (payload["aud"].get<std::string>() == config_.audience);
    } else if (payload["aud"].is_array()) {
        for (const auto &aud : payload["aud"]) {
            audValid = aud.is_string() && aud.get<std::string>() == config_.audience;
            if (audValid) {
                break;
            }
        }
    }
    if (!audValid) {
        return Status(StatusCode::FAILED, "JWT audience validation failed");
    }

    if (payload.contains("azp") && payload["azp"].is_string()) {
        std::string azp = payload["azp"].get<std::string>();
        if (!azp.empty() && azp != config_.clientId) {
            return Status(StatusCode::FAILED, "JWT authorized party validation failed");
        }
    }

    auto now = static_cast<uint64_t>(std::time(nullptr));
    if (payload.contains("iat") && payload["iat"].is_number() &&
        payload["iat"].get<uint64_t>() > now + JWT_CLOCK_SKEW_SECONDS) {
        return Status(StatusCode::FAILED, "JWT iat claim indicates future time");
    }
    if (payload.contains("nbf") && payload["nbf"].is_number() && payload["nbf"].get<uint64_t>() > now) {
        return Status(StatusCode::FAILED, "JWT not yet valid (nbf claim)");
    }
    if (!payload.contains("exp") || !payload["exp"].is_number()) {
        return Status(StatusCode::FAILED, "JWT missing or invalid exp claim");
    }
    info.exp = payload["exp"].get<int64_t>();
    if (info.exp > 0 && info.exp < static_cast<int64_t>(now)) {
        return Status(StatusCode::FAILED, "JWT token has expired");
    }
    return Status::OK();
}

namespace {
void AppendRoles(const nlohmann::json &roles, std::vector<std::string> *allRoles)
{
    if (!roles.is_array()) {
        return;
    }
    for (const auto &role : roles) {
        if (role.is_string()) {
            allRoles->push_back(role.get<std::string>());
        }
    }
}

void ExtractQuotaClaim(const nlohmann::json &payload, const std::string &claim, int64_t *quota)
{
    if (!payload.contains(claim)) {
        return;
    }
    if (payload[claim].is_number()) {
        *quota = payload[claim].get<int64_t>();
        return;
    }
    if (!payload[claim].is_string()) {
        return;
    }
    try {
        *quota = std::stoll(payload[claim].get<std::string>());
    } catch (const std::exception &e) {
        YRLOG_DEBUG("Failed to parse {} claim: {}", claim, e.what());
    }
}
}  // namespace

void KeycloakVerifier::ExtractRolesAndQuotas(const nlohmann::json &payload, ExternalUserInfo& info)
{
    std::vector<std::string> allRoles;
    if (payload.contains("realm_access") && payload["realm_access"].is_object()) {
        auto realmAccess = payload["realm_access"];
        if (realmAccess.contains("roles")) {
            AppendRoles(realmAccess["roles"], &allRoles);
        }
    }
    if (payload.contains("resource_access") && payload["resource_access"].is_object()) {
        for (const auto &clientAccess : payload["resource_access"].items()) {
            const auto &access = clientAccess.value();
            if (access.is_object() && access.contains("roles")) {
                AppendRoles(access["roles"], &allRoles);
            }
        }
    }
    info.role = GetHighestRole(allRoles);
    if (info.role.empty()) {
        info.role = "user";
    }

    ExtractQuotaClaim(payload, "cpu_quota", &info.cpuQuota);
    ExtractQuotaClaim(payload, "mem_quota", &info.memQuota);
}

Status KeycloakVerifier::PopulateUserInfoFromPayload(const std::string &payloadJson, ExternalUserInfo& info)
{
    try {
        auto payload = nlohmann::json::parse(payloadJson);
        Status status = ExtractSubjectAndTenant(payload, info);
        if (status.IsError()) {
            return status;
        }
        status = ValidateTokenClaims(payload, info);
        if (status.IsError()) {
            return status;
        }
        ExtractRolesAndQuotas(payload, info);
    } catch (const nlohmann::json::exception &e) {
        return Status(StatusCode::FAILED, std::string("Failed to parse JWT payload: ") + e.what());
    }
    return Status::OK();
}

ExternalUserInfo KeycloakVerifier::VerifySync(const std::string &idToken)
{
    ExternalUserInfo info;
    Status status = EnsureEnabledAndJwks();
    if (status.IsError()) {
        info.status = status;
        return info;
    }

    std::string headerJson;
    std::string payloadJson;
    std::string signature;
    status = ParseJwt(idToken, headerJson, payloadJson, signature);
    if (status.IsError()) {
        YRLOG_ERROR("Failed to parse JWT: {}", status.ToString());
        info.status = status;
        return info;
    }
    YRLOG_DEBUG("JWT parsed successfully, headerJson size: {}", headerJson.size());

    std::string kid;
    JwkKey jwkKey;
    status = ExtractJwtHeader(headerJson, &kid);
    if (status.IsOk()) {
        status = ResolveJwkKey(kid, &jwkKey);
    }
    if (status.IsOk() && !VerifyRs256Signature(idToken, jwkKey)) {
        status = Status(StatusCode::FAILED, "JWT signature verification failed");
    }
    if (status.IsOk()) {
        status = PopulateUserInfoFromPayload(payloadJson, info);
    }
    info.status = status;
    if (status.IsOk()) {
        YRLOG_DEBUG("Keycloak token verified successfully: userId={}, role={}, cpuQuota={}, memQuota={}", info.userId,
                    info.role, info.cpuQuota, info.memQuota);
    }
    return info;
}

litebus::Future<ExternalUserInfo> KeycloakVerifier::Verify(const std::string &idToken)
{
    auto promise = std::make_shared<litebus::Promise<ExternalUserInfo>>();

    ExternalUserInfo info = VerifySync(idToken);
    promise->SetValue(info);

    return promise->GetFuture();
}

namespace {
std::string UrlEncode(const std::string &value)
{
    std::string encoded;
    encoded.reserve(value.size() * URL_ENCODE_EXPANSION_FACTOR);
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += static_cast<char>(c);
        } else {
            encoded += '%';
            encoded += HEX_DIGITS[c >> HEX_NIBBLE_BITS];
            encoded += HEX_DIGITS[c & HEX_NIBBLE_MASK];
        }
    }
    return encoded;
}
}  // namespace

bool KeycloakVerifier::IsConfigured() const
{
    return config_.enabled && !config_.url.empty() && !config_.clientId.empty() && !config_.clientSecret.empty();
}

std::string KeycloakVerifier::BuildTokenUrl() const
{
    return config_.url + "/realms/" + config_.realm + "/protocol/openid-connect/token";
}

ExternalUserInfo KeycloakVerifier::MakeFailureInfo(const std::string &message) const
{
    ExternalUserInfo info;
    info.userId = "";
    info.tenantId = "";
    info.status = Status(StatusCode::FAILED, message);
    return info;
}

Status KeycloakVerifier::PostTokenRequest(const std::string &body, const std::string &failureMessage,
                                          std::string *responseBody) const
{
    litebus::Try<litebus::http::URL> url = litebus::http::URL::Decode(BuildTokenUrl());
    if (url.IsError()) {
        return Status(StatusCode::FAILED, "Failed to parse token URL");
    }

    litebus::Future<litebus::http::Response> response =
        litebus::http::Post(url.Get(), litebus::None(), body, std::string("application/x-www-form-urlencoded"),
                            TOKEN_HTTP_TIMEOUT_MS);
    if (response.IsError()) {
        YRLOG_ERROR("{}: {}", failureMessage, response.GetErrorCode());
        return Status(StatusCode::FAILED, failureMessage);
    }

    const auto &httpResponse = response.Get();
    if (httpResponse.retCode != litebus::http::OK) {
        YRLOG_ERROR("{} HTTP error: {} - {}", failureMessage, httpResponse.retCode, httpResponse.body);
        return Status(StatusCode::FAILED, failureMessage + " HTTP error");
    }
    *responseBody = httpResponse.body;
    return Status::OK();
}

ExternalUserInfo KeycloakVerifier::VerifyTokenResponse(const std::string &responseBody,
                                                       const std::string &missingTokenMessage)
{
    try {
        auto json = nlohmann::json::parse(responseBody);
        if (!json.contains("id_token")) {
            YRLOG_ERROR("{}", missingTokenMessage);
            return MakeFailureInfo(missingTokenMessage);
        }

        ExternalUserInfo info = VerifySync(json["id_token"].get<std::string>());
        if (info.status.IsError()) {
            YRLOG_ERROR("Keycloak token verification failed: {}", info.status.ToString());
        }
        return info;
    } catch (const nlohmann::json::exception &e) {
        YRLOG_ERROR("Failed to parse Keycloak token response: {}", e.what());
        return MakeFailureInfo(std::string("Failed to parse response: ") + e.what());
    }
}

litebus::Future<ExternalUserInfo> KeycloakVerifier::LoginWithPassword(const std::string &username,
                                                                      const std::string &password)
{
    auto promise = std::make_shared<litebus::Promise<ExternalUserInfo>>();

    if (!IsConfigured()) {
        promise->SetValue(MakeFailureInfo("Keycloak not configured"));
        return promise->GetFuture();
    }

    std::string body =
        "grant_type=password" +
        std::string("&client_id=") +
        UrlEncode(config_.clientId) +
        "&client_secret=" + UrlEncode(config_.clientSecret) +
        "&username=" + UrlEncode(username) +
        "&password=" + UrlEncode(password) +
        "&scope=openid+profile+email";

    YRLOG_INFO("Attempting password grant for user: {}", username);

    std::string responseBody;
    Status status = PostTokenRequest(body, "Password grant failed", &responseBody);
    if (status.IsError()) {
        promise->SetValue(MakeFailureInfo(status.GetMessage()));
        return promise->GetFuture();
    }

    ExternalUserInfo info = VerifyTokenResponse(responseBody, "Password grant response missing id_token");
    if (info.status.IsOk()) {
        YRLOG_INFO("Password grant successful for user: {}", username);
    }
    promise->SetValue(info);
    return promise->GetFuture();
}

litebus::Future<ExternalUserInfo> KeycloakVerifier::ExchangeCode(const std::string &code,
                                                                 const std::string &redirectUri)
{
    auto promise = std::make_shared<litebus::Promise<ExternalUserInfo>>();

    if (!IsConfigured()) {
        promise->SetValue(MakeFailureInfo("Keycloak not configured"));
        return promise->GetFuture();
    }

    std::string body =
        "grant_type=authorization_code" +
        std::string("&client_id=") +
        UrlEncode(config_.clientId) +
        "&client_secret=" + UrlEncode(config_.clientSecret) +
        "&code=" + UrlEncode(code) +
        "&redirect_uri=" + UrlEncode(redirectUri);

    YRLOG_INFO("Exchanging authorization code for tokens");

    std::string responseBody;
    Status status = PostTokenRequest(body, "Code exchange failed", &responseBody);
    if (status.IsError()) {
        promise->SetValue(MakeFailureInfo(status.GetMessage()));
        return promise->GetFuture();
    }

    ExternalUserInfo info = VerifyTokenResponse(responseBody, "Code exchange response missing id_token");
    if (info.status.IsOk()) {
        YRLOG_INFO("Code exchange successful for user: {}", info.userId);
    }
    promise->SetValue(info);
    return promise->GetFuture();
}

std::string KeycloakVerifier::GetAuthUrl(const std::string &type, const std::string &redirectUri,
                                         const std::string &state)
{
    std::string baseUrl = config_.publicUrl.empty() ? config_.url : config_.publicUrl;
    std::string endpoint;

    if (type == "register") {
        endpoint = "/protocol/openid-connect/registrations";
    } else if (type == "logout") {
        endpoint = "/protocol/openid-connect/logout";
    } else {
        endpoint = "/protocol/openid-connect/auth";
    }

    std::string url = baseUrl + "/realms/" + config_.realm + endpoint;
    url += "?client_id=" + UrlEncode(config_.clientId);
    url += "&redirect_uri=" + UrlEncode(redirectUri);
    url += "&response_type=code";
    url += "&scope=openid+profile+email";

    std::string authState = state;
    if (type == "auth" && authState.empty()) {
        authState = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
    }
    if (!authState.empty()) {
        url += "&state=" + UrlEncode(authState);
    }

    if (type == "register") {
        url += "&action=register";
    }

    return url;
}

bool KeycloakVerifier::GetCachedServiceAccountToken(std::string& accessToken) const
{
    std::lock_guard<std::mutex> lock(serviceAccountMutex_);
    auto now = std::chrono::steady_clock::now();
    if (cachedServiceAccountToken_.empty() || now >= serviceAccountTokenExpiry_) {
        return false;
    }
    accessToken = cachedServiceAccountToken_;
    return true;
}

Status KeycloakVerifier::FetchServiceAccountToken(std::string& accessToken)
{
    std::string body = "grant_type=client_credentials&client_id=" + UrlEncode(config_.clientId) +
                       "&client_secret=" + UrlEncode(config_.clientSecret);
    std::string responseBody;
    Status status = PostTokenRequest(body, "Failed to get service account token", &responseBody);
    if (status.IsError()) {
        return status;
    }

    try {
        auto json = nlohmann::json::parse(responseBody);
        accessToken = json["access_token"].get<std::string>();
        int expiresIn = json.value("expires_in", SERVICE_TOKEN_DEFAULT_EXPIRES_SECONDS);

        std::lock_guard<std::mutex> lock(serviceAccountMutex_);
        cachedServiceAccountToken_ = accessToken;
        serviceAccountTokenExpiry_ = std::chrono::steady_clock::now() +
            std::chrono::seconds(expiresIn - SERVICE_TOKEN_EXPIRY_SKEW_SECONDS);
        return Status::OK();
    } catch (const nlohmann::json::exception &e) {
        YRLOG_ERROR("Failed to parse service account token response: {}", e.what());
        return Status(StatusCode::FAILED, std::string("Failed to parse service account token response: ") + e.what());
    }
}

std::pair<int64_t, int64_t> KeycloakVerifier::ParseQuotaResponse(const std::string &responseBody)
{
    try {
        auto userJson = nlohmann::json::parse(responseBody);
        if (!userJson.is_array() || userJson.empty() || !userJson[0].contains("attributes")) {
            return {-1, -1};
        }

        auto attrs = userJson[0]["attributes"];
        int64_t cpuQuota = -1;
        int64_t memQuota = -1;
        if (attrs.contains("cpu_quota") && attrs["cpu_quota"].is_array() && !attrs["cpu_quota"].empty()) {
            cpuQuota = std::stoll(attrs["cpu_quota"][0].get<std::string>());
        }
        if (attrs.contains("mem_quota") && attrs["mem_quota"].is_array() && !attrs["mem_quota"].empty()) {
            memQuota = std::stoll(attrs["mem_quota"][0].get<std::string>());
        }
        return {cpuQuota, memQuota};
    } catch (const nlohmann::json::exception &e) {
        YRLOG_ERROR("Failed to parse user response: {}", e.what());
        return {-1, -1};
    }
}

std::pair<int64_t, int64_t> KeycloakVerifier::QueryTenantQuotaWithToken(const std::string &tenantId,
                                                                        const std::string &accessToken) const
{
    std::string adminUrl = config_.url + "/admin/realms/" + config_.realm + "/users?username=" + UrlEncode(tenantId);
    litebus::Try<litebus::http::URL> adminUrlParsed = litebus::http::URL::Decode(adminUrl);
    if (adminUrlParsed.IsError()) {
        return {-1, -1};
    }

    std::unordered_map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + accessToken;
    litebus::Future<litebus::http::Response> userResponse =
        litebus::http::Get(adminUrlParsed.Get(), litebus::Some(headers), JWKS_HTTP_TIMEOUT_MS);

    if (userResponse.IsError() || userResponse.Get().retCode != litebus::http::OK) {
        YRLOG_ERROR("Failed to query user: {}", userResponse.GetErrorCode());
        return {-1, -1};
    }
    return ParseQuotaResponse(userResponse.Get().body);
}

litebus::Future<std::pair<int64_t, int64_t>> KeycloakVerifier::QueryTenantQuota(const std::string &tenantId)
{
    auto promise = std::make_shared<litebus::Promise<std::pair<int64_t, int64_t>>>();
    if (!IsConfigured()) {
        promise->SetValue(std::make_pair<int64_t, int64_t>(-1, -1));
        return promise->GetFuture();
    }

    std::string accessToken;
    if (!GetCachedServiceAccountToken(accessToken)) {
        Status status = FetchServiceAccountToken(accessToken);
        if (status.IsError()) {
            promise->SetValue(std::make_pair<int64_t, int64_t>(-1, -1));
            return promise->GetFuture();
        }
    }

    promise->SetValue(QueryTenantQuotaWithToken(tenantId, accessToken));
    return promise->GetFuture();
}

}  // namespace functionsystem::iamserver
