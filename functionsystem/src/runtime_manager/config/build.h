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

#ifndef RUNTIME_MANAGER_CONFIG_BUILD_H
#define RUNTIME_MANAGER_CONFIG_BUILD_H

#include <iostream>
#include <nlohmann/json.hpp>
#include <vector>

#include "common/proto/pb/message_pb.h"
#include "runtime_manager/executor/executor.h"

namespace functionsystem::runtime_manager {

const std::string LD_LIBRARY_PATH = "LD_LIBRARY_PATH";
const std::string RUNTIME_DIRECT_CONNECTION_ENABLE = "RUNTIME_DIRECT_CONNECTION_ENABLE";
const std::string ENABLE_CLEAN_STREAM_PRODUCER = "ENABLE_CLEAN_STREAM_PRODUCER";
const std::string YR_DEBUG_SERVER_PORT = "YR_DEBUG_SERVER_PORT";

struct Envs {
    std::map<std::string, std::string> posixEnvs{};
    std::map<std::string, std::string> customResourceEnvs{};
    std::map<std::string, std::string> userEnvs{};
};

struct RuntimeFeatures {
    bool serverMode{false}; // runtime server
    std::string serverPort;

    bool runtimeDirectConnectionEnable{false};
    std::string directRuntimeServerPort;

    bool cleanStreamProducerEnable{true};
};

Envs GenerateEnvs(const RuntimeConfig &config, const std::shared_ptr<messages::StartInstanceRequest> &request,
                  const std::string &port, const std::vector<int> &cardsIDs);

Envs GenerateEnvs(const RuntimeConfig &config, const std::shared_ptr<messages::StartInstanceRequest> &request,
                  const std::string &port, const std::vector<int> &cardsIDs, const RuntimeFeatures &features);

std::map<std::string, std::string> GeneratePosixEnvs(const RuntimeConfig &config,
                                                     const std::shared_ptr<messages::StartInstanceRequest> &request,
                                                     const std::string &port);

std::vector<std::string> GenerateLayerPath(const ::messages::RuntimeInstanceInfo &info);

std::map<std::string, std::string> GenerateUserEnvs(const ::messages::RuntimeInstanceInfo &info,
                                                    const std::vector<int> &cardsIDs);

void AddYuanRongEnvs(std::map<std::string, std::string> &envs);

std::string SelectRealIDs(const std::string &env, const std::vector<int> &cardsIDs);
std::string ReplaceDollarContent(const std::string &source, std::map<std::string, std::string> &env);
bool IsPreconfiguredEnv(std::string& key);
}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_CONFIG_BUILD_H
