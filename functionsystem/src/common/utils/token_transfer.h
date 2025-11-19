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

#ifndef COMMON_UTILS_TOKEN_TRANSFER_H
#define COMMON_UTILS_TOKEN_TRANSFER_H

#include <nlohmann/json.hpp>

#include "common/logs/logging.h"

namespace functionsystem {
const std::string TOKEN_STR = "token";
const std::string SALT_STR = "salt";
const std::string SPLIT_SYMBOL_TOKEN = "+";
// if token will be expired in $offset seconds, we need to generate new right now
const uint32_t NEW_TOKEN_EXPIRED_OFFSET = 30;

struct TokenSalt {
    std::string token;
    std::string salt;
    uint64_t expiredTimeStamp{ 0 };
    Status status = Status::OK();
};

[[maybe_unused]] static std::string TransToJsonFromTokenSalt(const TokenSalt &tokenSalt)
{
    nlohmann::json val = nlohmann::json::object();
    val[TOKEN_STR] = tokenSalt.token;
    val[SALT_STR] = tokenSalt.salt;
    return val.dump();
}

[[maybe_unused]] static std::shared_ptr<TokenSalt> TransTokenSaltFromJson(const std::string &json)
{
    auto tokenSalt = std::make_shared<TokenSalt>();
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json);
    } catch (std::exception &error) {
        YRLOG_WARN("failed to parse token and salt, error: {}", error.what());
        return tokenSalt;
    }
    if (j.find(TOKEN_STR) != j.end()) {
        tokenSalt->token = j.at(TOKEN_STR).get<std::string>();
    }
    if (j.find(SALT_STR) != j.end()) {
        tokenSalt->salt = j.at(SALT_STR).get<std::string>();
    }
    return tokenSalt;
}

[[maybe_unused]] static void CleanSensitiveStrMemory(std::string &sensitiveStr, const std::string &msg)
{
    auto ret = memset_s(&sensitiveStr[0], sensitiveStr.length(), 0, sensitiveStr.length());
    if (ret != 0) {
        YRLOG_WARN("token clean memory failed when {}.", msg);
    }
}

[[maybe_unused]] static Status DecryptTokenSaltFromStorage(const std::string &tokenStrFromStorage,
                                                           const std::shared_ptr<TokenSalt> &tokenSalt)
{
    auto pos = tokenStrFromStorage.find('_');
    if (pos == std::string::npos) {
        return Status(StatusCode::FAILED, "decrypt token from storage error!");
    }
    std::string key = tokenStrFromStorage.substr(0, pos);
    std::string info = tokenStrFromStorage.substr(pos + 1);
    std::string tokenStr(info);
    // token in storage format is: Encrypt(encryptToken+timestamp)
    pos = tokenStr.rfind(SPLIT_SYMBOL_TOKEN);
    if (pos == std::string::npos) {
        return Status(StatusCode::FAILED, "get token from decrypt storage token failed!");
    }
    tokenSalt->token = tokenStr.substr(0, pos);
    try {
        std::string expiredTimeStr = tokenStr.substr(pos + 1);
        tokenSalt->expiredTimeStamp = std::stoull(expiredTimeStr);
    } catch (std::exception &e) {
        return Status(StatusCode::FAILED, "transform time stamp type failed, err:" + std::string(e.what()));
    }
    CleanSensitiveStrMemory(tokenStr, "decrypt token from storage");
    return Status::OK();
}
}  // namespace functionsystem

#endif  // COMMON_UTILS_TOKEN_TRANSFER_H
