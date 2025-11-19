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

#ifndef FUNCTION_AGENT_FUNCTION_AGENT_STARTUP_H
#define FUNCTION_AGENT_FUNCTION_AGENT_STARTUP_H

#include "common/http/http_server.h"
#include "common/utils/module_driver.h"
#include "function_agent/agent_service_actor.h"
#include "function_agent/common/utils.h"

namespace functionsystem::function_agent {
struct FunctionAgentStartParam {
    std::string ip;
    std::string localSchedulerAddress;
    std::string nodeID;
    std::string alias;
    std::string modelName;
    std::string agentPort;
    std::string decryptAlgorithm;
    bool s3Enable;
    S3Config s3Config;
    messages::CodePackageThresholds codePackageThresholds;

    uint32_t heartbeatTimeoutMs;
    std::string agentUid;
    std::string localNodeID;
    bool enableSignatureValidation;
    std::string componentName;
};

class FunctionAgentDriver : public ModuleDriver {
public:
    FunctionAgentDriver(const std::string &nodeID, const FunctionAgentStartParam &param);
    ~FunctionAgentDriver() override = default;

    Status Start() override;

    Status Stop() override;

    void Await() override;

    void GracefulShutdown();

private:
    FunctionAgentStartParam startParam_;
    std::shared_ptr<AgentServiceActor> actor_ = nullptr;
    std::shared_ptr<HttpServer> httpServer_;
    std::shared_ptr<HealthyApiRouter> apiRouteRegister_;
};  // class FunctionAgentDriver

}  // namespace functionsystem::function_agent

#endif  // FUNCTION_AGENT_FUNCTION_AGENT_STARTUP_H