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


#ifndef FUNCTION_AGENT_STATIC_FUNCTION_UTIL_H
#define FUNCTION_AGENT_STATIC_FUNCTION_UTIL_H

#include "common/metadata/metadata_type.h"
#include "common/proto/pb/message_pb.h"

namespace functionsystem::function_agent {
struct StaticFunctionConfig {
    uint32_t scheduleTimeoutMs;
    std::string instanceTypeNote;
    std::string delegateDirectoryInfo;
    std::string invokeLabels;
    std::string functionMetaPath;
    std::string functionSignature;
    std::unordered_map<std::string, std::string> extensions;
};

struct FunctionLanguageInfo {
    std::string functionId;
    resources::LanguageType languageType;
};

FunctionLanguageInfo GetLanguageInfo(const FunctionMeta &funcMeta);

void BuildScheduleRequest(const std::shared_ptr<messages::ScheduleRequest> &request, const FunctionMeta &meta,
                          const StaticFunctionConfig &config);
}  // namespace functionsystem::function_agent
#endif  // FUNCTION_AGENT_STATIC_FUNCTION_UTIL_H
