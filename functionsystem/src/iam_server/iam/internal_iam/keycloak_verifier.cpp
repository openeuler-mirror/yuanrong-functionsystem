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
#include "common/hex/hex.h"
#include "common/logs/logging.h"
#include "common/utils/actor_worker.h"
#include "constants.h"
#include "httpd/http.hpp"
#include "httpd/http_connect.hpp"

namespace functionsystem::iamserver {

namespace {
const std::string JWT_SEPARATOR = ".";

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
        while (base64.size() % 4 != 0) {
            base64 += '=';
        }

        // Decode using OpenSSL BIO
        BioPtr b64(BIO_new(BIO_f_base64()), BIO_free);
        BioPtr mem(BIO_new_mem_buf(base64.c_str(), static_cast<int>(base64.length())), BIO_free);
        BioChainPtr bio(BIO_push(b64.release(), mem.release()), BIO_free_all);
        BIO_set_flags(bio.get(), BIO_FLAGS_BASE64_NO_NL);

        // Calculate maximum possible output size
        size_t maxLen = (base64.length() * 3) / 4;

        // Use vector instead of raw pointer for buffer
        std::vector<unsigned char> buffer(maxLen);

        int decodedLen = BIO_read(bio.get(), buffer.data(), static_cast<int>(maxLen));

        if (decodedLen <= 0) {
            return {};
        }

        // Resize vector to actual decoded length
        buffer.resize(decodedLen);
        return buffer;
    } catch (...) {
        return {};
    }
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
    while (true) {
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
        if (forceRefresh || !NeedRefreshJwks()) {
            return lastJwksRefreshStatus_;
        }
    }
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

    litebus::Future<litebus::http::Response> response = litebus::http::Get(url.Get(), litebus::None(), 5000);
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

    const std::string &responseBody = httpResponse.body;
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

            std::string keyUse;
            if (keyJson.contains("use") && keyJson["use"].is_string()) {
                keyUse = keyJson["use"].get<std::string>();
            }

            // Only add RSA keys with required fields and proper algorithm
            // Validate: kty=RSA, use=sig (optional but preferred), alg=RS256
            bool isValidKey = (key.kty == "RSA" && !key.kid.empty() && !key.n.empty() && !key.e.empty());
            if (isValidKey && !key.alg.empty()) {
                isValidKey = (key.alg == "RS256");
            }
            if (isValidKey && !keyUse.empty()) {
                isValidKey = (keyUse == "sig");
            }
            if (isValidKey) {
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
    size_t secondDot = token.find(JWT_SEPARATOR, firstDot + 1);
    if (secondDot == std::string::npos) {
        return Status(StatusCode::FAILED, "JWT format error: second separator not found");
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

    // Convert bytes to BIGNUM
    BignumPtr nBn(BN_bin2bn(nBytes.data(), static_cast<int>(nBytes.size()), nullptr), BN_free);
    BignumPtr eBn(BN_bin2bn(eBytes.data(), static_cast<int>(eBytes.size()), nullptr), BN_free);

    if (!nBn || !eBn) {
        YRLOG_ERROR("Failed to create BIGNUM from modulus or exponent");
        return "";
    }

    // Create RSA key
    RsaPtr rsa(RSA_new(), RSA_free);
    if (!rsa) {
        YRLOG_ERROR("Failed to create RSA structure");
        return "";
    }

    int setResult = RSA_set0_key(rsa.get(), nBn.get(), eBn.get(), nullptr);
    if (setResult != 1) {
        YRLOG_ERROR("Failed to set RSA key components");
        return "";
    }

    // nBn and eBn are now owned by rsa, transfer ownership to avoid double free.
    nBn.release();
    eBn.release();

    // Convert to EVP_PKEY
    EvpPkeyPtr pkey(EVP_PKEY_new(), EVP_PKEY_free);
    if (!pkey) {
        YRLOG_ERROR("Failed to create EVP_PKEY");
        return "";
    }

    if (EVP_PKEY_assign_RSA(pkey.get(), rsa.get()) != 1) {
        YRLOG_ERROR("Failed to assign RSA to EVP_PKEY");
        return "";
    }

    // rsa is now owned by pkey, transfer ownership to avoid double free.
    rsa.release();

    // Write to PEM format
    BioPtr bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio) {
        YRLOG_ERROR("Failed to create BIO");
        return "";
    }

    if (PEM_write_bio_PUBKEY(bio.get(), pkey.get()) != 1) {
        YRLOG_ERROR("Failed to write public key to PEM");
        return "";
    }

    // Read PEM from BIO
    char *data = nullptr;
    long len = BIO_get_mem_data(bio.get(), &data);
    std::string pem(data, len);

    return pem;
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
    ret = EVP_DigestVerify(
        mdCtx.get(), reinterpret_cast<const unsigned char *>(signature.data()), static_cast<int>(signature.length()),
        reinterpret_cast<const unsigned char *>(signingInput.data()), static_cast<int>(signingInput.length()));
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

ExternalUserInfo KeycloakVerifier::VerifySync(const std::string &idToken)
{
    ExternalUserInfo info;

    if (!config_.enabled) {
        YRLOG_ERROR("Keycloak integration is not enabled");
        info.status = Status(StatusCode::FAILED, "Keycloak integration is not enabled");
        return info;
    }

    // Refresh JWKS if needed
    if (NeedRefreshJwks()) {
        Status fetchStatus = RefreshJwksOnce();
        if (fetchStatus.IsError()) {
            info.status = Status(StatusCode::FAILED, "Failed to fetch JWKS: " + fetchStatus.ToString());
            return info;
        }
    }

    // Parse JWT
    std::string headerJson, payloadJson, signature;
    Status parseStatus = ParseJwt(idToken, headerJson, payloadJson, signature);
    if (parseStatus.IsError()) {
        YRLOG_ERROR("Failed to parse JWT: {}", parseStatus.ToString());
        info.status = parseStatus;
        return info;
    }
    YRLOG_DEBUG("JWT parsed successfully, headerJson size: {}", headerJson.size());

    // Parse header to get kid
    std::string kid;
    try {
        YRLOG_DEBUG("JWT header: {}", headerJson);
        auto header = nlohmann::json::parse(headerJson);
        if (header.contains("kid") && header["kid"].is_string()) {
            kid = header["kid"].get<std::string>();
        }
        // Verify algorithm is RS256
        if (!header.contains("alg") || header["alg"] != "RS256") {
            info.status = Status(StatusCode::FAILED, "Unsupported JWT algorithm, expected RS256");
            return info;
        }
    } catch (const nlohmann::json::exception &e) {
        info.status = Status(StatusCode::FAILED, std::string("Failed to parse JWT header: ") + e.what());
        return info;
    }

    // Find the key in JWKS cache
    JwkKey jwkKey;
    bool keyNotFound = false;
    {
        std::lock_guard<std::mutex> lock(jwksMutex_);
        auto it = jwksCache_->keys.find(kid);
        if (it == jwksCache_->keys.end()) {
            keyNotFound = true;
        } else {
            jwkKey = it->second;
        }
    }

    // If kid not found, try to refresh JWKS once
    if (keyNotFound) {
        YRLOG_WARN("JWT key ID not found in JWKS cache: {}, attempting refresh", kid);
        Status fetchStatus = RefreshJwksOnce(true);
        if (fetchStatus.IsError()) {
            info.status = Status(StatusCode::FAILED, "Failed to fetch JWKS after key miss: " + fetchStatus.ToString());
            return info;
        }
        // Retry finding the key
        std::lock_guard<std::mutex> lock(jwksMutex_);
        auto it = jwksCache_->keys.find(kid);
        if (it == jwksCache_->keys.end()) {
            info.status = Status(StatusCode::FAILED, "JWT key ID not found in JWKS after refresh: " + kid);
            return info;
        }
        jwkKey = it->second;
    }

    // Verify signature
    if (!VerifyRs256Signature(idToken, jwkKey)) {
        info.status = Status(StatusCode::FAILED, "JWT signature verification failed");
        return info;
    }

    // Parse payload
    try {
        auto payload = nlohmann::json::parse(payloadJson);

        // Extract sub (user ID)
        if (!payload.contains("sub") || !payload["sub"].is_string()) {
            info.status = Status(StatusCode::FAILED, "JWT payload missing 'sub' claim");
            return info;
        }
        info.userId = payload["sub"].get<std::string>();

        // Extract tenant ID: explicit claim > preferred_username > email domain > fail
        if (payload.contains("tenant_id") && payload["tenant_id"].is_string()) {
            info.tenantId = payload["tenant_id"].get<std::string>();
        }

        if (info.tenantId.empty() && payload.contains("preferred_username")
            && payload["preferred_username"].is_string()) {
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
            info.status =
                Status(StatusCode::FAILED,
                       "JWT payload missing usable tenant identifier: no tenant_id, preferred_username or email");
            return info;
        }

        // Validate iss (issuer)
        if (!payload.contains("iss") || !payload["iss"].is_string()) {
            info.status = Status(StatusCode::FAILED, "JWT missing iss claim");
            return info;
        }
        std::string issuer = payload["iss"].get<std::string>();
        if (issuer != config_.issuer) {
            info.status = Status(StatusCode::FAILED, "JWT issuer validation failed");
            return info;
        }

        // Validate aud (audience)
        if (!payload.contains("aud")) {
            info.status = Status(StatusCode::FAILED, "JWT missing aud claim");
            return info;
        }
        bool audValid = false;
        if (payload["aud"].is_string()) {
            audValid = (payload["aud"].get<std::string>() == config_.audience);
        } else if (payload["aud"].is_array()) {
            for (const auto &aud : payload["aud"]) {
                if (aud.is_string() && aud.get<std::string>() == config_.audience) {
                    audValid = true;
                    break;
                }
            }
        }
        if (!audValid) {
            info.status = Status(StatusCode::FAILED, "JWT audience validation failed");
            return info;
        }

        // Validate azp (authorized party) if present in token
        if (payload.contains("azp") && payload["azp"].is_string()) {
            // Optional: validate azp matches expected client_id
        }

        // Extract and validate iat (issued at)
        if (payload.contains("iat") && payload["iat"].is_number()) {
            uint64_t iat = payload["iat"].get<uint64_t>();
            auto now = static_cast<uint64_t>(std::time(nullptr));
            // Token should not be issued in the future (allow 60s clock skew)
            if (iat > now + 60) {
                info.status = Status(StatusCode::FAILED, "JWT iat claim indicates future time");
                return info;
            }
        }

        // Extract and validate nbf (not before)
        if (payload.contains("nbf") && payload["nbf"].is_number()) {
            uint64_t nbf = payload["nbf"].get<uint64_t>();
            auto now = static_cast<uint64_t>(std::time(nullptr));
            if (nbf > now) {
                info.status = Status(StatusCode::FAILED, "JWT not yet valid (nbf claim)");
                return info;
            }
        }

        // Extract exp
        if (!payload.contains("exp") || !payload["exp"].is_number()) {
            info.status = Status(StatusCode::FAILED, "JWT missing or invalid exp claim");
            return info;
        }
        info.exp = payload["exp"].get<int64_t>();

        // Check if token is expired
        auto now = static_cast<int64_t>(std::time(nullptr));
        if (info.exp > 0 && info.exp < now) {
            info.status = Status(StatusCode::FAILED, "JWT token has expired");
            return info;
        }

        // Collect roles from realm_access.roles and resource_access.<client>.roles
        std::vector<std::string> allRoles;
        if (payload.contains("realm_access") && payload["realm_access"].is_object()) {
            auto realmAccess = payload["realm_access"];
            if (realmAccess.contains("roles") && realmAccess["roles"].is_array()) {
                for (const auto &role : realmAccess["roles"]) {
                    if (role.is_string()) {
                        allRoles.push_back(role.get<std::string>());
                    }
                }
            }
        }
        if (payload.contains("resource_access") && payload["resource_access"].is_object()) {
            for (auto &[clientId, clientAccess] : payload["resource_access"].items()) {
                if (clientAccess.is_object() && clientAccess.contains("roles") && clientAccess["roles"].is_array()) {
                    for (const auto &role : clientAccess["roles"]) {
                        if (role.is_string()) {
                            allRoles.push_back(role.get<std::string>());
                        }
                    }
                }
            }
        }
        info.role = GetHighestRole(allRoles);
        // Default to "user" for any successfully authenticated principal
        if (info.role.empty()) {
            info.role = "user";
        }

        // Extract resource quotas from user attributes
        if (payload.contains("cpu_quota")) {
            if (payload["cpu_quota"].is_number()) {
                info.cpuQuota = payload["cpu_quota"].get<int64_t>();
            } else if (payload["cpu_quota"].is_string()) {
                try {
                    info.cpuQuota = std::stoll(payload["cpu_quota"].get<std::string>());
                } catch (...) {
                }
            }
        }
        if (payload.contains("mem_quota")) {
            if (payload["mem_quota"].is_number()) {
                info.memQuota = payload["mem_quota"].get<int64_t>();
            } else if (payload["mem_quota"].is_string()) {
                try {
                    info.memQuota = std::stoll(payload["mem_quota"].get<std::string>());
                } catch (...) {
                }
            }
        }

        info.status = Status::OK();
        YRLOG_DEBUG("Keycloak token verified successfully: userId={}, role={}, cpuQuota={}, memQuota={}", info.userId,
                    info.role, info.cpuQuota, info.memQuota);
    } catch (const nlohmann::json::exception &e) {
        info.status = Status(StatusCode::FAILED, std::string("Failed to parse JWT payload: ") + e.what());
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
    encoded.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += static_cast<char>(c);
        } else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            encoded += buf;
        }
    }
    return encoded;
}
}  // namespace

litebus::Future<ExternalUserInfo> KeycloakVerifier::LoginWithPassword(const std::string &username,
                                                                      const std::string &password)
{
    auto promise = std::make_shared<litebus::Promise<ExternalUserInfo>>();

    if (!config_.enabled || config_.url.empty() || config_.clientId.empty() || config_.clientSecret.empty()) {
        ExternalUserInfo info;
        info.userId = "";
        info.tenantId = "";
        info.status = Status(StatusCode::FAILED, "Keycloak not configured");
        promise->SetValue(info);
        return promise->GetFuture();
    }

    std::string tokenUrl = config_.url + "/realms/" + config_.realm + "/protocol/openid-connect/token";
    std::string body =
        "grant_type=password"
        "&client_id="
        + UrlEncode(config_.clientId) + "&client_secret=" + UrlEncode(config_.clientSecret)
        + "&username=" + UrlEncode(username) + "&password=" + UrlEncode(password) + "&scope=openid+profile+email";

    YRLOG_INFO("Attempting password grant for user: {}", username);

    litebus::Try<litebus::http::URL> url = litebus::http::URL::Decode(tokenUrl);
    if (url.IsError()) {
        ExternalUserInfo info;
        info.userId = "";
        info.tenantId = "";
        info.status = Status(StatusCode::FAILED, "Failed to parse token URL");
        promise->SetValue(info);
        return promise->GetFuture();
    }

    litebus::Future<litebus::http::Response> response =
        litebus::http::Post(url.Get(), litebus::None(), body, std::string("application/x-www-form-urlencoded"), 10000);

    if (response.IsError()) {
        YRLOG_ERROR("Password grant failed: {}", response.GetErrorCode());
        ExternalUserInfo info;
        info.userId = "";
        info.tenantId = "";
        info.status = Status(StatusCode::FAILED, "Password grant failed");
        promise->SetValue(info);
        return promise->GetFuture();
    }

    const auto &httpResponse = response.Get();
    if (httpResponse.retCode != litebus::http::OK) {
        YRLOG_ERROR("Password grant HTTP error: {} - {}", httpResponse.retCode, httpResponse.body);
        ExternalUserInfo info;
        info.userId = "";
        info.tenantId = "";
        info.status = Status(StatusCode::FAILED, "Password grant HTTP error");
        promise->SetValue(info);
        return promise->GetFuture();
    }

    try {
        auto json = nlohmann::json::parse(httpResponse.body);
        if (!json.contains("id_token")) {
            YRLOG_ERROR("Password grant response missing id_token");
            ExternalUserInfo info;
            info.userId = "";
            info.tenantId = "";
            info.status = Status(StatusCode::FAILED, "Password grant response missing id_token");
            promise->SetValue(info);
            return promise->GetFuture();
        }

        std::string idToken = json["id_token"].get<std::string>();
        ExternalUserInfo info = VerifySync(idToken);
        info.status = Status::OK();
        YRLOG_INFO("Password grant successful for user: {}", username);
        promise->SetValue(info);
    } catch (const nlohmann::json::exception &e) {
        YRLOG_ERROR("Failed to parse password grant response: {}", e.what());
        ExternalUserInfo info;
        info.userId = "";
        info.tenantId = "";
        info.status = Status(StatusCode::FAILED, std::string("Failed to parse response: ") + e.what());
        promise->SetValue(info);
    }

    return promise->GetFuture();
}

litebus::Future<ExternalUserInfo> KeycloakVerifier::ExchangeCode(const std::string &code,
                                                                 const std::string &redirectUri)
{
    auto promise = std::make_shared<litebus::Promise<ExternalUserInfo>>();

    if (!config_.enabled || config_.url.empty() || config_.clientId.empty() || config_.clientSecret.empty()) {
        ExternalUserInfo info;
        info.userId = "";
        info.tenantId = "";
        info.status = Status(StatusCode::FAILED, "Keycloak not configured");
        promise->SetValue(info);
        return promise->GetFuture();
    }

    std::string tokenUrl = config_.url + "/realms/" + config_.realm + "/protocol/openid-connect/token";
    std::string body =
        "grant_type=authorization_code"
        "&client_id="
        + UrlEncode(config_.clientId) + "&client_secret=" + UrlEncode(config_.clientSecret) + "&code=" + UrlEncode(code)
        + "&redirect_uri=" + UrlEncode(redirectUri);

    YRLOG_INFO("Exchanging authorization code for tokens");

    litebus::Try<litebus::http::URL> url = litebus::http::URL::Decode(tokenUrl);
    if (url.IsError()) {
        ExternalUserInfo info;
        info.userId = "";
        info.tenantId = "";
        info.status = Status(StatusCode::FAILED, "Failed to parse token URL");
        promise->SetValue(info);
        return promise->GetFuture();
    }

    litebus::Future<litebus::http::Response> response =
        litebus::http::Post(url.Get(), litebus::None(), body, std::string("application/x-www-form-urlencoded"), 10000);

    if (response.IsError()) {
        YRLOG_ERROR("Code exchange failed: {}", response.GetErrorCode());
        ExternalUserInfo info;
        info.userId = "";
        info.tenantId = "";
        info.status = Status(StatusCode::FAILED, "Code exchange failed");
        promise->SetValue(info);
        return promise->GetFuture();
    }

    const auto &httpResponse = response.Get();
    if (httpResponse.retCode != litebus::http::OK) {
        YRLOG_ERROR("Code exchange HTTP error: {} - {}", httpResponse.retCode, httpResponse.body);
        ExternalUserInfo info;
        info.userId = "";
        info.tenantId = "";
        info.status = Status(StatusCode::FAILED, "Code exchange HTTP error");
        promise->SetValue(info);
        return promise->GetFuture();
    }

    try {
        auto json = nlohmann::json::parse(httpResponse.body);
        if (!json.contains("id_token")) {
            YRLOG_ERROR("Code exchange response missing id_token");
            ExternalUserInfo info;
            info.userId = "";
            info.tenantId = "";
            info.status = Status(StatusCode::FAILED, "Code exchange response missing id_token");
            promise->SetValue(info);
            return promise->GetFuture();
        }

        std::string idToken = json["id_token"].get<std::string>();
        ExternalUserInfo info = VerifySync(idToken);
        info.status = Status::OK();
        YRLOG_INFO("Code exchange successful for user: {}", info.userId);
        promise->SetValue(info);
    } catch (const nlohmann::json::exception &e) {
        YRLOG_ERROR("Failed to parse code exchange response: {}", e.what());
        ExternalUserInfo info;
        info.userId = "";
        info.tenantId = "";
        info.status = Status(StatusCode::FAILED, std::string("Failed to parse response: ") + e.what());
        promise->SetValue(info);
    }

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

    if (!state.empty()) {
        url += "&state=" + UrlEncode(state);
    }

    if (type == "register") {
        url += "&action=register";
    }

    return url;
}

litebus::Future<std::pair<int64_t, int64_t>> KeycloakVerifier::QueryTenantQuota(const std::string &tenantId)
{
    auto promise = std::make_shared<litebus::Promise<std::pair<int64_t, int64_t>>>();

    if (!config_.enabled || config_.url.empty() || config_.clientId.empty() || config_.clientSecret.empty()) {
        std::pair<int64_t, int64_t> result(-1, -1);
        promise->SetValue(result);
        return promise->GetFuture();
    }

    std::string accessToken;
    bool useCached = false;

    {
        std::lock_guard<std::mutex> lock(serviceAccountMutex_);
        auto now = std::chrono::steady_clock::now();
        if (!cachedServiceAccountToken_.empty() && now < serviceAccountTokenExpiry_) {
            accessToken = cachedServiceAccountToken_;
            useCached = true;
        }
    }

    if (!useCached) {
        std::string tokenUrl = config_.url + "/realms/" + config_.realm + "/protocol/openid-connect/token";
        std::string body =
            "grant_type=client_credentials"
            "&client_id="
            + UrlEncode(config_.clientId) + "&client_secret=" + UrlEncode(config_.clientSecret);

        litebus::Try<litebus::http::URL> tokenUrlParsed = litebus::http::URL::Decode(tokenUrl);
        if (tokenUrlParsed.IsError()) {
            promise->SetValue(std::make_pair<int64_t, int64_t>(-1, -1));
            return promise->GetFuture();
        }

        litebus::Future<litebus::http::Response> tokenResponse = litebus::http::Post(
            tokenUrlParsed.Get(), litebus::None(), body, std::string("application/x-www-form-urlencoded"), 10000);

        if (tokenResponse.IsError() || tokenResponse.Get().retCode != litebus::http::OK) {
            YRLOG_ERROR("Failed to get service account token");
            promise->SetValue(std::make_pair<int64_t, int64_t>(-1, -1));
            return promise->GetFuture();
        }

        try {
            auto json = nlohmann::json::parse(tokenResponse.Get().body);
            accessToken = json["access_token"].get<std::string>();
            int expiresIn = json.value("expires_in", 300);

            {
                std::lock_guard<std::mutex> lock(serviceAccountMutex_);
                cachedServiceAccountToken_ = accessToken;
                serviceAccountTokenExpiry_ = std::chrono::steady_clock::now() + std::chrono::seconds(expiresIn - 30);
            }
        } catch (const nlohmann::json::exception &e) {
            YRLOG_ERROR("Failed to parse service account token response: {}", e.what());
            std::pair<int64_t, int64_t> result(-1, -1);
            promise->SetValue(result);
            return promise->GetFuture();
        }
    }

    std::string adminUrl = config_.url + "/admin/realms/" + config_.realm + "/users?username=" + UrlEncode(tenantId);

    litebus::Try<litebus::http::URL> adminUrlParsed = litebus::http::URL::Decode(adminUrl);
    if (adminUrlParsed.IsError()) {
        std::pair<int64_t, int64_t> result(-1, -1);
        promise->SetValue(result);
        return promise->GetFuture();
    }

    std::unordered_map<std::string, std::string> headers;
    headers["Authorization"] = "Bearer " + accessToken;
    litebus::Future<litebus::http::Response> userResponse =
        litebus::http::Get(adminUrlParsed.Get(), litebus::Some(headers), 5000);

    if (userResponse.IsError() || userResponse.Get().retCode != litebus::http::OK) {
        YRLOG_ERROR("Failed to query user: {}", userResponse.GetErrorCode());
        std::pair<int64_t, int64_t> result(-1, -1);
        promise->SetValue(result);
        return promise->GetFuture();
    }

    try {
        auto userJson = nlohmann::json::parse(userResponse.Get().body);
        if (!userJson.is_array() || userJson.empty() || !userJson[0].contains("attributes")) {
            std::pair<int64_t, int64_t> result(-1, -1);
            promise->SetValue(result);
            return promise->GetFuture();
        }

        auto attrs = userJson[0]["attributes"];
        int64_t cpuQuota = -1;
        int64_t memQuota = -1;

        if (attrs.contains("cpu_quota")) {
            if (attrs["cpu_quota"].is_array() && !attrs["cpu_quota"].empty()) {
                cpuQuota = std::stoll(attrs["cpu_quota"][0].get<std::string>());
            }
        }
        if (attrs.contains("mem_quota")) {
            if (attrs["mem_quota"].is_array() && !attrs["mem_quota"].empty()) {
                memQuota = std::stoll(attrs["mem_quota"][0].get<std::string>());
            }
        }

        std::pair<int64_t, int64_t> result(cpuQuota, memQuota);
        promise->SetValue(result);
    } catch (const nlohmann::json::exception &e) {
        YRLOG_ERROR("Failed to parse user response: {}", e.what());
        std::pair<int64_t, int64_t> result(-1, -1);
        promise->SetValue(result);
    }

    return promise->GetFuture();
}

}  // namespace functionsystem::iamserver
