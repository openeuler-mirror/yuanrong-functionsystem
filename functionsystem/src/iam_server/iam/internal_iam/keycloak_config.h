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

#ifndef IAM_SERVER_INTERNAL_IAM_KEYCLOAK_CONFIG_H
#define IAM_SERVER_INTERNAL_IAM_KEYCLOAK_CONFIG_H

#include <string>

namespace functionsystem::iamserver {

struct KeycloakConfig {
    std::string url;             // Keycloak 服务端直连地址（容器网络）
    std::string publicUrl;       // 浏览器侧公开地址（用于拼 auth URL）
    std::string realm;           // Realm 名称
    std::string clientId;        // client ID for frontend
    std::string clientSecret;    // client secret for frontend
    std::string issuer;          // Expected issuer for JWT validation
    std::string audience;        // Expected audience for JWT validation
    bool enabled{ false };       // 是否启用
    int cacheTtlSeconds{ 300 };  // JWKS 缓存时间
};

}  // namespace functionsystem::iamserver

#endif  // IAM_SERVER_INTERNAL_IAM_KEYCLOAK_CONFIG_H
