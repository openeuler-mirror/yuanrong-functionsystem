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

#include "function_agent_driver.h"

#include <chrono>
#include <memory>

#include "agent_service_actor.h"
#include "async/future.hpp"
#include "common/constants/actor_name.h"
#include "common/logs/logging.h"
#include "common/register/register_helper.h"
#include "function_agent/code_deployer/copy_deployer.h"
#include "function_agent/code_deployer/local_deployer.h"
#include "function_agent/code_deployer/s3_deployer.h"
#include "function_agent/code_deployer/working_dir_deployer.h"

namespace functionsystem::function_agent {

const std::string FUNCTION_AGENT = "function-agent";
const litebus::Duration TIMEOUTMS = 5000;
const uint32_t AGENT_ID_SUFFIX_LENGTH = 6;

FunctionAgentDriver::FunctionAgentDriver(const std::string &nodeID, const FunctionAgentStartParam &param)
{
    startParam_ = param;
    // start agent service actor, agentID should be consistent
    // agentID must contain ip:port. In k8s scenarios, scaler need to parse ip address from id to delete pod.
    std::string agentID = FUNCTION_AGENT_ID_PREFIX + startParam_.ip + "-" + startParam_.agentPort;

    if (!startParam_.agentUid.empty()) {
        agentID = startParam_.agentUid;
    }
    std::string localSchedFuncAgentMgrName = (startParam_.localNodeID == "" ? nodeID : startParam_.localNodeID) +
                                             LOCAL_SCHED_FUNC_AGENT_MGR_ACTOR_NAME_POSTFIX;
    auto localSchedFuncAgentMgrAID = litebus::AID(localSchedFuncAgentMgrName, startParam_.localSchedulerAddress);

    uint32_t receivedPingTimeoutMs = startParam_.heartbeatTimeoutMs;
    function_agent::AgentServiceActor::Config config{ localSchedFuncAgentMgrAID,         startParam_.s3Config,
                                                      startParam_.codePackageThresholds, receivedPingTimeoutMs,
                                                      TENANT_PODIP_IPSET_NAME,           nodeID };
    actor_ = std::make_shared<function_agent::AgentServiceActor>(
        FUNCTION_AGENT_AGENT_SERVICE_ACTOR_NAME, agentID, config, startParam_.alias, param.componentName);
    httpServer_ = std::make_shared<HttpServer>(FUNCTION_AGENT);
    apiRouteRegister_ = std::make_shared<HealthyApiRouter>(startParam_.nodeID, TIMEOUTMS);
    apiRouteRegister_->AddProbe([aid(actor_->GetAID())]() -> litebus::Future<Status> {
        return litebus::Async(aid, &AgentServiceActor::Readiness);
    });
    apiRouteRegister_->Register();
    if (auto registerStatus(httpServer_->RegisterRoute(apiRouteRegister_)); registerStatus != StatusCode::SUCCESS) {
        YRLOG_ERROR("register health check api router failed.");
    }
}

Status FunctionAgentDriver::Start()
{
    auto registerHelper = std::make_shared<RegisterHelper>(FUNCTION_AGENT_AGENT_SERVICE_ACTOR_NAME);
    actor_->SetRegisterHelper(registerHelper);
    // set deployers to actor
    std::shared_ptr<Deployer> s3Deployer = std::make_shared<function_agent::LocalDeployer>();
    if (startParam_.s3Enable) {
        auto config = std::make_shared<S3Config>(startParam_.s3Config);
        s3Deployer = std::make_shared<function_agent::S3Deployer>(config, startParam_.codePackageThresholds,
                                                                  startParam_.enableSignatureValidation);
    } else {
        YRLOG_WARN("s3 is not Enable");
    }
    (void)actor_->SetDeployers(S3_STORAGE_TYPE, s3Deployer);

    auto localDeployer = std::make_shared<function_agent::LocalDeployer>();
    (void)actor_->SetDeployers(LOCAL_STORAGE_TYPE, localDeployer);
    auto copyDeployer = std::make_shared<function_agent::CopyDeployer>();
    (void)actor_->SetDeployers(COPY_STORAGE_TYPE, copyDeployer);
    auto workingDirDeployer = std::make_shared<function_agent::WorkingDirDeployer>();
    (void)actor_->SetDeployers(WORKING_DIR_STORAGE_TYPE, workingDirDeployer);
    auto sharedDirDeployer = std::make_shared<function_agent::SharedDirDeployer>();
    (void)actor_->SetDeployers(SHARED_DIR_STORAGE_TYPE, sharedDirDeployer);

    (void)litebus::Spawn(actor_);
    (void)litebus::Spawn(httpServer_);
    YRLOG_INFO("success to start FunctionAgent");
    return Status::OK();
}

void FunctionAgentDriver::GracefulShutdown()
{
    auto fut = litebus::Async(actor_->GetAID(), &AgentServiceActor::GracefulShutdown);
    (void)fut.Get();
}

Status FunctionAgentDriver::Stop()
{
    litebus::Terminate(actor_->GetAID());
    litebus::Terminate(httpServer_->GetAID());
    return Status::OK();
}

void FunctionAgentDriver::Await()
{
    litebus::Await(actor_->GetAID());
    litebus::Await(httpServer_->GetAID());
}

}  // namespace functionsystem::function_agent