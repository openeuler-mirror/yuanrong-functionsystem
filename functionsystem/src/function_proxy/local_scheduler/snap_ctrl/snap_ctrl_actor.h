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

#ifndef LOCAL_SCHEDULER_SNAP_CTRL_ACTOR_H
#define LOCAL_SCHEDULER_SNAP_CTRL_ACTOR_H

#include <actor/actor.hpp>
#include <async/future.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/logs/logging.h"
#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix_pb.h"
#include "common/schedule_decision/scheduler_common.h"
#include "common/state_machine/instance_control_view.h"
#include "common/state_machine/instance_state_machine.h"
#include "common/status/status.h"
#include "common/utils/actor_worker.h"
#include "function_proxy/common/posix_client/control_plane_client/control_interface_client_manager_proxy.h"
#include "local_scheduler/function_agent_manager/function_agent_mgr_actor.h"
#include "local_scheduler/instance_control/instance_ctrl.h"
#include "local_scheduler/local_scheduler_service/local_sched_srv.h"
#include "local_scheduler/snap_ctrl/instance_lifecycle.h"
#include "local_scheduler/snap_ctrl/resume_attempt_registry.h"

namespace functionsystem::local_scheduler {

class FunctionAgentMgr;
class LocalSchedSrv;
class InstanceCtrlActor;

struct PauseRetryPolicy {
    uint64_t initialDelayMs{ 10 };
    uint64_t maximumDelayMs{ 1'000 };
    uint64_t operationTimeoutMs{ 60'000 };
};

class SnapCtrlActor : public BasisActor {
public:
    SnapCtrlActor(const std::string &name, const std::string &nodeID,
                  PauseRetryPolicy pauseRetryPolicy = {});
    ~SnapCtrlActor() override = default;

    void Init() override;

    void Finalize() override;

    /**
     * Handle INSTANCE_SNAPSHOT_SIGNAL
     * Create a snapshot of the running instance
     * @param requestID: Request ID for tracing
     * @param instanceID: ID of the instance to snapshot
     * @param payload: core_service::SnapOptions payload
     * @return KillResponse containing snapshot info payload
     */
    litebus::Future<KillResponse> HandleSnapshot(const std::string &requestID,
                                                  const std::string &instanceID,
                                                  const std::string &payload);

    litebus::Future<KillResponse> HandleAnonymousCheckpoint(
        const std::string &requestID, const std::string &instanceID,
        uint64_t checkpointTimeoutMs = 0);

    /**
     * Callback to convert SnapshotResult to KillResponse
     * @param result: The snapshot result
     * @return KillResponse with appropriate code and payload
     */
    KillResponse OnHandleSnapshot(const SnapshotResult &result);

    /**
     * Handle INSTANCE_SNAPSTART_SIGNAL
     * Restore an instance from a snapshot
     * @param requestID: Request ID for tracing
     * @param checkpointID: The checkpoint ID to restore from
     * @param payload: core_service::SnapStartOptions payload
     * @return KillResponse with restore result
     */
    litebus::Future<KillResponse> HandleSnapStart(const std::string &requestID,
                                                   const std::string &checkpointID,
                                                   const std::string &payload);

    litebus::Future<DeletePreparation> PrepareForAuthorizedDelete(const std::string &instanceID);

    litebus::Future<Status> FinishAuthorizedDelete(const std::string &instanceID, uint64_t generation);

    litebus::Future<Status> DeletePauseSnapshot(const resources::InstanceInfo &instanceInfo);

    /**
     * Handle snapstart instance initialization after state transition to CREATING
     * Complete flow: DeployInstance -> CreateInstanceClient -> StartHeartbeat
     *                -> SnapStarted -> TransInstanceState(RUNNING) -> SetValue
     * @param scheduleResp: Promise to return schedule response
     * @param scheduleReq: The schedule request for the restored instance
     * @param result: Schedule result from scheduler
     * @param transResult: Transition result from state transition to CREATING
     * @return Option of TransitionResult
     */
    void SnapStart(
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleResp,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const schedule_decision::ScheduleResult &result,
        const TransitionResult &transResult,
        const std::shared_ptr<const resume_identity::TrustedResumeIdentity> &trustedResumeIdentity = nullptr);

    /**
     * Bind the FunctionAgentMgr for sending snapshot requests to agents
     * @param functionAgentMgr: The function agent manager interface
     */
    void BindFunctionAgentMgr(const std::shared_ptr<FunctionAgentMgr> &functionAgentMgr)
    {
        functionAgentMgr_ = functionAgentMgr;
    }

    /**
     * Bind the LocalSchedSrv for recording snapshot metadata
     * @param localSchedSrv: The local scheduler service interface
     */
    void BindLocalSchedSrv(const std::shared_ptr<LocalSchedSrv> &localSchedSrv)
    {
        localSchedSrv_ = localSchedSrv;
    }

    /**
     * Bind the InstanceControlView for accessing instance state machines
     * @param instanceControlView: The instance control view
     */
    void BindInstanceControlView(const std::shared_ptr<InstanceControlView> &instanceControlView)
    {
        instanceControlView_ = instanceControlView;
    }

    /**
     * Bind the ControlInterfaceClientManagerProxy for accessing instance clients
     * @param clientManager: The client manager
     */
    void BindClientManager(const std::shared_ptr<ControlInterfaceClientManagerProxy> &clientManager)
    {
        clientManager_ = clientManager;
    }

    /**
     * Bind the InstanceCtrlActor for deleting instances
     * @param instanceCtrl: The instance control actor
     */
    void BindInstanceCtrl(const std::shared_ptr<InstanceCtrl> &instanceCtrl)
    {
        instanceCtrl_ = instanceCtrl;
    }

    /**
     * Bind the per-instance reverse-tunnel admission gate.  The acquire
     * callback returns false when a tunnel is active; a successful acquire is
     * paired with exactly one release after the reusable Snapshot operation
     * reaches a terminal response.
     */
    void BindReusableSnapshotTunnelGate(
        std::function<bool(const std::string &)> acquire,
        std::function<void(const std::string &)> release)
    {
        reusableSnapshotTunnelGateAcquire_ = std::move(acquire);
        reusableSnapshotTunnelGateRelease_ = std::move(release);
    }

    /** Replay exact RESUME_COMMITTED cleanup for an already RUNNING winner. */
    void ReplayCommittedResumeFinalize(
        const std::shared_ptr<messages::ScheduleRequest> &sourceRequest,
        const resources::InstanceInfo &authoritative,
        const resume_identity::TrustedResumeIdentity &identity);

private:
    struct AnonymousCheckpointContext {
        std::string requestID;
        std::string instanceID;
        std::string snapshotID;
        resources::InstanceInfo instanceInfo;
        messages::SnapshotRuntimeResponse snapshotResponse;
        std::shared_ptr<litebus::Promise<KillResponse>> completion;
        std::chrono::steady_clock::time_point snapStartedDeadline;
    };

    void OnAnonymousCheckpointPrepared(
        const std::shared_ptr<AnonymousCheckpointContext> &context,
        const litebus::Future<Status> &future);
    void OnAnonymousCheckpointCreated(
        const std::shared_ptr<AnonymousCheckpointContext> &context,
        const litebus::Future<messages::SnapshotRuntimeResponse> &future);
    void RetryAnonymousCheckpointClient(
        const std::shared_ptr<AnonymousCheckpointContext> &context);
    void OnAnonymousCheckpointClient(
        const std::shared_ptr<AnonymousCheckpointContext> &context,
        const litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> &future);
    void OnAnonymousCheckpointStarted(
        const std::shared_ptr<AnonymousCheckpointContext> &context,
        const litebus::Future<runtime::SnapStartedResponse> &future);
    static void CompleteAnonymousCheckpoint(
        const std::shared_ptr<AnonymousCheckpointContext> &context,
        common::ErrorCode code, const std::string &message);

    struct ReusableSnapshotContext {
        std::string requestID;
        std::string instanceID;
        std::string snapshotID;
        std::string name;
        resources::InstanceInfo sourceInstanceInfo;
        ::messages::SnapshotArtifact artifact;
        ::core_service::SnapshotInfo publicInfo;
        std::shared_ptr<litebus::Promise<KillResponse>> completion;
        bool tunnelGateHeld{ false };
        bool completed{ false };
    };

    litebus::Future<KillResponse> HandleReusableSnapshot(
        const std::string &requestID, const std::string &instanceID,
        const resources::InstanceInfo &instanceInfo, const SnapOptions &options);
    void OnReusableSnapshotBegun(
        const std::shared_ptr<ReusableSnapshotContext> &context,
        const litebus::Future<::messages::BeginReusableSnapshotResponse> &future);
    void OnReusableSnapshotResolvedForCleanup(
        const std::shared_ptr<ReusableSnapshotContext> &context,
        const litebus::Future<::messages::ResolveReusableSnapshotForCreateResponse> &future);
    void OnReusableSnapshotPrepared(
        const std::shared_ptr<ReusableSnapshotContext> &context,
        const litebus::Future<Status> &future);
    void OnReusableSnapshotCheckpointed(
        const std::shared_ptr<ReusableSnapshotContext> &context,
        const litebus::Future<::messages::SnapshotRuntimeResponse> &future);
    void OnReusableSnapshotPublished(
        const std::shared_ptr<ReusableSnapshotContext> &context,
        const litebus::Future<::messages::SnapshotRuntimeResponse> &future);
    void OnReusableSnapshotClient(
        const std::shared_ptr<ReusableSnapshotContext> &context,
        const litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> &future);
    void OnReusableSnapshotStarted(
        const std::shared_ptr<ReusableSnapshotContext> &context,
        const litebus::Future<runtime::SnapStartedResponse> &future);
    void OnReusableSnapshotCommitted(
        const std::shared_ptr<ReusableSnapshotContext> &context,
        const litebus::Future<::messages::CommitReusableSnapshotResponse> &future);
    void OnReusableSnapshotFinalized(
        const std::shared_ptr<ReusableSnapshotContext> &context,
        ::messages::SnapshotAttemptFinalizeOperation operation,
        common::ErrorCode terminalCode, const std::string &terminalMessage,
        const litebus::Future<::messages::SnapshotAttemptFinalizeResponse> &future);
    void OnReusableSnapshotFailedRecord(
        const std::shared_ptr<ReusableSnapshotContext> &context,
        common::ErrorCode terminalCode, const std::string &terminalMessage,
        const litebus::Future<::messages::FailReusableSnapshotResponse> &future);
    void FailReusableSnapshot(
        const std::shared_ptr<ReusableSnapshotContext> &context,
        common::ErrorCode code, const std::string &message);
    void FinalizeReusableSnapshot(
        const std::shared_ptr<ReusableSnapshotContext> &context,
        ::messages::SnapshotAttemptFinalizeOperation operation,
        common::ErrorCode terminalCode, const std::string &terminalMessage);
    void CompleteReusableSnapshotRequest(
        const std::shared_ptr<ReusableSnapshotContext> &context,
        const KillResponse &response);
    KillResponse BuildReusableSnapshotResponse(
        common::ErrorCode code, const std::string &message,
        const ::core_service::SnapshotInfo *snapshotInfo = nullptr) const;
    void ReleaseReusableSnapshotTunnelGate(
        const std::shared_ptr<ReusableSnapshotContext> &context);

    enum class PausePhase {
        GATE,
        PREPARE,
        CHECKPOINT,
        CONVERGING,
    };

    enum class ConvergenceResult {
        COMMITTED,
        SOURCE_RUNNING,
        RESULT_UNKNOWN,
        IDENTITY_CHANGED,
        TERMINAL_FAILURE,
    };

    struct PauseContext {
        std::string operationRequestID;
        resources::InstanceInfo sourceInstanceInfo;
        int32_t ttlSeconds{ 90'000 };
        std::string snapshotCreateTime;
        std::string storageBackend;
        uint64_t publishedSize{ 0 };
        std::string publishedSha256;
        bool published{ false };
        std::shared_ptr<litebus::Promise<KillResponse>> completion;
        std::chrono::steady_clock::time_point prepareDeadline;
        uint64_t prepareRetryDelayMs{ 0 };
        PausePhase phase{ PausePhase::GATE };
    };

    enum class InstanceLifecyclePhase {
        PAUSING,
        PREPARING_DELETE,
    };

    struct InstanceLifecycleState {
        InstanceLifecyclePhase phase{ InstanceLifecyclePhase::PAUSING };
        uint64_t generation{ 0 };
        std::shared_ptr<PauseContext> pauseContext;
        std::shared_ptr<litebus::Promise<DeletePreparation>> deletePreparation;
    };

    bool IsCurrentPauseContext(const std::string &instanceID,
                               const std::shared_ptr<PauseContext> &context) const;

    bool DeleteOwnsLifecycle(const std::string &instanceID,
                             const std::shared_ptr<PauseContext> &context) const;

    litebus::Future<KillResponse> HandlePauseResumeSnapshot(
        const std::string &requestID, const std::string &instanceID,
        const std::shared_ptr<InstanceStateMachine> &stateMachine, int32_t ttlSeconds,
        uint64_t checkpointTimeoutMs);

    void OnPauseGateComplete(const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
                             const litebus::Future<Status> &gateFuture);

    void PreparePauseSource(const std::string &instanceID, const std::shared_ptr<PauseContext> &context);

    void RetryPausePrepare(const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
                           const std::string &failedOperation);

    void OnPausePrepareComplete(const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
                                const litebus::Future<Status> &prepareFuture);

    litebus::Future<KillResponse> ContinuePauseCheckpoint(const std::string &instanceID,
                                                          const std::shared_ptr<PauseContext> &context);

    void RetryPauseCheckpoint(const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
                              const std::shared_ptr<litebus::Promise<KillResponse>> &promise);

    litebus::Future<KillResponse> HandlePauseCheckpointResponse(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        const messages::SnapshotRuntimeResponse &response);

    ConvergenceResult ClassifyPauseCheckpointResponse(
        const messages::SnapshotRuntimeResponse &response,
        const Status &continuationStatus) const;

    ConvergenceResult ClassifyPausedCommit(
        const std::shared_ptr<PauseContext> &context,
        const resources::InstanceInfo &latest) const;

    litebus::Future<KillResponse> RecoverRunningPauseSource(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        const KillResponse &originalError);

    void RetryRunningPauseClient(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        const KillResponse &originalError, const std::shared_ptr<litebus::Promise<KillResponse>> &result);

    void OnRunningPauseClient(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        const KillResponse &originalError, const std::shared_ptr<litebus::Promise<KillResponse>> &result,
        const litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> &clientFuture);

    void OnRunningPauseStarted(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        const KillResponse &originalError, const std::shared_ptr<litebus::Promise<KillResponse>> &result,
        const litebus::Future<runtime::SnapStartedResponse> &startedFuture);

    void RetryRunningPauseGate(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        const KillResponse &originalError, const std::shared_ptr<litebus::Promise<KillResponse>> &result);

    void OnRunningPauseGateRecovered(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        const KillResponse &originalError, const std::shared_ptr<litebus::Promise<KillResponse>> &result,
        const litebus::Future<Status> &recoverFuture);

    void RetryPreparePauseGate(const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
                               const KillResponse &originalError);

    void OnPreparePauseGateRecovered(const std::string &instanceID,
                                     const std::shared_ptr<PauseContext> &context,
                                     const KillResponse &originalError,
                                     const litebus::Future<Status> &recoverFuture);

    litebus::Future<KillResponse> CleanupPauseAttemptRemoteArtifacts(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        const KillResponse &originalError);

    litebus::Future<KillResponse> ReconcilePausedCommit(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        const KillResponse &commitError);

    litebus::Future<KillResponse> FinalizeSuccessfulPause(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context);

    litebus::Future<KillResponse> FinalizePauseAfterResourceRelease(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context);

    void RetryPausedResourceRelease(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        const std::shared_ptr<litebus::Promise<KillResponse>> &result);

    void OnPausedResourceReleased(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        const std::shared_ptr<litebus::Promise<KillResponse>> &result,
        const litebus::Future<Status> &releaseFuture);

    litebus::Future<KillResponse> FinalizePauseAttempt(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        ::messages::SnapshotAttemptFinalizeOperation operation, const KillResponse &terminalResponse);

    void RetryPauseAttemptFinalization(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        ::messages::SnapshotAttemptFinalizeOperation operation, const KillResponse &terminalResponse,
        const std::shared_ptr<litebus::Promise<KillResponse>> &result);

    void OnPauseAttemptFinalized(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        ::messages::SnapshotAttemptFinalizeOperation operation, const KillResponse &terminalResponse,
        const std::shared_ptr<litebus::Promise<KillResponse>> &result,
        const litebus::Future<::messages::SnapshotAttemptFinalizeResponse> &finalizeFuture);

    void RetryPausedCommitReconcile(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        const KillResponse &commitError, const std::shared_ptr<litebus::Promise<KillResponse>> &result);

    void OnPausedCommitSynced(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        const KillResponse &commitError, const std::shared_ptr<litebus::Promise<KillResponse>> &result,
        const litebus::Future<resources::InstanceInfo> &syncFuture);

    litebus::Future<KillResponse> ReleasePauseSourceRuntime(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context);

    void RetryReleasePauseSourceRuntime(const std::string &instanceID,
                                        const std::shared_ptr<PauseContext> &context,
                                        const std::shared_ptr<litebus::Promise<KillResponse>> &promise);

    litebus::Future<KillResponse> CommitPausedState(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context);

    litebus::Future<KillResponse> FailPauseAndCleanupTemporary(
        const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
        const Status &status);

    Status ValidatePauseContinuation(const std::string &instanceID,
                                     const std::shared_ptr<PauseContext> &context) const;

    void OnPauseCheckpointComplete(const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
                                   const litebus::Future<KillResponse> &checkpointFuture);

    void CompletePauseContext(const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
                              const KillResponse &response);

    /**
     * Prepare snapshot by calling runtime PrepareSnap interface
     * @param requestID: Request ID for tracing
     * @param instanceID: ID of the instance
     * @return Status of preparation
     */
    litebus::Future<Status> PrepareSnap(const std::string &requestID, const std::string &instanceID);

    /**
     * Handle DeploySnapStartInstance completion
     * @param scheduleResp: Promise to return schedule response
     * @param scheduleReq: The schedule request
     * @param deployFuture: Deploy response future
     */
    void OnDeploySnapStartInstanceComplete(
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleResp,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const std::shared_ptr<const resume_identity::TrustedResumeIdentity> &trustedResumeIdentity,
        const std::shared_ptr<ResumeContext> &resumeContext,
        const litebus::Future<messages::DeployInstanceResponse> &deployFuture);

    void RetryTrustedResumeAfterResultUnknown(const std::shared_ptr<ResumeContext> &context);

    void OnTrustedResumeClientCreated(
        const std::shared_ptr<ResumeContext> &context,
        const litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> &clientFuture);

    void OnTrustedResumeSnapStarted(
        const std::shared_ptr<ResumeContext> &context,
        const litebus::Future<runtime::SnapStartedResponse> &startedFuture);

    void CommitTrustedResume(const std::shared_ptr<ResumeContext> &context);

    void OnTrustedResumeCommitted(
        const std::shared_ptr<ResumeContext> &context,
        const litebus::Future<TransitionResult> &transitionFuture);

    void ReconcileTrustedResume(const std::shared_ptr<ResumeContext> &context, bool resultUnknown);

    void OnTrustedResumeReconciled(
        const std::shared_ptr<ResumeContext> &context, bool resultUnknown,
        const litebus::Future<resources::InstanceInfo> &syncFuture);

    void ConfirmTrustedResumeWinner(const std::shared_ptr<ResumeContext> &context,
                                    const resources::InstanceInfo *authoritative = nullptr);

    void BeginTrustedResumeWinnerCleanup(const std::shared_ptr<ResumeContext> &context);

    void CleanupTrustedResumeWinner(const std::shared_ptr<ResumeContext> &context);

    void OnTrustedResumeFinalized(
        const std::shared_ptr<ResumeContext> &context, bool winner,
        const litebus::Future<::messages::SnapshotAttemptFinalizeResponse> &finalizeFuture);

    void ReleaseTrustedResumeLoserResources(const std::shared_ptr<ResumeContext> &context);

    void OnTrustedResumeLoserResourcesReleased(
        const std::shared_ptr<ResumeContext> &context,
        const litebus::Future<Status> &releaseFuture);

    void ClearCommittedResumeSnapshot(const std::shared_ptr<ResumeContext> &context);

    void OnCommittedResumeSnapshotCleared(
        const std::shared_ptr<ResumeContext> &context,
        const litebus::Future<TransitionResult> &transitionFuture);

    void CleanupTrustedResumeLoser(const std::shared_ptr<ResumeContext> &context, const Status &failure);

    void FinalizeTrustedResumeAttempt(const std::shared_ptr<ResumeContext> &context, bool winner);

    void CompleteTrustedResume(const std::shared_ptr<ResumeContext> &context, const Status &status);

    /**
     * Handle CreateInstanceClient completion
     * @param scheduleResp: Promise to return schedule response
     * @param scheduleReq: The schedule request with updated runtime info
     * @param clientResult: Client future result
     */
    void OnCreateInstanceClientComplete(
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleResp,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> &clientResult);

    /**
     * Handle SnapStarted RPC completion
     * @param scheduleResp: Promise to return schedule response
     * @param scheduleReq: The schedule request
     * @param snapStartedResult: SnapStarted RPC result
     */
    void OnSnapStartedRpcComplete(
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleResp,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const litebus::Future<runtime::SnapStartedResponse> &snapStartedResult);

    /**
     * Handle TransInstanceState completion
     * @param scheduleResp: Promise to return schedule response
     * @param scheduleReq: The schedule request
     * @param transResult: Transition result
     */
    void OnTransInstanceStateComplete(
        const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleResp,
        const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
        const litebus::Future<TransitionResult> &transResult);

    std::string nodeID_;
    PauseRetryPolicy pauseRetryPolicy_;

    std::shared_ptr<FunctionAgentMgr> functionAgentMgr_;
    std::shared_ptr<LocalSchedSrv> localSchedSrv_;
    std::shared_ptr<InstanceControlView> instanceControlView_;
    std::shared_ptr<ControlInterfaceClientManagerProxy> clientManager_;
    std::shared_ptr<InstanceCtrl> instanceCtrl_;
    std::shared_ptr<ActorWorker> snapshotWorker_;
    std::function<bool(const std::string &)> reusableSnapshotTunnelGateAcquire_;
    std::function<void(const std::string &)> reusableSnapshotTunnelGateRelease_;
    std::unordered_map<std::string, InstanceLifecycleState> instanceLifecycles_;
    uint64_t nextLifecycleGeneration_{ 1 };
    ResumeAttemptRegistry resumeAttempts_;
};

}  // namespace functionsystem::local_scheduler

#endif  // LOCAL_SCHEDULER_SNAP_CTRL_ACTOR_H
