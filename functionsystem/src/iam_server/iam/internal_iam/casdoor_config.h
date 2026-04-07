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

#ifndef IAM_SERVER_INTERNAL_IAM_CASDOOR_CONFIG_H
#define IAM_SERVER_INTERNAL_IAM_CASDOOR_CONFIG_H

#include <string>

namespace functionsystem::iamserver {

struct CasdoorConfig {
    std::string endpoint;        // Casdoor server endpoint (internal network)
    std::string publicEndpoint;  // Casdoor public endpoint (for browser redirects)
    std::string clientId;        // Casdoor Client ID
    std::string clientSecret;    // Casdoor Client Secret
    std::string organization;    // Casdoor Organization name
    std::string application;     // Casdoor Application name
    std::string adminUser;       // Casdoor admin username for user management
    std::string adminPassword;   // Casdoor admin password for user management
    std::string jwtPublicKey;    // Public key (PEM) for verifying Casdoor JWTs
    bool enabled{ false };       // Whether Casdoor integration is enabled
};

}  // namespace functionsystem::iamserver

#endif  // IAM_SERVER_INTERNAL_IAM_CASDOOR_CONFIG_H
