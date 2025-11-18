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

#ifndef FUNCTION_AGENT_COMMON_UTILS_H
#define FUNCTION_AGENT_COMMON_UTILS_H

#include <string>
#include <async/option.hpp>
#include "common/proto/pb/message_pb.h"
#include "common/metadata/metadata.h"


namespace functionsystem::function_agent {

std::shared_ptr<messages::DeployRequest> SetDeployRequestConfig(
    const std::shared_ptr<messages::DeployInstanceRequest> &req, const std::shared_ptr<messages::Layer> &layer);
std::shared_ptr <messages::DeployRequest> BuildDeployRequestConfigByLayerInfo(
    const Layer &info, const std::shared_ptr <messages::DeployRequest> &config);

void AddDefaultEnv(const std::shared_ptr<messages::DeployInstanceRequest> &req, messages::RuntimeConfig &runtimeConf);
messages::RuntimeConfig SetRuntimeConfig(const std::shared_ptr<messages::DeployInstanceRequest> &req);
void SetDeploymentConfig(messages::DeploymentConfig *deploymentConf,
                         const std::shared_ptr<messages::DeployInstanceRequest> &req);
void SetTenantCredConfig(const std::shared_ptr<messages::DeployInstanceRequest> &req,
                         messages::RuntimeConfig &runtimeConf);
messages::RuntimeInstanceInfo SetRuntimeInstanceInfo(const std::shared_ptr<messages::DeployInstanceRequest> &req);
messages::DeploymentConfig SetDeploymentConfigOfLayer(const std::shared_ptr<messages::DeployInstanceRequest> &req,
                                                      const std::shared_ptr<messages::Layer> &layer);

void SetUserEnv(const std::shared_ptr<messages::DeployInstanceRequest> &req, messages::RuntimeConfig &runtimeConf);
void ParseDelegateEnv(const std::string &value, messages::RuntimeConfig &runtimeConf);

void SetCreateOptions(const std::shared_ptr<messages::DeployInstanceRequest> &req, messages::RuntimeConfig &runtimeConf,
                      const std::vector<std::string> &keyList);

void SetTLSConfig(const std::shared_ptr<messages::DeployInstanceRequest> &req, messages::RuntimeConfig &runtimeConf);

void SetStartRuntimeInstanceRequestConfig(const std::unique_ptr<messages::StartInstanceRequest> &startInstanceRequest,
                                          const std::shared_ptr<messages::DeployInstanceRequest> &req);

void SetStopRuntimeInstanceRequest(messages::StopInstanceRequest &stopInstanceRequest,
                                   const std::shared_ptr<messages::KillInstanceRequest> &req);

void SetDelegateDecryptInfo(const std::shared_ptr<messages::DeployInstanceRequest> &req,
                            messages::RuntimeConfig &runtimeConf);

void SetSubDirConfig(const std::shared_ptr<messages::DeployInstanceRequest> &req, messages::RuntimeConfig &runtimeConf);

std::unordered_map<std::string, std::shared_ptr<messages::Layer>> SetDeployingRequestLayers(
    const messages::FuncDeploySpec &spec);

std::string JoinEntryFile(const std::shared_ptr<messages::DeployInstanceRequest> &req,
                          const std::string &entryFileName);

bool HasSuffix(const std::string &source, const std::string &suffix);

bool IsDir(const std::string &path);

std::vector<std::string> Field(const std::string &str, const char &ch);

void AddLayer(const std::shared_ptr<messages::DeployInstanceRequest> &req);
litebus::Option<std::string> DecryptDelegateData(const std::string &str, const std::string &defKey);

void ParseEnvInfoJson(const std::string &parsedJson, messages::RuntimeConfig &runtimeConf);

void ParseMountConfig(messages::RuntimeConfig &runtimeConfig, const std::string &str);
}  // namespace functionsystem::function_agent
#endif  // FUNCTION_AGENT_COMMON_UTILS_H