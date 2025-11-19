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


#ifndef COMMON_AKSK_UTIL_H
#define COMMON_AKSK_UTIL_H

#include <map>
#include <memory>

#include "actor/aid.hpp"
#include "common/common_flags/common_flags.h"
#include "common/status/status.h"
#include "common/utils/key_for_aksk.h"
#include "common/utils/raii.h"
#include "litebus.h"
#include "sign_request.h"
#include "utils/time_util.hpp"

namespace functionsystem {
const std::string HEADER_TOKEN_KEY = "X-Signature";              // key for token
const std::string HEADER_SIGNED_HEADER_KEY = "X-Signed-Header";  // key for signed header

std::map<std::string, std::string> SignHttpRequest(const SignRequest &request, const KeyForAKSK &key);

bool VerifyHttpRequest(const SignRequest &request, const KeyForAKSK &key);

std::string SignActorMsg(const std::string &msgName, const std::string &msgBody, const KeyForAKSK &key);

bool VerifyActorMsg(const std::string &msgName, const std::string &msgBody, const std::string &authToken,
                    const KeyForAKSK &key);

bool ParseAuthToken(const std::string &str, std::string &alg, std::string &timestamp, std::string &ak,
                    std::string &signature);

Status InitLitebusAKSKEnv(const CommonFlags &flags);

std::string GenSignatureData(const std::string &canonicalRequest, const std::string &timestamp);

std::string GetActorMsgRequest(const std::string &msgName, const std::string &sha256);

SensitiveValue GetComponentDataKey();

litebus::Option<std::string> SerializeBodyToString(const runtime_rpc::StreamingMessage &message);

bool SignStreamingMessage(const std::string &accessKey, const SensitiveValue &secretKey,
                          const std::shared_ptr<runtime_rpc::StreamingMessage> &message);

bool VerifyStreamingMessage(const std::string &accessKey, const SensitiveValue &secretKey,
                            const std::shared_ptr<runtime_rpc::StreamingMessage> &message);

std::string SignTimestamp(const std::string &accessKey, const SensitiveValue &secretKey, const std::string &timestamp);

bool VerifyTimestamp(const std::string &accessKey, const SensitiveValue &secretKey, const std::string &timestamp,
                     const std::string &signature);

std::map<std::string, std::string> SignHttpRequestX(const SignRequest &request, const KeyForAKSK &key);
}  // namespace functionsystem

#endif  // COMMON_AKSK_UTIL_H
