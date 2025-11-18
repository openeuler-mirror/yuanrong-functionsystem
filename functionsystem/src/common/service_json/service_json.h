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

#ifndef COMMON_SERVICE_JSON_H
#define COMMON_SERVICE_JSON_H

#include <dlfcn.h>

#include <map>
#include <nlohmann/json.hpp>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/logs/logging.h"
#include "common/metadata/metadata.h"
#include "common/service_json/service_info.h"

namespace functionsystem::service_json {

litebus::Option<std::vector<FunctionMeta>> GetFuncMetaFromServiceYaml(const std::string &filePath,
                                                                      const std::string &libPath);

litebus::Option<std::vector<service_json::ServiceInfo>> GetServiceInfosFromJson(const std::string &jsonStr);

litebus::Option<std::vector<FunctionMeta>> ConvertFunctionMeta(const std::vector<ServiceInfo> &serviceInfos,
                                                               const std::string &yamlPath);

bool NameMatch(const std::string &str, const std::string &regex);

bool CheckName(const std::string &name, const std::string regex, uint32_t minLen, uint32_t maxLen);

bool CheckServiceName(const std::string &serviceName);

bool CheckKind(const std::string &kind);

bool CheckServiceInfo(const service_json::ServiceInfo &serviceInfo);

bool CheckFunctionName(const std::string &functionName);

bool CheckRuntime(const std::string &runtime);

bool CheckCPUAndMemorySize(int64_t cpu, int64_t memory);

bool CheckEnv(const std::unordered_map<std::string, std::string> &envs);

bool CheckLayerName(const std::string &layerName);

bool ParseAndCheckLayerVersion(const std::string &layerVersion);

bool CheckFunctionRefLayer(const std::string &refLayer);

bool CheckFunctionLayers(const std::vector<std::string> &layers);

bool CheckMinInstance(int64_t minInstance);

bool CheckMaxInstance(int64_t maxInstance);

bool IsMinInstanceLargeThanMaxInstance(int64_t minInstance, int64_t maxInstance);

bool CheckConcurrentNum(int32_t concurrentNum);

bool CheckWorkerConfig(const FunctionConfig &function);

std::unordered_map<std::string, std::string> PackHookHandler(FunctionHookHandlerConfig functionHookHandlerConfig);

bool CheckHookHandlerRegularization(const std::string &handler, const std::string &runtime);

bool CheckHookHandler(const FunctionHookHandlerConfig &functionHookHandlerConfig, const std::string &runtime);

bool CheckFunctionConfig(const FunctionConfig &functionConfig);

bool CheckFunction(const std::string &functionName, const FunctionConfig &functionConfig);

bool CheckServiceInfos(std::vector<service_json::ServiceInfo> &serviceInfos);

void ParseFunctionHookHandlerConfig(service_json::FunctionHookHandlerConfig &functionHookHandlerConfig,
                                    const nlohmann::json &h);

void ParseDeviceInfo(DeviceMetaData &device, const nlohmann::json &h);

void ParseCodeMeta(service_json::FunctionConfig &functionConfig, const nlohmann::json &f);

void ParseEnvMeta(service_json::FunctionConfig &functionConfig, const nlohmann::json &f);

void ParseInstMeta(service_json::FunctionConfig &functionConfig, const nlohmann::json &f);

void ParseRes(service_json::FunctionConfig &functionConfig, const nlohmann::json &f);

void Parsefunction(service_json::FunctionConfig &functionConfig, const nlohmann::json &f);

void ParseServiceInfo(std::vector<service_json::ServiceInfo> &serviceInfos, const nlohmann::json &j);

litebus::Option<FunctionMeta> BuildFunctionMeta(const ServiceInfo &serviceInfo, const FunctionConfig &functionConfig,
                                                const std::string &functionName, const std::string &yamlPath);

void LoadFuncMetaFromServiceYaml(std::unordered_map<std::string, FunctionMeta> &map, const std::string &filePath,
                                 const std::string &libPath);

}  // namespace functionsystem::service_json

#endif  // COMMON_SERVICE_JSON_H
