/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#ifndef RUNTIME_MANAGER_EXECUTOR_DOCKER_EXECUTOR_H
#define RUNTIME_MANAGER_EXECUTOR_DOCKER_EXECUTOR_H

#include <nlohmann/json.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/proto/pb/message_pb.h"
#include "common/status/status.h"
#include "config/build.h"
#include "executor.h"
#include "runtime_manager/config/command_builder.h"

namespace functionsystem::runtime_manager {

class DockerExecutor : public Executor {
public:
    explicit DockerExecutor(const std::string &name, const litebus::AID &functionAgentAID);

    ~DockerExecutor() override = default;

    litebus::Future<messages::StartInstanceResponse> StartInstance(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::vector<int> &cardIDs) override;

    litebus::Future<Status> StopInstance(const std::shared_ptr<messages::StopInstanceRequest> &request,
                                         bool oomKilled = false) override;

    litebus::Future<messages::SnapshotRuntimeResponse> SnapshotRuntime(
        const std::shared_ptr<messages::SnapshotRuntimeRequest> &request) override;

    std::map<std::string, messages::RuntimeInstanceInfo> GetRuntimeInstanceInfos() override;

    void UpdatePrestartRuntimePromise(pid_t pid) override {};

    void ClearCapability() override {}

    litebus::Future<messages::UpdateCredResponse> UpdateCredForRuntime(
        const std::shared_ptr<messages::UpdateCredRequest> &request) override;

    litebus::Future<Status> NotifyInstancesDiskUsageExceedLimit(const std::string &description,
                                                                const int limit) override;
    void InitConfig() override;

    bool IsRuntimeActive(const std::string &runtimeID) override;

    litebus::Future<bool> StopAllContainers();

protected:
    void Init() override;

    void Finalize() override;

    void InitPrestartRuntimePool() override {};

    void InitVirtualEnvIdleTimeLimit() override {};

private:
    void ConfigRuntimeRedirectLog(std::string &stdOut, std::string &stdErr, const std::string &runtimeID);

    void BuildRuntimeCommands(runtime::v1::FunctionRuntime *funcRt, const std::vector<std::string> &buildArgs);

    void SetRequestEnvsAndLogsForStart(runtime::v1::StartRequest *req, const Envs &envs, const std::string &runtimeID);

    // Resolve the executable path for the runtime language.
    std::string ResolveExecPath(const std::string &language,
        const messages::RuntimeInstanceInfo &info) const;

    // Build bind mounts (code/layer) from deploy info.
    std::vector<std::string> BuildBindMounts(const Envs &envs,
        const messages::RuntimeInstanceInfo &info) const;

    // Build user port forwarding bindings. Returns false on port allocation failure.
    bool BuildPortBindings(const messages::RuntimeInstanceInfo &info,
        std::map<std::string, std::string> &portBindings);

    // Build CPU/memory resources map from runtime config.
    std::map<std::string, double> BuildResources(const messages::RuntimeInstanceInfo &info) const;

    // Parse a /containers/create response into a container ID, or empty string on failure.
    std::string ParseCreateContainerResponse(const nlohmann::json &resp, const std::string &runtimeID);

    // Build the container command vector (execPath + runtime command args).
    static std::vector<std::string> BuildContainerCommand(const std::string &execPath,
        const runtime::v1::StartRequest &startReq);

    litebus::Future<messages::StartInstanceResponse> StartRuntime(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &language, const Envs &envs,
        const std::vector<std::string> &args, const std::string &port);

    litebus::Future<messages::StartInstanceResponse> StartContainerChain(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &image,
        const nlohmann::json &createBody, const std::string &port);

    litebus::Future<messages::StartInstanceResponse> OnStartRuntime(
        const Status &startStatus, const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::string &port);

    litebus::Future<messages::StartInstanceResponse> OnStartInstanceCompleted(
        const std::string &runtimeID, const messages::StartInstanceResponse &response);

    messages::StartInstanceResponse GenSuccessStartInstanceResponse(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &containerID,
        const std::string &port);

    // Docker Engine API communication (UDS HTTP, same pattern as SupervisorExecutor)
    int ConnectDockerSocket();
    std::string BuildDockerHttpRequest(const std::string &method, const std::string &path, const std::string &body);
    void ParseDockerResponse(litebus::Promise<nlohmann::json> promise, std::string response);
    litebus::Future<nlohmann::json> SendRequestToDocker(const std::string &method, const std::string &path,
                                                         const nlohmann::json &body = nlohmann::json::object());

    // Container lifecycle management
    litebus::Future<Status> StartContainer(const std::string &runtimeID, const std::string &containerID);
    litebus::Future<Status> TerminateContainer(const std::string &runtimeID, const std::string &containerID,
                                               bool force);
    litebus::Future<Status> StopContainer(const std::string &containerID, int64_t timeout);
    litebus::Future<Status> RemoveContainer(const std::string &containerID, bool force);

    // Release all per-runtime state (container/port maps) and ports.
    void CleanupRuntimeState(const std::string &runtimeID);

    // Image management
    litebus::Future<Status> EnsureImageExists(const std::string &image);
    litebus::Future<Status> PullImage(const std::string &image);
    std::string GetRuntimeImage(const std::shared_ptr<messages::StartInstanceRequest> &request);

    // Build Docker API request body
    struct ContainerCreateSpec {
        std::string image;
        std::vector<std::string> command;
        std::map<std::string, std::string> envs;
        std::vector<std::string> bindMounts;
        std::map<std::string, std::string> portBindings;  // "containerPort/proto" -> hostPort
        std::map<std::string, double> resources;          // "cpu"/"memory" in milli-cores / MB
        std::string workingDir;                           // container working directory (-w)
    };
    nlohmann::json BuildCreateContainerRequest(const ContainerCreateSpec &spec);
    nlohmann::json BuildHostConfig(const ContainerCreateSpec &spec) const;

    std::string GetDockerApiPrefix() const;

    std::map<std::string, messages::RuntimeInstanceInfo> runtimeInstanceInfoMap_;
    std::map<std::string, std::string> runtime2containerID_;
    std::unordered_map<std::string, litebus::Future<messages::StartInstanceResponse>> inProgressStarts_;
    std::unordered_set<std::string> pendingDeletes_;
    std::map<std::string, std::string> runtime2portMappings_;  // runtimeID -> portMappings JSON
    litebus::AID functionAgentAID_;
    CommandBuilder cmdBuilder_{ false };

    std::string dockerSocketPath_;
    std::string dockerApiVersion_;
};

class DockerExecutorProxy : public ExecutorProxy {
public:
    explicit DockerExecutorProxy(const std::shared_ptr<DockerExecutor> &executor) : ExecutorProxy(executor) {};

    ~DockerExecutorProxy() override = default;

    litebus::Future<::messages::StartInstanceResponse> StartInstance(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::vector<int> &cardIDs) override;

    litebus::Future<Status> StopInstance(const std::shared_ptr<messages::StopInstanceRequest> &request,
                                         bool oomKilled = false) override;

    litebus::Future<messages::SnapshotRuntimeResponse> SnapshotRuntime(
        const std::shared_ptr<messages::SnapshotRuntimeRequest> &request) override;

    litebus::Future<std::map<std::string, messages::RuntimeInstanceInfo>> GetRuntimeInstanceInfos() override;

    void UpdatePrestartRuntimePromise(pid_t pid) override {};

    void ClearCapability() override {}

    litebus::Future<bool> GracefulShutdown() override
    {
        return litebus::Async(executor_->GetAID(), &DockerExecutor::StopAllContainers);
    }
};

}  // namespace functionsystem::runtime_manager

#endif
