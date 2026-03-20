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

#include "casdoor_verifier.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <vector>

#include "common/hex/hex.h"
#include "common/logs/logging.h"
#include "httpd/http.hpp"
#include "httpd/http_connect.hpp"
#include "iam_server/constants.h"

namespace functionsystem::iamserver {

namespace {
const std::string JWT_SEPARATOR = ".";
const std::string CASDOOR_BUILTIN_ORG = "built-in";
const std::string CASDOOR_BUILTIN_APP = "app-built-in";

bool IsKnownRole(const std::string &role)
{
    return role == ROLE_ADMIN || role == ROLE_DEVELOPER || role == ROLE_USER || role == ROLE_VIEWER;
}

std::string ExtractCookieHeader(const litebus::http::Response &response)
{
    auto iter = response.headers.find("Set-Cookie");
    if (iter == response.headers.end() || iter->second.empty()) {
        return "";
    }

    const std::string &cookie = iter->second;
    size_t delimiter = cookie.find(';');
    return delimiter == std::string::npos ? cookie : cookie.substr(0, delimiter);
}

std::string ExtractPersistedRole(const nlohmann::json &userJson)
{
    const std::vector<std::string> candidates = { "role", "tag" };
    for (const auto &key : candidates) {
        if (!userJson.contains(key) || !userJson[key].is_string()) {
            continue;
        }

        std::string value = userJson[key].get<std::string>();
        if (IsKnownRole(value)) {
            return value;
        }
    }
    return "";
}

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

std::string Base64UrlDecode(const std::string &input)
{
    std::string base64 = input;
    std::replace(base64.begin(), base64.end(), '-', '+');
    std::replace(base64.begin(), base64.end(), '_', '/');
    while (base64.size() % 4 != 0) {
        base64 += '=';
    }

    BIO *b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO *bmem = BIO_new_mem_buf(base64.c_str(), static_cast<int>(base64.length()));
    b64 = BIO_push(b64, bmem);

    std::vector<char> buffer(base64.length());
    int decoded_len = BIO_read(b64, buffer.data(), static_cast<int>(buffer.size()));
    BIO_free_all(b64);

    if (decoded_len < 0)
        return "";
    return std::string(buffer.data(), decoded_len);
}

std::string NormalizePem(const std::string &pem)
{
    if (pem.find("-----BEGIN ") == std::string::npos || pem.find("-----END ") == std::string::npos) {
        return pem;
    }

    auto beginPos = pem.find("-----BEGIN ");
    auto beginEnd = pem.find("-----", beginPos + 11);
    if (beginPos == std::string::npos || beginEnd == std::string::npos) {
        return pem;
    }

    auto endPos = pem.find("-----END ", beginEnd);
    auto endEnd = pem.find("-----", endPos == std::string::npos ? std::string::npos : endPos + 9);
    if (endPos == std::string::npos || endEnd == std::string::npos) {
        return pem;
    }

    std::string beginLine = pem.substr(beginPos, beginEnd - beginPos + 5);
    std::string endLine = pem.substr(endPos, endEnd - endPos + 5);
    std::string body = pem.substr(beginEnd + 5, endPos - (beginEnd + 5));

    body.erase(std::remove_if(body.begin(), body.end(), [](unsigned char c) { return std::isspace(c); }), body.end());

    std::string normalized = beginLine + "\n";
    for (size_t i = 0; i < body.size(); i += 64) {
        normalized += body.substr(i, 64);
        normalized += "\n";
    }
    normalized += endLine + "\n";
    return normalized;
}

EVP_PKEY *ReadPemPublicKey(const std::string &pem)
{
    std::string normalizedPem = NormalizePem(pem);

    BIO *bio = BIO_new_mem_buf(normalizedPem.c_str(), static_cast<int>(normalizedPem.length()));
    if (!bio) {
        return nullptr;
    }

    EVP_PKEY *pkey = PEM_read_bio_PUBKEY(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (pkey) {
        return pkey;
    }

    bio = BIO_new_mem_buf(normalizedPem.c_str(), static_cast<int>(normalizedPem.length()));
    if (!bio) {
        return nullptr;
    }

    X509 *cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    if (!cert) {
        return nullptr;
    }

    pkey = X509_get_pubkey(cert);
    X509_free(cert);
    return pkey;
}
}  // namespace

CasdoorVerifier::CasdoorVerifier(const CasdoorConfig &config) : config_(config)
{
}

litebus::Future<ExternalUserInfo> CasdoorVerifier::Verify(const std::string &idToken)
{
    auto promise = std::make_shared<litebus::Promise<ExternalUserInfo>>();
    promise->SetValue(VerifyJwt(idToken));
    return promise->GetFuture();
}

ExternalUserInfo CasdoorVerifier::VerifyJwt(const std::string &idToken)
{
    ExternalUserInfo info;
    if (!config_.enabled) {
        info.status = Status(StatusCode::FAILED, "Casdoor integration is not enabled");
        return info;
    }

    if (!VerifySignature(idToken)) {
        info.status = Status(StatusCode::FAILED, "Casdoor JWT signature verification failed");
        return info;
    }

    try {
        size_t firstDot = idToken.find(JWT_SEPARATOR);
        size_t secondDot = idToken.find(JWT_SEPARATOR, firstDot + 1);
        std::string payloadJson = Base64UrlDecode(idToken.substr(firstDot + 1, secondDot - firstDot - 1));
        auto payload = nlohmann::json::parse(payloadJson);

        info.userId = payload.value("name", "");
        info.tenantId = payload.value("owner", "");
        info.role = payload.value("role", "");
        info.exp = payload.value("exp", 0LL);

        auto now = static_cast<int64_t>(std::time(nullptr));
        if (info.exp > 0 && info.exp < now) {
            info.status = Status(StatusCode::FAILED, "Casdoor JWT token has expired");
            return info;
        }

        if (info.role.empty()) {
            info.role = ResolveUserRole(info.tenantId, info.userId);
        }
        if (info.role.empty()) {
            info.role = ROLE_USER;
        }

        // Extract quotas if available in custom attributes
        if (payload.contains("cpu_quota")) {
            info.cpuQuota = payload["cpu_quota"].is_number() ? payload["cpu_quota"].get<int64_t>()
                                                             : std::stoll(payload["cpu_quota"].get<std::string>());
        }
        if (payload.contains("mem_quota")) {
            info.memQuota = payload["mem_quota"].is_number() ? payload["mem_quota"].get<int64_t>()
                                                             : std::stoll(payload["mem_quota"].get<std::string>());
        }

        info.status = Status::OK();
    } catch (const std::exception &e) {
        info.status = Status(StatusCode::FAILED, std::string("Failed to parse Casdoor JWT: ") + e.what());
    }
    return info;
}

std::string CasdoorVerifier::ResolveUserRole(const std::string &owner, const std::string &name)
{
    if (owner.empty() || name.empty() || config_.adminUser.empty() || config_.adminPassword.empty()) {
        return "";
    }

    std::string loginUrl = config_.endpoint + "/api/login";
    litebus::Try<litebus::http::URL> loginParsed = litebus::http::URL::Decode(loginUrl);
    if (loginParsed.IsError()) {
        return "";
    }

    nlohmann::json loginBody = { { "type", "login" },
                                 { "username", config_.adminUser },
                                 { "password", config_.adminPassword },
                                 { "organization", CASDOOR_BUILTIN_ORG },
                                 { "application", CASDOOR_BUILTIN_APP } };

    auto loginResponse = litebus::http::Post(loginParsed.Get(), litebus::None(), loginBody.dump(),
                                             std::string("application/json"), 5000);
    if (loginResponse.IsError() || loginResponse.Get().retCode != litebus::http::OK) {
        YRLOG_WARN("Failed to login Casdoor admin user for role resolution");
        return "";
    }

    std::string cookie = ExtractCookieHeader(loginResponse.Get());
    if (cookie.empty()) {
        YRLOG_WARN("Casdoor admin login did not return session cookie");
        return "";
    }

    std::unordered_map<std::string, std::string> headers;
    headers["Cookie"] = cookie;

    std::string userId = owner + "/" + name;
    std::string getUserUrl = config_.endpoint + "/api/get-user?id=" + UrlEncode(userId);
    auto getUserParsed = litebus::http::URL::Decode(getUserUrl);
    if (getUserParsed.IsError()) {
        return "";
    }

    auto userResponse = litebus::http::Get(getUserParsed.Get(), litebus::Some(headers), 5000);
    if (userResponse.IsError() || userResponse.Get().retCode != litebus::http::OK) {
        YRLOG_WARN("Failed to query Casdoor user {}", userId);
        return "";
    }

    try {
        auto body = nlohmann::json::parse(userResponse.Get().body);
        if (!body.contains("data") || body["data"].is_null() || !body["data"].is_object()) {
            return "";
        }

        auto userJson = body["data"];
        std::string persistedRole = ExtractPersistedRole(userJson);
        if (!persistedRole.empty()) {
            return persistedRole;
        }

        std::string signupApplication = userJson.value("signupApplication", "");
        std::string registerSource = userJson.value("registerSource", "");
        const std::string expectedSource = config_.organization + "/" + config_.application;
        bool isManagedSignup = signupApplication == config_.application || registerSource == expectedSource;
        if (!isManagedSignup) {
            return "";
        }

        userJson["tag"] = ROLE_DEVELOPER;
        std::string updateUserUrl = config_.endpoint + "/api/update-user?id=" + UrlEncode(userId);
        auto updateUserParsed = litebus::http::URL::Decode(updateUserUrl);
        if (updateUserParsed.IsError()) {
            return "";
        }

        auto updateResponse = litebus::http::Post(updateUserParsed.Get(), litebus::Some(headers), userJson.dump(),
                                                  std::string("application/json"), 5000);
        if (updateResponse.IsError() || updateResponse.Get().retCode != litebus::http::OK) {
            YRLOG_WARN("Failed to persist Casdoor role tag for user {}", userId);
            return "";
        }

        YRLOG_INFO("Assigned Casdoor user {} developer role tag after signup", userId);
        return ROLE_DEVELOPER;
    } catch (const std::exception &e) {
        YRLOG_WARN("Failed to resolve Casdoor role for {}: {}", userId, e.what());
        return "";
    }
}

bool CasdoorVerifier::VerifySignature(const std::string &token)
{
    if (config_.jwtPublicKey.empty()) {
        YRLOG_ERROR("Casdoor JWT public key is not configured");
        return false;
    }

    size_t secondDot = token.rfind(JWT_SEPARATOR);
    if (secondDot == std::string::npos)
        return false;

    std::string signingInput = token.substr(0, secondDot);
    std::string signature = Base64UrlDecode(token.substr(secondDot + 1));

    EVP_PKEY *pkey = ReadPemPublicKey(config_.jwtPublicKey);

    if (!pkey) {
        YRLOG_ERROR("Failed to read Casdoor public key");
        return false;
    }

    EVP_MD_CTX *mdCtx = EVP_MD_CTX_new();
    EVP_DigestVerifyInit(mdCtx, nullptr, EVP_sha256(), nullptr, pkey);
    int ret = EVP_DigestVerify(mdCtx, reinterpret_cast<const unsigned char *>(signature.data()), signature.length(),
                               reinterpret_cast<const unsigned char *>(signingInput.data()), signingInput.length());

    EVP_MD_CTX_free(mdCtx);
    EVP_PKEY_free(pkey);

    return ret == 1;
}

litebus::Future<ExternalUserInfo> CasdoorVerifier::LoginWithPassword(const std::string &username,
                                                                     const std::string &password)
{
    auto promise = std::make_shared<litebus::Promise<ExternalUserInfo>>();

    // Direct password login via Casdoor /api/login typically establishes a session cookie
    // but does not return an OIDC ID Token directly in a way compatible with ExternalUserInfo.
    // To remain secure and aligned with the contract, we disable this direct flow and
    // encourage the use of the standard OAuth2/OIDC flow.
    ExternalUserInfo info;
    info.status =
        Status(StatusCode::FAILED, "Direct password login is not supported for Casdoor. Please use the OIDC flow.");
    promise->SetValue(info);
    return promise->GetFuture();
}

litebus::Future<ExternalUserInfo> CasdoorVerifier::ExchangeCode(const std::string &code, const std::string &redirectUri)
{
    auto promise = std::make_shared<litebus::Promise<ExternalUserInfo>>();

    // Casdoor's authorization-code token exchange endpoint is under /api/login/oauth/access_token.
    std::string tokenUrl = config_.endpoint + "/api/login/oauth/access_token";
    std::string body =
        "grant_type=authorization_code"
        "&client_id="
        + UrlEncode(config_.clientId) + "&client_secret=" + UrlEncode(config_.clientSecret) + "&code=" + UrlEncode(code)
        + "&redirect_uri=" + UrlEncode(redirectUri);

    litebus::Try<litebus::http::URL> url = litebus::http::URL::Decode(tokenUrl);
    litebus::http::Post(url.Get(), litebus::None(), litebus::Some(body),
                        litebus::Some(std::string("application/x-www-form-urlencoded")), litebus::Some(5000UL))
        .OnComplete([this, promise](const litebus::Future<litebus::http::Response> &future) {
            if (future.IsError() || future.Get().retCode != litebus::http::OK) {
                ExternalUserInfo info;
                info.status = Status(StatusCode::FAILED, "Code exchange failed");
                promise->SetValue(info);
                return;
            }
            try {
                auto resp = nlohmann::json::parse(future.Get().body);
                std::string idToken = resp.value("id_token", resp.value("access_token", ""));
                if (idToken.empty()) {
                    ExternalUserInfo info;
                    info.status = Status(StatusCode::FAILED, "No token in response");
                    promise->SetValue(info);
                    return;
                }
                promise->SetValue(VerifyJwt(idToken));
            } catch (...) {
                ExternalUserInfo info;
                info.status = Status(StatusCode::FAILED, "Parse error");
                promise->SetValue(info);
            }
        });

    return promise->GetFuture();
}

std::string CasdoorVerifier::GetAuthUrl(const std::string &type, const std::string &redirectUri,
                                        const std::string &state)
{
    std::string baseUrl = config_.publicEndpoint.empty() ? config_.endpoint : config_.publicEndpoint;

    if (type == "register") {
        // This Casdoor frontend only registers /signup and /signup/:applicationName routes.
        // The application owner is resolved server-side via /api/get-application?id=admin/<application>.
        std::string url = baseUrl + "/signup/" + config_.application;
        url += "?redirect_uri=" + UrlEncode(redirectUri);
        url += "&state=" + UrlEncode(state);
        return url;
    }

    if (type == "logout") {
        std::string url = baseUrl + "/api/logout";
        url += "?redirect_uri=" + UrlEncode(redirectUri);
        return url;
    }

    // Default: login / authorize
    std::string url = baseUrl + "/login/oauth/authorize";
    url += "?client_id=" + UrlEncode(config_.clientId);
    url += "&scope=openid+profile+email";
    url += "&response_type=code";
    url += "&redirect_uri=" + UrlEncode(redirectUri);
    url += "&state=" + UrlEncode(state);
    return url;
}

litebus::Future<std::pair<int64_t, int64_t>> CasdoorVerifier::QueryTenantQuota(const std::string &tenantId)
{
    auto promise = std::make_shared<litebus::Promise<std::pair<int64_t, int64_t>>>();

    // In Casdoor, we query the user to get attributes.
    // This requires a client-credentials token first (similar to Keycloak).
    // TODO: Implement Casdoor User API query
    promise->SetValue(std::make_pair(static_cast<int64_t>(-1), static_cast<int64_t>(-1)));
    return promise->GetFuture();
}

}  // namespace functionsystem::iamserver
