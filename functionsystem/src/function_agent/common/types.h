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

#ifndef FUNCTION_AGENT_TYPES_H
#define FUNCTION_AGENT_TYPES_H

#include <async/future.hpp>
#include <string>
#include <timer/timer.hpp>
#include <unordered_map>
#include <vector>

#include "function_agent/code_deployer/deployer.h"

namespace functionsystem::function_agent {

struct LayerBucket {
    std::string bucketURL;
    std::string objectID;
    std::string bucketID;
    std::string appID;
    std::string sha256;
};

struct FuncCodeReferInfo {
    int32_t runtimeNum = 0;
    std::vector<messages::Layer> layers;
};

struct LayerCodeReferInfo {
    int32_t runtimeNum = 0;
};

struct CodeReferInfo {
    std::unordered_set<std::string> instanceIDs;
    std::shared_ptr<Deployer> deployer;
    uint64_t lastAccessTimestamp{ 0 };
};

struct RuntimesDeploymentCache {
    std::unordered_map<std::string, functionsystem::messages::RuntimeInstanceInfo> runtimes;
};

struct CheckDeployPathResult {
    int32_t code = 0;
    std::string errorMsg;
};

struct RegisterInfo {
    litebus::Promise<messages::Registered> registeredPromise;
    litebus::Timer reRegisterTimer;
};

struct UpdateInstanceStatusInfo {
    std::shared_ptr<litebus::Promise<messages::UpdateInstanceStatusResponse>> updateInstanceStatusPromise;
    litebus::AID fromRuntimeManager;
};

struct DecryptEnvResult {
    Status status;
    std::string decryptedContent;
};
}  // namespace functionsystem::function_agent
#endif  // FUNCTION_AGENT_TYPES_H