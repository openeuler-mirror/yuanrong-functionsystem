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

#ifndef IAM_SERVER_INTERNAL_IAM_CASDOOR_VERIFIER_H
#define IAM_SERVER_INTERNAL_IAM_CASDOOR_VERIFIER_H

#include <memory>
#include <mutex>
#include <string>

#include "async/future.hpp"
#include "casdoor_config.h"
#include "common/status/status.h"
#include "external_auth_verifier.h"

namespace functionsystem::iamserver {

class CasdoorVerifier : public ExternalAuthVerifier {
public:
    explicit CasdoorVerifier(const CasdoorConfig &config);
    ~CasdoorVerifier() override = default;

    /**
     * Verify a Casdoor ID token (JWT) using public key
     */
    litebus::Future<ExternalUserInfo> Verify(const std::string &idToken) override;

    /**
     * Login with username/password directly to Casdoor
     */
    litebus::Future<ExternalUserInfo> LoginWithPassword(const std::string &username,
                                                        const std::string &password) override;

    /**
     * Exchange authorization code for user info
     */
    litebus::Future<ExternalUserInfo> ExchangeCode(const std::string &code, const std::string &redirectUri) override;

    /**
     * Get Casdoor auth URL for redirection
     */
    std::string GetAuthUrl(const std::string &type, const std::string &redirectUri, const std::string &state) override;

    /**
     * Query tenant quota from Casdoor user attributes
     */
    litebus::Future<std::pair<int64_t, int64_t>> QueryTenantQuota(const std::string &tenantId) override;

    bool IsEnabled() const
    {
        return config_.enabled;
    }

private:
    /**
     * Internal JWT verification using the configured public key
     */
    ExternalUserInfo VerifyJwt(const std::string &idToken);

    /**
     * Resolve a user's role from Casdoor and persist developer for newly registered users.
     */
    std::string ResolveUserRole(const std::string &owner, const std::string &name);

    /**
     * Verify signature using RSA public key
     */
    bool VerifySignature(const std::string &token);

    CasdoorConfig config_;
};

}  // namespace functionsystem::iamserver

#endif  // IAM_SERVER_INTERNAL_IAM_CASDOOR_VERIFIER_H
