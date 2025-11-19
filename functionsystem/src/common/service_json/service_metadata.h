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

#ifndef SERVICE_JSON_METADATA_H
#define SERVICE_JSON_METADATA_H

#include <memory>
#include <string>
#include <unordered_map>

#include "common/logs/logging.h"
#include "common/metadata/metadata.h"
#include "common/service_json/service_handler.h"
#include "common/service_json/service_info.h"

namespace functionsystem::service_json {

std::string GetEnvironmentText(const std::unordered_map<std::string, std::string> &funcEnv);

litebus::Option<EnvMetaData> BuildEnv(const FunctionConfig &functionConfig);

std::string BuildFuncName(const std::string &serviceName, const std::string &functionName);

std::string BuildFunctionURN(const std::string &serviceName, const std::string &functionName);

FuncMetaData BuildFuncMetaData(const ServiceInfo &serviceInfo, const FunctionConfig &functionConfig,
                               const std::string &functionName, const std::shared_ptr<BuildHandlerMap> &mapBuilder);

std::string ParseCodePath(const std::string &codePath, const std::string &yamlPath);

}  // namespace functionsystem::service_json

#endif  // FUNCTIONCORE_CPP_METADATA_H
