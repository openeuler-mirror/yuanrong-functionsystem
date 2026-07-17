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

#ifndef RUNTIME_MANAGER_EXECUTOR_EXECUTOR_ACTOR_SANDBOX_EXECUTOR_H
#define RUNTIME_MANAGER_EXECUTOR_EXECUTOR_ACTOR_SANDBOX_EXECUTOR_H

#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix/runtime_launcher_interface.grpc.pb.h"
#include "common/status/status.h"
#include "config/build.h"
#include "executor.h"
#include "runtime_manager/config/command_builder.h"

namespace functionsystem::runtime_manager {

class SupervisorExecutor : public Executor {
public:
    explicit SupervisorExecutor(const std::string &name, const litebus::AID &functionAgentAID);

    ~SupervisorExecutor() override = default;

    litebus::Future<messages::StartInstanceResponse> StartInstance(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::vector<int> &cardIDs) override;

    litebus::Future<Status> StopInstance(const std::shared_ptr<messages::StopInstanceRequest> &request,
                                         bool oomKilled = false) override;

    litebus::Future<messages::SnapshotRuntimeResponse> SnapshotRuntime(
        const std::shared_ptr<messages::SnapshotRuntimeRequest> &request) override;

    std::map<std::string, messages::RuntimeInstanceInfo> GetRuntimeInstanceInfos() override;

    void UpdatePrestartRuntimePromise(pid_t pid) override {};

    void ClearCapability() override
    {
    }

    litebus::Future<messages::UpdateCredResponse> UpdateCredForRuntime(
        const std::shared_ptr<messages::UpdateCredRequest> &request) override;

    litebus::Future<Status> NotifyInstancesDiskUsageExceedLimit(const std::string &description,
                                                                const int limit) override;
    void InitConfig() override;

    bool IsRuntimeActive(const std::string &runtimeID) override;

    litebus::Future<bool> StopAllSandboxes();

protected:
    void Init() override;

    void Finalize() override;

    void InitPrestartRuntimePool() override {};

    void InitVirtualEnvIdleTimeLimit() override {};

private:
    void ConfigRuntimeRedirectLog(std::string &stdOut, std::string &stdErr, const std::string &runtimeID,
                                 const std::string &hostUser = "");

    // Helper functions to reduce code duplication
    void BuildRuntimeCommands(runtime::v1::FunctionRuntime *funcRt, const std::vector<std::string> &buildArgs);

    void SetRequestEnvsAndLogsForStart(runtime::v1::StartRequest *req, const Envs &envs, const std::string &runtimeID,
                                       const std::string &hostUser);

    litebus::Future<runtime::v1::StartResponse> StartByRuntimeID(
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::map<std::string, std::string> startRuntimeParams, const std::vector<std::string> &buildArgs,
        const Envs &envs);

    litebus::Future<Status> TerminateSandbox(const std::string &runtimeID, const std::string &sandboxID);
    messages::StartInstanceResponse GenSuccessStartInstanceResponse(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &sandboxID);

    litebus::Future<messages::StartInstanceResponse> StartRuntime(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &language, const Envs &envs,
        const std::vector<std::string> &args);

    litebus::Future<std::string> CreateSandbox(const std::string &runtimeID, const std::string &hostUser = "");
    nlohmann::json BuildCommand(const ::std::shared_ptr<runtime::v1::StartRequest> &start);
    // POSIX single-quote shell-escape a single argv token / redirect path so it is passed
    // through sh -c verbatim without any metacharacter interpretation (no injection).
    static std::string ShellQuote(const std::string &token);
    litebus::Future<runtime::v1::DeleteResponse> DoDeleteSandbox(
        const std::shared_ptr<runtime::v1::DeleteRequest> &req);

    litebus::Future<runtime::v1::StartResponse> ExecInSandbox(const std::string &runtimeID,
                                                              const ::std::shared_ptr<runtime::v1::StartRequest> &start,
                                                              const std::string &sandboxId);
    litebus::Future<messages::StartInstanceResponse> OnStartInstanceCompleted(
        const std::string &runtimeID, const messages::StartInstanceResponse &response);

    std::map<std::string, messages::RuntimeInstanceInfo> runtimeInstanceInfoMap_;
    std::map<std::string, std::string> runtime2sandboxID_;
    std::unordered_map<std::string, litebus::Future<messages::StartInstanceResponse>> inProgressStarts_;
    std::unordered_set<std::string> pendingDeletes_;
    std::unordered_map<std::string, std::string> runtime2portMappings_;  // runtimeID -> portMappings JSON
    litebus::AID functionAgentAID_;
    CommandBuilder cmdBuilder_{ false };
    std::string pkgType_;

    void ParseResponse(litebus::Promise<nlohmann::json> promise, std::string response);
    litebus::Future<nlohmann::json> SendRequestToSupervisor(const std::string &method, const std::string &path,
                                                            const nlohmann::json &body = nlohmann::json::object());
    int ConnectUdsSocket(const std::string &socketPath);
    std::string BuildUdsHttpRequest(const std::string &method, const std::string &path, const std::string &body);
    bool SendUdsRequest(int fd, const std::string &httpRequest);
    bool ReceiveUdsResponse(int fd, std::string &response);
};

class SupervisorExecutorProxy : public ExecutorProxy {
public:
    explicit SupervisorExecutorProxy(const std::shared_ptr<SupervisorExecutor> &executor) : ExecutorProxy(executor) {};

    ~SupervisorExecutorProxy() override = default;

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
     * Snapshot Runtime when receive message from function agent.
     *
     * @param request Include snapshot arguments.
     * @return response Include snapshot result with checkpoint info.
     */
    litebus::Future<messages::SnapshotRuntimeResponse> SnapshotRuntime(
        const std::shared_ptr<messages::SnapshotRuntimeRequest> &request) override;

    /**
     * Get runtime instance infos.
     *
     * @return Runtime infos.
     */
    litebus::Future<std::map<std::string, messages::RuntimeInstanceInfo>> GetRuntimeInstanceInfos() override;

    void UpdatePrestartRuntimePromise(pid_t pid) override {};

    void ClearCapability() override
    {
    }

    litebus::Future<bool> GracefulShutdown() override
    {
        return litebus::Async(executor_->GetAID(), &SupervisorExecutor::StopAllSandboxes);
    }
};

}  // namespace functionsystem::runtime_manager

#endif
