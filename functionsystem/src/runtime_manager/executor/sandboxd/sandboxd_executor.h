/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef RUNTIME_MANAGER_EXECUTOR_SANDBOXD_SANDBOXD_EXECUTOR_H
#define RUNTIME_MANAGER_EXECUTOR_SANDBOXD_SANDBOXD_EXECUTOR_H

#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "async/defer.hpp"
#include "common/metrics/metrics_adapter.h"
#include "common/utils/port_forward_mapping.h"
#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix/sandbox_api.grpc.pb.h"
#include "common/rpc/client/grpc_client.h"
#include "common/status/status.h"
#include "healthcheck/health_check.h"
#include "runtime_manager/ckpt/ckpt_file_manager.h"
#include "runtime_manager/ckpt/ckpt_file_manager_actor.h"
#include "runtime_manager/config/command_builder.h"
#include "runtime_manager/executor/executor.h"
#include "runtime_manager/executor/sandboxd/runtime_state_manager.h"
#include "runtime_manager/port/port_manager.h"
#include "sandboxd_checkpoint_orchestrator.h"
#include "sandboxd_request_builder.h"

namespace functionsystem {
class ActorWorker;
}

namespace functionsystem::runtime_manager {

class PauseArtifactPathManager;

struct SandboxdRestoreResult {
    Status status;
    std::string sandboxID;
    std::vector<std::string> ports;
};

struct SandboxdListResult {
    Status status;
    runtime::v1::ListSandboxesResponse response;
};

struct SandboxdResumeIdentity {
    bool trusted{false};
    bool reusable{false};
    bool rejected{false};
    std::string logicalRequestID;
    std::string tenantID;
    std::string snapshotID;
    int64_t expectedSize{0};
    std::string expectedSHA256;
    std::map<std::string, std::string> labels;
};

/**
 * SandboxdStartGuard — RAII guard for the in-progress start lifecycle.
 *
 * On construction:  registers an in-progress Future in RuntimeStateManager.
 * On destruction:   if Commit() was never called, calls Unregister() to roll
 *                   back all state — ensuring no map entries leak on failure.
 * On Commit():      records physical success. The lifecycle owner clears the
 *                   in-progress fence only after finalization/cleanup.
 *
 * The guard keeps failed starts from leaking state into actor-owned maps.
 */
class SandboxdStartGuard {
public:
    SandboxdStartGuard(RuntimeStateManager &mgr, std::string runtimeID,
                      litebus::Future<messages::StartInstanceResponse> future)
        : mgr_(mgr), runtimeID_(std::move(runtimeID))
    {
        mgr_.MarkStartInProgress(runtimeID_, std::move(future));
    }

    ~SandboxdStartGuard()
    {
        if (!committed_) {
            mgr_.RollbackUncommittedStart(runtimeID_);
        }
    }

    // Call when the start path has completed successfully.
    void Commit()
    {
        committed_ = true;
    }

    SandboxdStartGuard(const SandboxdStartGuard &) = delete;
    SandboxdStartGuard &operator=(const SandboxdStartGuard &) = delete;

private:
    RuntimeStateManager &mgr_;
    std::string runtimeID_;
    bool committed_ = false;
};

/**
 * SandboxdExecutor — lifecycle client for the sandboxd runtime.v1.SandboxService.
 *
 * It drives Start/Stop/Wait/List/Stats, warm-up, checkpoint, restore, and
 * reconciliation through sandboxd's SandboxService and relies on the shared
 * RuntimeStateManager.
 */
class SandboxdExecutor : public Executor {
public:
    struct RuntimeCapability {
        bool supportsCheckpointRestore = false;
        std::string checkpointHandoffPath;
        std::string restoreEnvPath;
    };

    using AvailableRuntimes = std::set<std::string>;
    using RuntimeCapabilities = std::unordered_map<std::string, RuntimeCapability>;
    using AvailableRuntimesCallback = std::function<void(bool, const AvailableRuntimes &)>;

    static constexpr uint32_t kDefaultOrphanGracePeriodSec = 180;
    static constexpr uint32_t kOrphanDeleteRetryIntervalSec = 70;
    static constexpr uint32_t kSandboxReclaimInitialBackoffMs = 1000;
    static constexpr uint32_t kSandboxReclaimMaxBackoffMs = 60000;

    enum class SandboxLifecycleStatus : int32_t {
        CREATING = 1,
        RUNNING = 2,
        COMPLETED = 3,
        ABNORMAL = 4,
    };

    SandboxdExecutor(const std::string &name, const litebus::AID &functionAgentAID,
                     const std::string &checkpointDir = {},
                     AvailableRuntimesCallback availableRuntimesCallback = {});
    ~SandboxdExecutor() override = default;

    void SetHealthCheckClient(const std::shared_ptr<HealthCheck> &healthCheck)
    {
        healthCheckClient_ = healthCheck;
    }

    // ── Executor interface ────────────────────────────────────────────────────

    litebus::Future<messages::StartInstanceResponse> StartInstance(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::vector<int> &cardIDs) override;

    litebus::Future<Status> StopInstance(const std::shared_ptr<messages::StopInstanceRequest> &request,
                                         bool oomKilled = false) override;

    // Snapshot is not exposed by the sandboxd SandboxService.
    litebus::Future<messages::SnapshotRuntimeResponse> SnapshotRuntime(
        const std::shared_ptr<messages::SnapshotRuntimeRequest> &request) override;

    std::map<std::string, messages::RuntimeInstanceInfo> GetRuntimeInstanceInfos() override;
    bool IsRuntimeActive(const std::string &runtimeID) override;
    std::shared_ptr<litebus::Exec> GetExecByRuntimeID(const std::string &runtimeID) override;
    void ClearCapability() override
    {
    }
    void UpdatePrestartRuntimePromise(pid_t /* pid */) override
    {
    }

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
        PortRouteKind routeKind = PortRouteKind::PUBLIC;
    };

    static std::vector<PortForwardConfig> ParseForwardPorts(const std::string &networkJson);
    static bool IsRetryableWaitError(const Status &status);
    static uint32_t SandboxReclaimBackoffMs(uint32_t failedAttempts);
    static bool IsResultUnknownRpcError(const Status &status);
    static std::vector<::messages::SandboxPhysicalFact> ProjectPhysicalFacts(
        const runtime::v1::ListSandboxesResponse &listResponse);
    static Status BuildPortReservationsFromPhysicalFacts(
        const runtime::v1::ListSandboxesResponse &listResponse,
        PortManager::ReservationMap &reservations);
    litebus::Future<Status> DeleteReusableSnapshotCheckpoint(
        const ::messages::SnapshotAttemptFinalizeRequest &request);
protected:
    void Init() override;
    void Finalize() override;
    void InitPrestartRuntimePool() override
    {
    }
    void InitVirtualEnvIdleTimeLimit() override
    {
    }

private:
    static SandboxdResumeIdentity ConsumeRestoreIdentity(messages::StartInstanceRequest &request);
    static bool IsRestoreRequest(const messages::RuntimeInstanceInfo &info);
    static Status BuildSnapshotCheckpointPlan(
        const messages::SnapshotRuntimeRequest &request, const std::string &sandboxID,
        CheckpointPlan &plan);
    static bool UsesLegacySnapshotRegistry(const messages::SnapshotRuntimeRequest &request);
    static Status BuildReusableSnapshotCleanupIdentity(
        const ::messages::SnapshotAttemptFinalizeRequest &request, const SandboxInfo &source,
        const std::string &checkpointRoot, std::string &checkpointDirectory);
    // ── Start paths: normal / warm-up (Register) / restore (Restore) ───────────

    struct SandboxdStartContext {
        std::shared_ptr<messages::StartInstanceRequest> request;
        CommandArgs cmdArgs;
        std::string port;
        Envs envs;
        std::vector<int> cardIDs;
        std::shared_ptr<SandboxdStartGuard> guard;
        SandboxdResumeIdentity resumeIdentity;
    };

    struct SandboxdRestoreContext {
        std::string checkpointPath;
        SandboxdStartContext start;
    };

    struct ReconcileCleanupContext {
        std::string requestID;
        std::shared_ptr<runtime::v1::ListSandboxesResponse> listResp;
        std::unordered_set<std::string> expectedIDs;
        std::chrono::steady_clock::time_point now;
        messages::ReconcileRuntimesResponse *response = nullptr;
        int32_t *orphansCleaned = nullptr;
        std::unordered_set<std::string> *actualRunningIDs = nullptr;
    };

    litebus::Future<messages::StartInstanceResponse> StartNormal(const SandboxdStartContext &context);

    litebus::Future<messages::StartInstanceResponse> OnStartDone(
        const runtime::v1::StartResponse &response, const std::shared_ptr<messages::StartInstanceRequest> &request,
        std::shared_ptr<SandboxdStartGuard> guard);

    litebus::Future<messages::StartInstanceResponse> OnStartCompleted(
        const std::string &runtimeID, const messages::StartInstanceResponse &response);
    void OnPendingDeleteCompleted(const std::string &runtimeID,
                                  const litebus::Future<Status> &future);

    // Warm-up: register a reusable SandboxTemplate via the Register RPC.
    litebus::Future<messages::StartInstanceResponse> StartWarmUp(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const CommandArgs &cmdArgs,
        const std::string &port, const Envs &envs, std::shared_ptr<SandboxdStartGuard> guard);
    litebus::Future<messages::StartInstanceResponse> OnWarmUpRegistered(
        const runtime::v1::NormalResponse &response, const std::shared_ptr<messages::StartInstanceRequest> &request,
        std::shared_ptr<SandboxdStartGuard> guard);

    // Restore: legacy snapshots use download/ref-count; trusted resume uses an
    // attempt-scoped PauseArtifactPathManager path directly.
    litebus::Future<messages::StartInstanceResponse> StartBySnapshot(const SandboxdStartContext &context);
    litebus::Future<messages::StartInstanceResponse> OnCheckpointDownloaded(const std::string &checkpointPath,
                                                                            const SandboxdStartContext &context);
    litebus::Future<messages::StartInstanceResponse> OnCheckpointRefAdded(const Status &refStatus,
        const SandboxdRestoreContext &context);
    litebus::Future<messages::StartInstanceResponse> ReconcileBeforeResumeRestore(
        const SandboxdRestoreContext &context);
    litebus::Future<messages::StartInstanceResponse> RestoreAfterExactAbsence(
        const SandboxdRestoreContext &context);
    litebus::Future<messages::StartInstanceResponse> OnResumeRestoreUncertain(
        const SandboxdRestoreResult &result, const SandboxdRestoreContext &context,
        const std::shared_ptr<runtime::v1::StartRequest> &restoreReq, bool retried);
    litebus::Future<messages::StartInstanceResponse> OnRestoreDone(
        const SandboxdRestoreResult &result, const std::shared_ptr<messages::StartInstanceRequest> &request,
        std::shared_ptr<SandboxdStartGuard> guard, bool trustedResume, bool exactExisting = false);

    // ── Stop helpers ──────────────────────────────────────────────────────────

    litebus::Future<Status> StopSandbox(
        const std::shared_ptr<messages::StopInstanceRequest> &request, bool oomKilled);
    litebus::Future<Status> DeletePauseSourceCheckpoint(
        const std::shared_ptr<messages::StopInstanceRequest> &request);
    litebus::Future<Status> StopExactResumeSandboxAfterAgentRestart(
        const std::string &runtimeID, const std::string &requestID,
        const std::string &logicalInstanceID, const std::string &targetAttemptID);
    litebus::Future<Status> OnExactResumeCleanupListed(
        const std::string &runtimeID, const std::string &requestID,
        const std::string &logicalInstanceID, const std::string &targetAttemptID,
        const SandboxdListResult &listed);
    litebus::Future<Status> TerminateSandbox(const std::string &runtimeID, const std::string &requestID,
                                             const std::string &sandboxID, bool force);
    litebus::Future<Status> OnDeleteDone(const std::string &runtimeID, const std::string &requestID,
                                         const std::string &sandboxID, const runtime::v1::DeleteResponse &response);
    // Warm-up teardown via the Unregister RPC.
    litebus::Future<Status> UnregisterWarmUp(const std::string &runtimeID, const std::string &requestID);
    litebus::Future<Status> OnWarmUpUnregistered(const runtime::v1::NormalResponse &response,
                                                 const std::string &runtimeID, const std::string &requestID);

    // ── gRPC call wrappers (thin, no business logic) ──────────────────────────

    litebus::Future<runtime::v1::ListSandboxesResponse> DoList();
    litebus::Future<SandboxdListResult> DoListForOperation(
        const std::string &sandboxID, const std::map<std::string, std::string> &selector = {});
    litebus::Future<runtime::v1::StartResponse> DoStart(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                                        const std::shared_ptr<runtime::v1::StartRequest> &startReq);

    litebus::Future<Status> DoDelete(const std::string &instanceID, const std::string &runtimeID,
                                     const std::string &requestID,
                                     const std::shared_ptr<runtime::v1::DeleteRequest> &req);

    litebus::Future<runtime::v1::NormalResponse> DoRegister(const std::shared_ptr<runtime::v1::RegisterRequest> &req);
    litebus::Future<runtime::v1::NormalResponse> DoUnregister(
        const std::shared_ptr<runtime::v1::UnregisterRequest> &req);
    litebus::Future<runtime::v1::GetRegisteredResponse> DoGetRegistered();
    litebus::Future<SandboxdRestoreResult> DoRestore(
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::shared_ptr<runtime::v1::StartRequest> &req);

    void DoWait(const std::string &sandboxID, const std::string &runtimeID);
    void RestoreWait(const std::string &sandboxID);

    // ── Wait retry on sandboxd disconnection ─────────────────────────────────
    void DoWaitWithRetry(const std::string &sandboxID, const std::string &runtimeID, int retryCount);

    void CollectSandboxStats(const std::string &runtimeID, const std::string &sandboxID);
    void ScheduleSandboxStatsCollection(const std::string &runtimeID, const std::string &sandboxID);
    void ScheduleRunningStatusHeartbeat(const std::string &runtimeID);
    void ReportRunningStatusHeartbeat(const std::string &runtimeID);
    litebus::Future<Status> OnSandboxStatsCollected(const std::string &runtimeID, const std::string &sandboxID,
                                                    const Status &status, const runtime::v1::StatsResponse &response,
                                                    std::chrono::steady_clock::time_point collectedAt);

    litebus::Future<Status> CleanupSandboxAfterMaxRetries(const std::string &runtimeID, const std::string &sandboxID);
    void StartSandboxReclaim(const std::string &runtimeID, const std::string &sandboxID,
                             const std::string &requestID);
    void ReclaimSandbox(const std::string &runtimeID, const std::string &sandboxID,
                        const std::string &requestID);
    litebus::Future<Status> OnReclaimSandboxDone(const std::string &runtimeID, const std::string &sandboxID,
                                                 const std::string &requestID,
                                                 litebus::Try<runtime::v1::DeleteResponse> response);

    litebus::Future<Status> OnWaitDone(const std::string &runtimeID, const std::string &sandboxID,
                                      const runtime::v1::WaitResponse &response);

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

    // ── Connectivity ──────────────────────────────────────────────────────────

    void Sync();
    Status OnStartupPhysicalFacts(const SandboxdListResult &listed);
    void RetryStartupPhysicalFacts();
    void ScheduleStartupPhysicalFactsRetry();
    void ReconnectContainerd();
    void OnReconnectContainerd();
    void CheckConnectivity();
    void FetchInitialAvailableRuntimes();
    void RetryFetchInitialAvailableRuntimes();
    void ScheduleAvailableRuntimesRetry();
    Status OnListAvailableRuntimes(const std::shared_ptr<runtime::v1::ListAvailableRuntimesResponse> &response,
                                   const Status &status);
    bool SupportsCheckpointRestore(const std::string &runtimeClass) const;
    void ApplyCheckpointRestoreEnvironment(const std::string &runtimeClass,
                                           google::protobuf::Map<std::string, std::string> *envs) const;

    messages::StartInstanceResponse MakeSuccessStartResponse(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::string &sandboxID);

    void ReportMetrics(const std::string &instanceID, const std::string &runtimeID, const std::string &sandboxID,
                       const functionsystem::metrics::MeterTitle &title);

    void DoReportMetrics(const std::string &instanceID, const std::string &runtimeID, const std::string &sandboxID,
                         const functionsystem::metrics::MeterTitle &title);

    // ── Reconciliation helpers ────────────────────────────────────────────────

    void WaitAndReconcile(const std::shared_ptr<messages::ReconcileRuntimesRequest> &request, int32_t retryCount,
        const std::shared_ptr<litebus::Promise<messages::ReconcileRuntimesResponse>> &promise);

    messages::ReconcileRuntimesResponse OnReconcileRuntimes(
        const std::shared_ptr<messages::ReconcileRuntimesRequest> &request,
        const std::shared_ptr<runtime::v1::ListSandboxesResponse> &listResp);

    void CleanupExitedSandboxes(const std::string &requestID,
                                const std::shared_ptr<runtime::v1::ListSandboxesResponse> &listResp,
                                messages::ReconcileRuntimesResponse *response, int32_t *orphansCleaned);

    void CleanupOrphanSandboxes(const ReconcileCleanupContext &context);

    void AddMissingAndConfirmedEntries(const std::shared_ptr<messages::ReconcileRuntimesRequest> &request,
        const std::unordered_set<std::string> &actualRunningIDs,
        messages::ReconcileRuntimesResponse *response);

    void PurgeOrphanTracking(const std::unordered_set<std::string> &actualRunningIDs);
    void CleanupLocalRuntimeStateForOrphan(const std::string &requestID, const std::string &sandboxID);
    void DeleteSandboxAsync(const std::string &sandboxID);
    Status OnDeleteSandboxComplete(const std::string &sandboxID, litebus::Try<runtime::v1::DeleteResponse> rsp);
    std::shared_ptr<PauseArtifactPathManager> CreatePauseArtifactPathManager(
        const std::string &tenantHash, const std::string &instanceID);
    Status BuildStartCommandArgs(const std::shared_ptr<messages::StartInstanceRequest> &request,
                                 const std::string &port, CommandArgs *cmdArgs);
    Status ApplyPortForwardMappings(SandboxdStartParams *params,
        const std::shared_ptr<messages::StartInstanceRequest> &request);
    Status ApplySandboxPhysicalPortMappings(
        const std::string &runtimeID,
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::vector<std::string> &physicalPorts);
    Status ApplyExistingSandboxPhysicalPortMappings(
        const std::string &runtimeID,
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::vector<std::string> &physicalPorts);
    Status DecodeSandboxPhysicalPortMappings(
        const std::shared_ptr<messages::StartInstanceRequest> &request,
        const std::vector<std::string> &physicalPorts,
        std::vector<int> &hostPorts, std::string &portMappingsJson) const;

    // ── State ─────────────────────────────────────────────────────────────────

    RuntimeStateManager stateManager_;
    CommandBuilder cmdBuilder_{false};
    std::shared_ptr<GrpcClient<runtime::v1::SandboxService>> sandboxd_{nullptr};
    std::shared_ptr<HealthCheck> healthCheckClient_;
    std::shared_ptr<CkptFileManager> ckptFileManager_;
    std::shared_ptr<SandboxdCheckpointOrchestrator> ckptOrch_;
    std::string checkpointRoot_;
    std::shared_ptr<ActorWorker> pauseArtifactWorker_;
    // runtimeIDs registered as warm-up templates (route StopInstance -> Unregister)
    std::unordered_set<std::string> warmupRuntimes_;
    std::unordered_set<std::string> registeredTemplateIDs_;
    std::unordered_map<std::string,
                       std::vector<std::shared_ptr<litebus::Promise<Status>>>> pendingDeleteWaiters_;
    litebus::AID functionAgentAID_;
    AvailableRuntimesCallback availableRuntimesCallback_;
    RuntimeCapabilities runtimeCapabilities_;
    bool reconnecting_ = false;
    bool synced_       = false;
    bool fetchingAvailableRuntimes_ = false;
    bool availableRuntimesInitialized_ = false;
    bool availableRuntimesRetryScheduled_ = false;
    bool startupPhysicalFactsReady_ = false;
    bool startupPhysicalFactsRetryScheduled_ = false;

    // ── Reconciliation state ─────────────────────────────────────────────────
    uint32_t orphanGracePeriodSec_ = kDefaultOrphanGracePeriodSec;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> orphanFirstSeen_;

    struct SandboxStatsSnapshot {
        uint64_t cpuUsageNs = 0;
        std::chrono::steady_clock::time_point collectedAt{};
    };

    std::unordered_map<std::string, SandboxStatsSnapshot> sandboxStatsSnapshots_;
    std::unordered_set<std::string> sandboxStatsPollingRuntimes_;
    std::unordered_map<std::string, SandboxLifecycleStatus> sandboxLifecycleStates_;
    std::unordered_set<std::string> userInitiatedTerminateRuntimes_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> sandboxRunningStartTimes_;
    struct SandboxReclaimState {
        std::string sandboxID;
        uint32_t failedAttempts = 0;
        std::shared_ptr<litebus::Promise<Status>> completion;
    };
    std::unordered_map<std::string, SandboxReclaimState> sandboxReclaims_;
};

/**
 * SandboxdExecutorProxy — thin actor-message bridge.
 */
class SandboxdExecutorProxy : public ExecutorProxy {
public:
    explicit SandboxdExecutorProxy(const std::shared_ptr<SandboxdExecutor> &executor)
        : ExecutorProxy(executor), sandboxd_(executor)
    {
    }

    ~SandboxdExecutorProxy() override = default;

    litebus::Future<messages::StartInstanceResponse> StartInstance(
        const std::shared_ptr<messages::StartInstanceRequest> &request, const std::vector<int> &cardIDs) override
    {
        return litebus::Async(sandboxd_->GetAID(), &SandboxdExecutor::StartInstance, request, cardIDs);
    }

    litebus::Future<Status> StopInstance(const std::shared_ptr<messages::StopInstanceRequest> &request,
                                         bool oomKilled = false) override
    {
        return litebus::Async(sandboxd_->GetAID(), &SandboxdExecutor::StopInstance, request, oomKilled);
    }

    litebus::Future<messages::SnapshotRuntimeResponse> SnapshotRuntime(
        const std::shared_ptr<messages::SnapshotRuntimeRequest> &request) override
    {
        return litebus::Async(sandboxd_->GetAID(), &SandboxdExecutor::SnapshotRuntime, request);
    }

    virtual litebus::Future<Status> DeleteReusableSnapshotCheckpoint(
        const ::messages::SnapshotAttemptFinalizeRequest &request)
    {
        return litebus::Async(sandboxd_->GetAID(),
                              &SandboxdExecutor::DeleteReusableSnapshotCheckpoint, request);
    }

    litebus::Future<std::map<std::string, messages::RuntimeInstanceInfo>> GetRuntimeInstanceInfos() override
    {
        return litebus::Async(sandboxd_->GetAID(), &SandboxdExecutor::GetRuntimeInstanceInfos);
    }

    void UpdatePrestartRuntimePromise(pid_t /* pid */) override
    {
    }
    void ClearCapability() override
    {
    }

    litebus::Future<bool> GracefulShutdown() override
    {
        return litebus::Async(sandboxd_->GetAID(), &SandboxdExecutor::StopAllSandboxes);
    }

    litebus::Future<messages::ReconcileRuntimesResponse> ReconcileRuntimes(
        const std::shared_ptr<messages::ReconcileRuntimesRequest> &request) override
    {
        return litebus::Async(sandboxd_->GetAID(), &SandboxdExecutor::ReconcileRuntimes, request);
    }

private:
    std::shared_ptr<SandboxdExecutor> sandboxd_;
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_EXECUTOR_SANDBOXD_SANDBOXD_EXECUTOR_H
