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

#ifndef IAM_SERVER_INTERNAL_IAM_KEYCLOAK_VERIFIER_H
#define IAM_SERVER_INTERNAL_IAM_KEYCLOAK_VERIFIER_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "async/future.hpp"
#include "common/status/status.h"
#include "external_auth_verifier.h"
#include "keycloak_config.h"

namespace functionsystem::iamserver {

struct JwkKey {
    std::string kid;  // key ID
    std::string kty;  // key type (RSA)
    std::string n;    // modulus (base64url)
    std::string e;    // exponent (base64url)
    std::string alg;  // algorithm (RS256)
};

struct JwksCache {
    std::unordered_map<std::string, JwkKey> keys;  // kid -> JwkKey
    std::chrono::steady_clock::time_point fetchedAt;
    int ttlSeconds;
};

class KeycloakVerifier : public ExternalAuthVerifier {
public:
    explicit KeycloakVerifier(const KeycloakConfig &config);
    ~KeycloakVerifier() override = default;

    /**
     * Verify a Keycloak ID token and extract user info
     */
    litebus::Future<ExternalUserInfo> Verify(const std::string &idToken) override;

    /**
     * Login with username/password
     */
    litebus::Future<ExternalUserInfo> LoginWithPassword(const std::string &username,
                                                        const std::string &password) override;

    /**
     * Exchange authorization code for user info
     */
    litebus::Future<ExternalUserInfo> ExchangeCode(const std::string &code, const std::string &redirectUri) override;

    /**
     * Get Keycloak auth URL
     */
    std::string GetAuthUrl(const std::string &type, const std::string &redirectUri, const std::string &state) override;

    /**
     * Query tenant quota from Keycloak Admin API
     */
    litebus::Future<std::pair<int64_t, int64_t>> QueryTenantQuota(const std::string &tenantId) override;

    /**
     * Check if Keycloak integration is enabled
     */
    bool IsEnabled() const
    {
        return config_.enabled;
    }

    /**
     * Get the Keycloak configuration
     */
    const KeycloakConfig &GetConfig() const
    {
        return config_;
    }

private:
    /**
     * Fetch JWKS synchronously
     * @return Status indicating success or failure
     */
    Status FetchJwksSync();

    /**
     * Check if JWKS cache needs refresh
     */
    bool NeedRefreshJwks() const;

    /**
     * Refresh JWKS once across concurrent callers.
     * @param forceRefresh Skip TTL check and force a single shared refresh attempt
     * @return Status of the shared refresh attempt
     */
    Status RefreshJwksOnce(bool forceRefresh = false);

    /**
     * Verify JWT signature using RS256
     * @param token The JWT token
     * @param jwkKey The public key to use for verification
     * @return true if signature is valid
     */
    bool VerifyRs256Signature(const std::string &token, const JwkKey &jwkKey);

    /**
     * Parse JWT and extract header and payload
     * @param token The JWT token
     * @param header Output: base64url-decoded header JSON
     * @param payload Output: base64url-decoded payload JSON
     * @param signature Output: base64url-decoded signature
     * @return Status::OK() if parsing succeeded
     */
    Status ParseJwt(const std::string &token, std::string &header, std::string &payload, std::string &signature);

    /**
     * Extract the highest priority role from realm_access.roles
     * @param roles List of roles from the token
     * @return The highest priority role name, or empty if none match
     */
    std::string GetHighestRole(const std::vector<std::string> &roles);

    /**
     * Synchronous verify method (used after JWKS is cached)
     * @param idToken The Keycloak ID token to verify
     * @return ExternalUserInfo with verification result
     */
    ExternalUserInfo VerifySync(const std::string &idToken);
    /**
     * Convert base64url encoded modulus and exponent to RSA public key
     * @param n Base64url encoded modulus
     * @param e Base64url encoded exponent
     * @return RSA public key in PEM format, or empty string on failure
     */
    std::string BuildRsaPublicKey(const std::string &n, const std::string &e);

    KeycloakConfig config_;
    mutable std::mutex jwksMutex_;
    mutable std::mutex jwksRefreshMutex_;
    mutable std::condition_variable jwksRefreshCv_;
    std::atomic<bool> jwksRefreshInProgress_{ false };
    Status lastJwksRefreshStatus_{ Status::OK() };
    std::shared_ptr<JwksCache> jwksCache_;

    // Service account token cache (for Admin API)
    mutable std::mutex serviceAccountMutex_;
    std::string cachedServiceAccountToken_;
    std::chrono::steady_clock::time_point serviceAccountTokenExpiry_;
};

}  // namespace functionsystem::iamserver

#endif  // IAM_SERVER_INTERNAL_IAM_KEYCLOAK_VERIFIER_H
