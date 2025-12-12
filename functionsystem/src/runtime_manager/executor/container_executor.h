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

#ifndef RUNTIME_MANAGER_EXECUTOR_EXECUTOR_ACTOR_CONTAINER_EXECUTOR_H
#define RUNTIME_MANAGER_EXECUTOR_EXECUTOR_ACTOR_CONTAINER_EXECUTOR_H

#include <sys/stat.h>

#include <chrono>
#include <memory>
#include <shared_mutex>

#include "async/defer.hpp"
#include "common/metrics/metrics_adapter.h"
#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "common/utils/files.h"
#include "config/build.h"
#include "executor.h"
#include "runtime_manager/config/command_builder.h"
#include "healthcheck/health_check.h"

#include "common/rpc/client/grpc_client.h"
#include "common/proto/pb/posix/runtime_launcher_interface.grpc.pb.h"

namespace functionsystem::runtime_manager {

class ContainerExecutor : public Executor {
public:
    explicit ContainerExecutor(const std::string &name, const litebus::AID &functionAgentAID);

    virtual ~ContainerExecutor() override = default;

    void SetHealthCheckClient(const std::shared_ptr<HealthCheck> &healthCheck) 
    {
        healthCheckClient_ = healthCheck;
    }

    litebus::Future<messages::StartInstanceResponse> StartInstance(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::vector<int> &cardIDs) override;

    litebus::Future<Status> StopInstance(const std::shared_ptr<messages::StopInstanceRequest> &request, bool oomKilled = false) override;

    std::map<std::string, messages::RuntimeInstanceInfo> GetRuntimeInstanceInfos() override;

    void UpdatePrestartRuntimePromise(pid_t pid) override {};

    void ClearCapability() override {}

    litebus::Future<messages::UpdateCredResponse> UpdateCredForRuntime(
        const std::shared_ptr<messages::UpdateCredRequest> &request) override;

    litebus::Future<Status> NotifyInstancesDiskUsageExceedLimit(const std::string &description,
                                                                const int limit) override;
    void InitConfig() override;

    litebus::Future<bool> StopAllContainers();

protected:
    void Init() override;

    void Finalize() override;

    // todo: maybe lwy for cold start fork
    void InitPrestartRuntimePool() override {};

    void InitVirtualEnvIdleTimeLimit() override {};

private:
    void ConfigRuntimeRedirectLog(std::string &stdOut, std::string &stdErr, const std::string &runtimeID);

    litebus::Future<runtime::v1::StartResponse> StartByRuntimeID(
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::map<std::string, std::string> startRuntimeParams, const std::vector<std::string> &buildArgs,
        const Envs &envs);

    litebus::Future<Status> StopInstanceByRuntimeID(const std::string &runtimeID, const std::string &requestID,
                                                    bool oomKilled = false);
    litebus::Future<Status> TerminateContainer(const std::string &runtimeID, const std::string &requestID,
                                               const std::string &containerID, bool force);
    litebus::Future<Status> OnDeleteContainer(const std::string &instanceID, const std::string &runtimeID,
                                              const std::string &requestID, const std::string &containerID);
    messages::StartInstanceResponse GenSuccessStartInstanceResponse(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &containerID);

    litebus::Future<messages::StartInstanceResponse> StartRuntime(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &language,
        const std::string &port, const Envs &envs, const std::vector<std::string> &args);

    litebus::Future<messages::StartInstanceResponse> OnStartRuntime(
        const runtime::v1::StartResponse &response,
        const std::shared_ptr<messages::StartInstanceRequest> &request);

    void ReportInfo(const std::string &instanceID, const std::string runtimeID, const std::string &containerID,
                    const functionsystem::metrics::MeterTitle &title);

    litebus::Future<runtime::v1::StartResponse> DoStartContainer(
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::shared_ptr<runtime::v1::StartRequest> &start);
    litebus::Future<runtime::v1::DeleteResponse> DoDeleteContainer(
        const std::string &instanceID, const std::string &runtimeID, const std::string &requestID,
        const std::shared_ptr<runtime::v1::DeleteRequest> &req);
    litebus::Future<runtime::v1::WaitResponse> DoWaitContainer(
        const std::shared_ptr<runtime::v1::WaitRequest> &req);

    std::map<std::string, messages::RuntimeInstanceInfo> runtimeInstanceInfoMap_;
    std::map<std::string, std::string> runtime2containerID_;
    std::unordered_set<std::string> innerOomKilledruntimes_;
    litebus::AID functionAgentAID_;
    std::unique_ptr<GrpcClient<runtime::v1::RuntimeLauncher>> containerd_ {nullptr};
    std::shared_ptr<HealthCheck> healthCheckClient_;
    CommandBuilder cmdBuilder_ = {false};
};

class ContainerExecutorProxy : public ExecutorProxy {
public:
    explicit ContainerExecutorProxy(const std::shared_ptr<ContainerExecutor> &executor) : ExecutorProxy(executor){};

    ~ContainerExecutorProxy() override = default;

    /**
     * Start Instance when receive message from function agent.
     *
     * @param request Include start instance arguments.
     * @return response Include start instance result arguments.
     */
    litebus::Future<::messages::StartInstanceResponse> StartInstance(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::vector<int> &cardIDs) override;

    /**
     * Stop Instance when receive message from function agent.
     *
     * @param request Include stop instance arguments.
     * @param oomKilled is inner oom killed by runtime-manager
     * @return response Include stop instance result arguments.
     */
    litebus::Future<Status> StopInstance(const std::shared_ptr<messages::StopInstanceRequest> &request,
                                         bool oomKilled = false) override;

    /**
     * Get runtime instance infos.
     *
     * @return Runtime infos.
     */
    litebus::Future<std::map<std::string, messages::RuntimeInstanceInfo>> GetRuntimeInstanceInfos() override;

    void UpdatePrestartRuntimePromise(pid_t pid) override {};

    void ClearCapability() override {}

    litebus::Future<bool> GracefulShutdown() override
    {
        return litebus::Async(executor_->GetAID(), &ContainerExecutor::StopAllContainers);
    }
};

}  // namespace functionsystem::runtime_manager


#endif