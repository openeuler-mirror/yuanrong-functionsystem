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

#ifndef COMMON_UTILS_S3_CONFIG_H
#define COMMON_UTILS_S3_CONFIG_H

#include "common/utils/sensitive_value.h"
#include "common/constants/constants.h"
#include "function_agent/common/constants.h"

namespace functionsystem {
struct S3Config {
    bool useSSL{ false };
    std::string credentialType{ CREDENTIAL_TYPE_PERMANENT_CREDENTIALS };
    std::string accessKey{ "" };
    SensitiveValue secretKey{ "" };
    std::string endpoint{ "" };
    std::string protocol{ function_agent::S3_PROTOCOL_HTTPS };
    std::string caFile{ "" };
    bool trustedCA{ false };
};
}  // namespace functionsystem

#endif  // COMMON_UTILS_S3_CONFIG_H
