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

#ifndef RUNTIME_MANAGER_EXECUTOR_SANDBOX_SANDBOX_EXECUTOR_H
#define RUNTIME_MANAGER_EXECUTOR_SANDBOX_SANDBOX_EXECUTOR_H

#include <chrono>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "async/defer.hpp"
#include "common/metrics/metrics_adapter.h"
#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix/runtime_launcher_interface.grpc.pb.h"
#include "common/rpc/client/grpc_client.h"
#include "common/status/status.h"
#include "healthcheck/health_check.h"
#include "runtime_manager/ckpt/ckpt_file_manager.h"
#include "runtime_manager/config/command_builder.h"
#include "runtime_manager/executor/executor.h"
#include "checkpoint_orchestrator.h"
#include "runtime_state_manager.h"
#include "sandbox_request_builder.h"

namespace functionsystem::runtime_manager {

/**
 * SandboxStartGuard — RAII guard for the in-progress start lifecycle.
 *
 * On construction:  registers an in-progress Future in RuntimeStateManager.
 * On destruction:   if Commit() was never called, calls Unregister() to roll
 *                   back all state — ensuring no map entries leak on failure.
 * On Commit():      transitions state to "active" (MarkStartDone).
 */
class SandboxStartGuard {
public:
    SandboxStartGuard(RuntimeStateManager &mgr, std::string runtimeID,
                      litebus::Future<messages::StartInstanceResponse> future)
        : mgr_(mgr), runtimeID_(std::move(runtimeID))
    {
        mgr_.MarkStartInProgress(runtimeID_, std::move(future));
    }

    ~SandboxStartGuard()
    {
        if (!committed_) {
            // Start path did not complete successfully — remove all residual state.
            mgr_.Unregister(runtimeID_);
        }
    }

    // Call when the start path has completed successfully.
    void Commit()
    {
        committed_ = true;
        mgr_.MarkStartDone(runtimeID_);
    }

    SandboxStartGuard(const SandboxStartGuard &) = delete;
    SandboxStartGuard &operator=(const SandboxStartGuard &) = delete;

private:
    RuntimeStateManager &mgr_;
    std::string runtimeID_;
    bool committed_ = false;
};

/**
 * SandboxExecutor — thin orchestration layer for sandbox lifecycle.
 *
 * Responsibilities (only):
 *   - Route StartInstance to the correct path (normal / warm-up / restore).
 *   - Chain litebus::Future callbacks for each path.
 *   - Delegate gRPC calls to Do*() wrappers.
 *   - Delegate state mutations to RuntimeStateManager.
 *
 * Does NOT:
 *   - Build gRPC proto fields (→ SandboxRequestBuilder, T5).
 *   - Manage checkpoint storage (→ CheckpointOrchestrator, T6).
 *   - Contain any language-specific command logic.
 */
class SandboxExecutor : public Executor {
public:
    static constexpr uint32_t kDefaultOrphanGracePeriodSec = 180;
    // After a failed orphan-container Delete RPC, re-arm the timer so that the
    // next reconcile cycle re-attempts the delete instead of waiting a full
    // grace period again.  Set to one reconcile interval (60 s) plus a small
    // buffer so the re-attempt fires on the very next reconcile tick.
    static constexpr uint32_t kOrphanDeleteRetryIntervalSec = 70;

    enum class SandboxLifecycleStatus : int32_t {
        CREATING = 1,
        RUNNING = 2,
        COMPLETED = 3,
        ABNORMAL = 4,
    };

    SandboxExecutor(const std::string &name, const litebus::AID &functionAgentAID,
                    const std::string &checkpointDir = {});

    ~SandboxExecutor() override = default;

    void SetHealthCheckClient(const std::shared_ptr<HealthCheck> &healthCheck)
    {
        healthCheckClient_ = healthCheck;
    }

    // Executor interface

    litebus::Future<messages::StartInstanceResponse> StartInstance(
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::vector<int> &cardIDs) override;

    litebus::Future<Status> StopInstance(const std::shared_ptr<messages::StopInstanceRequest> &request,
                                         bool oomKilled = false) override;

    litebus::Future<messages::SnapshotRuntimeResponse> SnapshotRuntime(
        const std::shared_ptr<messages::SnapshotRuntimeRequest> &request) override;

    std::map<std::string, messages::RuntimeInstanceInfo> GetRuntimeInstanceInfos() override;

    bool IsRuntimeActive(const std::string &runtimeID) override;

    std::shared_ptr<litebus::Exec> GetExecByRuntimeID(const std::string &runtimeID) override;
    void ClearCapability() override {}

    void UpdatePrestartRuntimePromise(pid_t /* pid */) override {}

    litebus::Future<messages::UpdateCredResponse> UpdateCredForRuntime(
        const std::shared_ptr<messages::UpdateCredRequest> &request) override;

    litebus::Future<Status> NotifyInstancesDiskUsageExceedLimit(const std::string &description,
                                                                const int limit) override;

    litebus::Future<bool> StopAllSandboxes();

    litebus::Future<messages::ReconcileRuntimesResponse> ReconcileRuntimes(
        const std::shared_ptr<messages::ReconcileRuntimesRequest> &request);

    void InitConfig() override;

    struct PortForwardConfig {
        uint32_t containerPort = 0;
        std::string protocol   = "tcp";
    };

    static std::vector<PortForwardConfig> ParseForwardPorts(const std::string &networkJson);

    // Returns true when a Wait RPC status indicates sandboxd transport-layer failure
    // and the call should be retried rather than treated as a sandbox exit.
    static bool IsRetryableWaitError(const Status &status);

protected:
    void Init() override;
    void Finalize() override;
    void InitPrestartRuntimePool() override {}
    void InitVirtualEnvIdleTimeLimit() override {}

private:
    struct StartContext {
        std::shared_ptr<messages::StartInstanceRequest> request;
        CommandArgs cmdArgs;
        std::string port;
        Envs envs;
        std::vector<int> cardIDs;
        std::shared_ptr<SandboxStartGuard> guard;
    };

    struct RestoreContext {
        std::string checkpointPath;
        StartContext start;
    };

    struct OrphanCleanupContext {
        std::string requestID;
        std::shared_ptr<runtime::v1::ListContainersResponse> listResp;
        const std::unordered_set<std::string> *expectedIDs{ nullptr };
        std::chrono::steady_clock::time_point now;
        messages::ReconcileRuntimesResponse *response{ nullptr };
        int32_t *orphansCleaned{ nullptr };
        std::unordered_set<std::string> *actualRunningIDs{ nullptr };
    };

    // Start routing (three paths)

    void PreparePortMappings(SandboxStartParams *params);
    Status BuildStartCommandArgs(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                 const std::string &port, CommandArgs *cmdArgs);

    litebus::Future<messages::StartInstanceResponse> StartNormal(const StartContext &context);

    litebus::Future<messages::StartInstanceResponse> OnStartDone(
        const runtime::v1::StartResponse &response,
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        std::shared_ptr<SandboxStartGuard> guard);

    litebus::Future<messages::StartInstanceResponse> OnStartCompleted(
        const std::string &runtimeID, const messages::StartInstanceResponse &response);

    litebus::Future<messages::StartInstanceResponse> StartWarmUp(const StartContext &context);

    litebus::Future<messages::StartInstanceResponse> OnWarmUpRegistered(
        const runtime::v1::NormalResponse &response,
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::shared_ptr<runtime::v1::RegisterRequest> &reg,
        std::shared_ptr<SandboxStartGuard> guard);

    litebus::Future<messages::StartInstanceResponse> StartBySnapshot(const StartContext &context);

    litebus::Future<messages::StartInstanceResponse> OnCheckpointDownloaded(
        const std::string &checkpointPath,
        const StartContext &context);

    litebus::Future<messages::StartInstanceResponse> OnCheckpointRefAdded(
        const Status &refStatus,
        const RestoreContext &context);

    litebus::Future<messages::StartInstanceResponse> OnRestoreDone(
        const runtime::v1::StartResponse &response,
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        std::shared_ptr<SandboxStartGuard> guard);

    // Stop helpers

    litebus::Future<Status> StopSandbox(const std::string &runtimeID, const std::string &requestID,
                                        bool oomKilled);
    litebus::Future<Status> TerminateSandbox(const std::string &runtimeID, const std::string &requestID,
                                             const std::string &sandboxID, bool force);
    litebus::Future<Status> OnDeleteDone(const std::string &runtimeID, const std::string &requestID,
                                         const std::string &sandboxID,
                                         const runtime::v1::DeleteResponse &response);
    litebus::Future<Status> UnregisterWarmUp(const std::string &runtimeID, const std::string &requestID);
    litebus::Future<Status> OnWarmUpUnregistered(
        const std::shared_ptr<runtime::v1::UnregisterRequest> &unReg,
        const runtime::v1::NormalResponse &response);

    // gRPC call wrappers (thin, no business logic)

    litebus::Future<runtime::v1::ListContainersResponse> DoList();
    litebus::Future<runtime::v1::StartResponse> DoStart(
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::shared_ptr<runtime::v1::StartRequest> &startReq);

    litebus::Future<runtime::v1::DeleteResponse> DoDelete(
        const std::string &instanceID, const std::string &runtimeID, const std::string &requestID,
        const std::shared_ptr<runtime::v1::DeleteRequest> &req);

    litebus::Future<runtime::v1::NormalResponse> DoRegisterWarmUp(
        const std::shared_ptr<runtime::v1::RegisterRequest> &req);

    litebus::Future<runtime::v1::NormalResponse> DoUnregisterWarmUp(
        const std::shared_ptr<runtime::v1::UnregisterRequest> &req);

    void DoWait(const std::string &sandboxID, const std::string &runtimeID);

    void RestoreWait(const std::string &sandboxID);

    // Wait retry on sandboxd disconnection
    void DoWaitWithRetry(const std::string &sandboxID, const std::string &runtimeID, int retryCount);

    void CollectSandboxStats(const std::string &runtimeID, const std::string &sandboxID);
    void ScheduleSandboxStatsCollection(const std::string &runtimeID, const std::string &sandboxID);
    void ScheduleRunningStatusHeartbeat(const std::string &runtimeID);
    void ReportRunningStatusHeartbeat(const std::string &runtimeID);
    litebus::Future<Status> OnSandboxStatsCollected(const std::string &runtimeID, const std::string &sandboxID,
                                                    const Status &status,
                                                    const runtime::v1::StatsResponse &response,
                                                    std::chrono::steady_clock::time_point collectedAt);

    litebus::Future<Status> CleanupSandboxAfterMaxRetries(const std::string &runtimeID,
                                                          const std::string &sandboxID);

    litebus::Future<Status> OnWaitDone(
        const std::string &runtimeID, const runtime::v1::WaitResponse &response);

    static void StartSandboxCreateSpan(const std::shared_ptr<messages::StartInstanceRequest> &request);
    static void StopSandboxCreateSpan(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                      const runtime::v1::StartResponse &response);

    void ReportSandboxLifecycleStatus(const messages::RuntimeInstanceInfo &info, const std::string &runtimeID,
                                      SandboxLifecycleStatus lifecycleStatus);
    void ReportSandboxRequestedResources(const messages::RuntimeInstanceInfo &info, const std::string &runtimeID);
    void ReportSandboxUsageMetrics(const messages::RuntimeInstanceInfo &info, const std::string &runtimeID,
                                   const runtime::v1::StatsResponse &response,
                                   std::chrono::steady_clock::time_point collectedAt);
    void ClearSandboxMetricsState(const std::string &runtimeID);

    // Connectivity

    void Sync();
    void ReconnectContainerd();
    void OnReconnectContainerd();
    void CheckConnectivity();

    messages::StartInstanceResponse MakeSuccessStartResponse(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &sandboxID);

    void ReportMetrics(const std::string &instanceID, const std::string &runtimeID,
                       const std::string &sandboxID, const functionsystem::metrics::MeterTitle &title);

    void DoReportMetrics(const std::string &instanceID, const std::string &runtimeID,
                         const std::string &sandboxID, const functionsystem::metrics::MeterTitle &title);

    // Reconciliation helpers

    void WaitAndReconcile(
        const std::shared_ptr<messages::ReconcileRuntimesRequest> &request, int32_t retryCount,
        const std::shared_ptr<litebus::Promise<messages::ReconcileRuntimesResponse>> &promise);

    messages::ReconcileRuntimesResponse OnReconcileRuntimes(
        const std::shared_ptr<messages::ReconcileRuntimesRequest> &request,
        const std::shared_ptr<runtime::v1::ListContainersResponse> &listResp);

    void CleanupExitedContainers(const std::string &requestID,
                                 const std::shared_ptr<runtime::v1::ListContainersResponse> &listResp,
                                 messages::ReconcileRuntimesResponse *response,
                                 int32_t *orphansCleaned);

    void CleanupOrphanContainers(const OrphanCleanupContext &context);

    void AddMissingAndConfirmedEntries(
        const std::shared_ptr<messages::ReconcileRuntimesRequest> &request,
        const std::unordered_set<std::string> &actualRunningIDs,
        messages::ReconcileRuntimesResponse *response);

    void PurgeOrphanTracking(const std::unordered_set<std::string> &actualRunningIDs);
    void CleanupLocalRuntimeStateForOrphan(const std::string &requestID, const std::string &containerID);
    void DeleteContainerAsync(const std::string &containerID);
    Status OnDeleteContainerComplete(const std::string &containerID,
                                     litebus::Try<runtime::v1::DeleteResponse> rsp);

    // State

    RuntimeStateManager stateManager_;
    CommandBuilder cmdBuilder_{false};
    std::shared_ptr<GrpcClient<runtime::v1::RuntimeLauncher>> containerd_{nullptr};
    std::shared_ptr<HealthCheck> healthCheckClient_;
    std::shared_ptr<CkptFileManager> ckptFileManager_;
    std::shared_ptr<CheckpointOrchestrator> ckptOrch_;
    litebus::AID functionAgentAID_;
    bool reconnecting_ = false;
    bool synced_       = false;

    // Reconciliation state
    uint32_t orphanGracePeriodSec_ = kDefaultOrphanGracePeriodSec;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> orphanFirstSeen_;

    struct SandboxStatsSnapshot {
        uint64_t cpuUsageNs = 0;
        std::chrono::steady_clock::time_point collectedAt{};
    };

    std::unordered_map<std::string, SandboxStatsSnapshot> sandboxStatsSnapshots_;
    std::unordered_set<std::string> sandboxStatsPollingRuntimes_;
    std::unordered_map<std::string, SandboxLifecycleStatus> sandboxLifecycleStates_;
    // runtimeIDs for which user explicitly called terminate() — used to report COMPLETED on Wait
    std::unordered_set<std::string> userInitiatedTerminateRuntimes_;
    // RUNNING start time for each sandbox — used to report lifecycle duration
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> sandboxRunningStartTimes_;
};

/**
 * SandboxExecutorProxy — thin actor-message bridge.
 *
 * Wraps a SandboxExecutor and routes every public call through
 * litebus::Async so callers stay off the executor's actor thread.
 * Drop-in replacement for ContainerExecutorProxy.
 */
class SandboxExecutorProxy : public ExecutorProxy {
public:
    explicit SandboxExecutorProxy(const std::shared_ptr<SandboxExecutor> &executor)
        : ExecutorProxy(executor), sandbox_(executor) {}

    ~SandboxExecutorProxy() override = default;

    litebus::Future<messages::StartInstanceResponse> StartInstance(
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::vector<int> &cardIDs) override
    {
        return litebus::Async(sandbox_->GetAID(), &SandboxExecutor::StartInstance, request, cardIDs);
    }

    litebus::Future<Status> StopInstance(
        const std::shared_ptr<messages::StopInstanceRequest> &request, bool oomKilled = false) override
    {
        return litebus::Async(sandbox_->GetAID(), &SandboxExecutor::StopInstance, request, oomKilled);
    }

    litebus::Future<messages::SnapshotRuntimeResponse> SnapshotRuntime(
        const std::shared_ptr<messages::SnapshotRuntimeRequest> &request) override
    {
        return litebus::Async(sandbox_->GetAID(), &SandboxExecutor::SnapshotRuntime, request);
    }

    litebus::Future<std::map<std::string, messages::RuntimeInstanceInfo>> GetRuntimeInstanceInfos() override
    {
        return litebus::Async(sandbox_->GetAID(), &SandboxExecutor::GetRuntimeInstanceInfos);
    }

    void UpdatePrestartRuntimePromise(pid_t /* pid */) override {}
    void ClearCapability() override {}

    litebus::Future<bool> GracefulShutdown() override
    {
        return litebus::Async(sandbox_->GetAID(), &SandboxExecutor::StopAllSandboxes);
    }

    litebus::Future<messages::ReconcileRuntimesResponse> ReconcileRuntimes(
        const std::shared_ptr<messages::ReconcileRuntimesRequest> &request) override
    {
        return litebus::Async(sandbox_->GetAID(), &SandboxExecutor::ReconcileRuntimes, request);
    }
private:
    std::shared_ptr<SandboxExecutor> sandbox_;
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_EXECUTOR_SANDBOX_SANDBOX_EXECUTOR_H
