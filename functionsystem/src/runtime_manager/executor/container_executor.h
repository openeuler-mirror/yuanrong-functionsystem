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
#include <vector>

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
#include "common/file_storage/file_storage_client.h"
#include "runtime_manager/ckpt/ckpt_file_manager.h"

namespace functionsystem::runtime_manager {

class ContainerExecutor : public Executor {
public:
    explicit ContainerExecutor(const std::string &name, const litebus::AID &functionAgentAID);

    ~ContainerExecutor() override = default;

    void SetHealthCheckClient(const std::shared_ptr<HealthCheck> &healthCheck)
    {
        healthCheckClient_ = healthCheck;
    }

    litebus::Future<messages::StartInstanceResponse> StartInstance(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::vector<int> &cardIDs) override;

    litebus::Future<Status> StopInstance(
        const std::shared_ptr<messages::StopInstanceRequest> &request, bool oomKilled = false) override;

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

    /**
     * Port forward configuration parsed from network JSON.
     */
    struct PortForwardConfig {
        uint32_t containerPort;  // Container port to forward
        std::string protocol;    // "TCP" or "UDP"
    };

    /**
     * Parse the list of port forward configs from a network JSON string.
     * Expected format: {"portForwardings": [{"port": 8080, "protocol": "TCP"}, ...]}
     *
     * @param networkJson JSON string from deployOptions["network"].
     * @return Parsed port forward configs, empty on error.
     */
    static std::vector<PortForwardConfig> ParseForwardPorts(const std::string &networkJson);

protected:
    void Init() override;

    void Finalize() override;

    // todo: maybe lwy for cold start fork
    void InitPrestartRuntimePool() override {};

    void InitVirtualEnvIdleTimeLimit() override {};

private:
    void Sync();
    litebus::Future<Status> DoSyncRegistered();
    void OnSyncRegistered();
    void ReconnectContainerd();
    void OnReconnectContainerd();
    void CheckConnectivity();
    void ConfigRuntimeRedirectLog(std::string &stdOut, std::string &stdErr, const std::string &runtimeID);
    Status BuildRootfs(const std::shared_ptr<messages::StartInstanceRequest> &request,
                       std::shared_ptr<runtime::v1::StartRequest> &start);

    // Helper functions to reduce code duplication
    void BuildRuntimeCommands(runtime::v1::FunctionRuntime *funcRt,
                            const std::shared_ptr<messages::StartInstanceRequest> &request,
                            const std::string &execPath, const std::vector<std::string> &buildArgs);

    void BuildRuntimeCommandsForRestore(runtime::v1::RestoreRequest *req,
                                       const std::shared_ptr<messages::StartInstanceRequest> &request,
                                       const std::string &execPath, const std::vector<std::string> &buildArgs);

    void SetRequestResources(google::protobuf::Map<std::string, double> *resourcesMap,
                           const std::shared_ptr<messages::StartInstanceRequest> &request);

    void SetRequestResourcesForStart(runtime::v1::StartRequest *req,
                                    const std::shared_ptr<messages::StartInstanceRequest> &request);

    void SetRequestResourcesForRestore(runtime::v1::RestoreRequest *req,
                                      const std::shared_ptr<messages::StartInstanceRequest> &request);

    void SetRequestEnvsAndLogsForStart(runtime::v1::StartRequest *req,
                                      const std::shared_ptr<messages::StartInstanceRequest> &request,
                                      const Envs &envs, const std::string &runtimeID);

    void SetRequestEnvsAndLogsForRestore(runtime::v1::RestoreRequest *req,
                                        const std::shared_ptr<messages::StartInstanceRequest> &request,
                                        const Envs &envs, const std::string &runtimeID);

    void SetRequestExtraConfigForStart(runtime::v1::StartRequest *req,
                                      const std::shared_ptr<messages::StartInstanceRequest> &request);

    void SetRequestExtraConfigForRestore(runtime::v1::RestoreRequest *req,
                                        const std::shared_ptr<messages::StartInstanceRequest> &request);

    litebus::Future<runtime::v1::StartResponse> StartByRuntimeID(
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::map<std::string, std::string> startRuntimeParams, const std::vector<std::string> &buildArgs,
        const Envs &envs);

    litebus::Future<Status> StopInstanceByRuntimeID(const std::string &runtimeID, const std::string &requestID,
                                                    bool oomKilled = false);

    litebus::Future<messages::StartInstanceResponse> WarmUp(
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::map<std::string, std::string> startRuntimeParams, const std::vector<std::string> &buildArgs,
        const Envs &envs);

    litebus::Future<messages::StartInstanceResponse> StartBySnapshot(
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::map<std::string, std::string> startRuntimeParams, const std::vector<std::string> &buildArgs,
        const Envs &envs);

    litebus::Future<messages::StartInstanceResponse> OnDownloadCheckpointForRestore(
        const std::string &checkpointPath,
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::map<std::string, std::string> startRuntimeParams, const std::vector<std::string> &buildArgs,
        const Envs &envs);

    litebus::Future<messages::StartInstanceResponse> OnAddReferenceForRestore(
        const Status &refStatus,
        const std::string &checkpointPath,
        const std::string &checkpointID,
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::map<std::string, std::string> startRuntimeParams,
        const std::vector<std::string> &buildArgs,
        const Envs &envs);

    litebus::Future<messages::StartInstanceResponse> OnRestoreCompleted(
        const runtime::v1::RestoreResponse &response,
        const std::shared_ptr<messages::StartInstanceRequest> &request);

    litebus::Future<Status> UnRegisteredWarmUped(const std::string &runtimeID, const std::string &requestID);

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

    litebus::Future<runtime::v1::CheckpointResponse> DoCheckpoint(
        const std::shared_ptr<runtime::v1::CheckpointRequest> &req);

    litebus::Future<runtime::v1::RestoreResponse> DoRestore(
        const std::shared_ptr<runtime::v1::RestoreRequest> &req);

    litebus::Future<messages::SnapshotRuntimeResponse> OnCheckpointCompleted(
        const runtime::v1::CheckpointResponse &ckptResponse,
        const std::string &requestID,
        const std::string &checkpointID,
        const std::string &checkpointPath,
        const std::string &runtimeID,
        int32_t ttl);

    litebus::Future<messages::SnapshotRuntimeResponse> OnRegisterCheckpoint(
        const std::string &storageUrl,
        messages::SnapshotRuntimeResponse response,
        const std::string &requestID,
        const std::string &checkpointID,
        const std::string &runtimeID,
        int32_t ttl);

    litebus::Future<runtime::v1::NormalResponse> DoRegisterToWarmUp(
        const std::shared_ptr<runtime::v1::RegisterRequest> &reg);

    litebus::Future<messages::StartInstanceResponse> OnRegisterToWarmUp(
        const runtime::v1::NormalResponse &response,
        const std::shared_ptr<messages::StartInstanceRequest> &request,
         const std::shared_ptr<runtime::v1::RegisterRequest> &reg);

    litebus::Future<runtime::v1::NormalResponse> DoUnregisterWarmUped(
        const std::shared_ptr<runtime::v1::UnregisterRequest> &unReg);

    litebus::Future<Status> OnUnregisteredWarmUped(const std::shared_ptr<runtime::v1::UnregisterRequest> &unReg,
                                                   const runtime::v1::NormalResponse &response);

    litebus::Future<runtime::v1::GetRegisteredResponse> GetRegisteredWarmUped();

    void OnGetRegisteredWarmUped(const litebus::Future<runtime::v1::GetRegisteredResponse> &registered);

    litebus::Future<messages::StartInstanceResponse> OnStartInstanceCompleted(
        const std::string &runtimeID, const messages::StartInstanceResponse &response);

    std::map<std::string, messages::RuntimeInstanceInfo> runtimeInstanceInfoMap_;
    std::map<std::string, std::string> runtime2containerID_;
    std::unordered_map<std::string, litebus::Future<messages::StartInstanceResponse>> inProgressStarts_;
    std::unordered_set<std::string> pendingDeletes_;
    std::unordered_map<std::string, std::string> runtime2checkpointID_;  // runtimeID -> checkpointID
    std::unordered_map<std::string, std::string> runtime2portMappings_;  // runtimeID -> portMappings JSON
    std::unordered_set<std::string> innerOomKilledruntimes_;
    litebus::AID functionAgentAID_;
    std::shared_ptr<GrpcClient<runtime::v1::RuntimeLauncher>> containerd_ {nullptr};
    std::shared_ptr<HealthCheck> healthCheckClient_;
    std::shared_ptr<CkptFileManager> ckptFileManager_;
    CommandBuilder cmdBuilder_{false};
    bool reconnecting_ = false;
    bool synced_ = false;
    std::unordered_map<std::string, runtime::v1::FunctionRuntime> registeredWarmUp_;
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

    void ClearCapability() override {}

    litebus::Future<bool> GracefulShutdown() override
    {
        return litebus::Async(executor_->GetAID(), &ContainerExecutor::StopAllContainers);
    }
};

}  // namespace functionsystem::runtime_manager


#endif