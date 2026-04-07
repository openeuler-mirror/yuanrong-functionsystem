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

#ifndef IAM_SERVER_INTERNAL_IAM_EXTERNAL_AUTH_VERIFIER_H
#define IAM_SERVER_INTERNAL_IAM_EXTERNAL_AUTH_VERIFIER_H

#include <memory>
#include <string>

#include "async/future.hpp"
#include "common/status/status.h"

namespace functionsystem::iamserver {

/**
 * Common structure for user info retrieved from external auth providers (Keycloak, Casdoor, etc.)
 */
struct ExternalUserInfo {
    std::string userId;      // Unique user ID from the provider (e.g., "sub" claim)
    std::string tenantId;    // Tenant or organization ID
    std::string role;        // Mapped role in the system
    int64_t cpuQuota{ -1 };  // CPU quota in millicores, -1 means not set
    int64_t memQuota{ -1 };  // Memory quota in MB, -1 means not set
    int64_t exp{ 0 };        // Token expiration timestamp, -1 means never expire
    Status status;           // Verification or operation status
};

/**
 * Abstract interface for external authentication providers.
 * Decouples iam-server from specific provider implementations like Keycloak or Casdoor.
 */
class ExternalAuthVerifier {
public:
    virtual ~ExternalAuthVerifier() = default;

    /**
     * Verify an external ID token and extract user information.
     */
    virtual litebus::Future<ExternalUserInfo> Verify(const std::string &token) = 0;

    /**
     * Authenticate using username and password.
     */
    virtual litebus::Future<ExternalUserInfo> LoginWithPassword(const std::string &username,
                                                                const std::string &password) = 0;

    /**
     * Exchange an authorization code for user information.
     */
    virtual litebus::Future<ExternalUserInfo> ExchangeCode(const std::string &code, const std::string &redirectUri) = 0;

    /**
     * Generate an authentication URL for the provider.
     * @param type "login", "register", or "logout"
     */
    virtual std::string GetAuthUrl(const std::string &type, const std::string &redirectUri,
                                   const std::string &state) = 0;

    /**
     * Query tenant-specific resource quotas from the provider.
     */
    virtual litebus::Future<std::pair<int64_t, int64_t>> QueryTenantQuota(const std::string &tenantId) = 0;
};

}  // namespace functionsystem::iamserver

#endif  // IAM_SERVER_INTERNAL_IAM_EXTERNAL_AUTH_VERIFIER_H
