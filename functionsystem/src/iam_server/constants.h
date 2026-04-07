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

#ifndef IAM_SERVER_CONSTANTS_H
#define IAM_SERVER_CONSTANTS_H

#include <cstdint>
#include <string>
#include <unordered_map>

namespace functionsystem::iamserver {
const std::string SPLIT_SYMBOL = "_";
const std::string SPLIT_SYMBOL_TIMESTAMP = "+";

const uint32_t TOKEN_NEVER_EXPIRE = 0;                  // expiredTimeSpan = 0 means token never expires
const uint32_t CHECK_EXPIRED_INTERVAL = 2 * 60 * 1000;  // unit: ms, check cred every 2 min
const uint32_t TIME_AHEAD_OF_EXPIRED = 10 * 60;         // unit: s, update cred 10 min before expired
const int32_t WATCH_TIMEOUT_MS = 30000;

const uint32_t MS_SECOND = 1000;
const uint32_t MIN_AHEAD_TIME_FACTOR = 3;
const uint32_t MIN_EXPIRED_FACTOR = 2;

// User role constants (must match Keycloak roles)
const std::string ROLE_ADMIN = "admin";
const std::string ROLE_DEVELOPER = "developer";
const std::string ROLE_USER = "user";
const std::string ROLE_VIEWER = "viewer";

// Role priority map (higher number = higher priority)
inline int GetRolePriority(const std::string &role)
{
    if (role == ROLE_ADMIN)
        return 4;
    if (role == ROLE_DEVELOPER)
        return 3;
    if (role == ROLE_USER)
        return 2;
    if (role == ROLE_VIEWER)
        return 1;
    return 0;
}
}  // namespace functionsystem::iamserver

#endif  // IAM_SERVER_CONSTANTS_H
