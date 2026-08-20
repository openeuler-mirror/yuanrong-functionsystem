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

#include "snap_ctrl_actor.h"

#include <chrono>
#include <exception>
#include <nlohmann/json.hpp>

#include "async/async.hpp"
#include "async/defer.hpp"
#include "common/constants/actor_name.h"
#include "common/logs/logging.h"
#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix_pb.h"
#include "common/resource_view/resource_type.h"
#include "local_scheduler/function_agent_manager/function_agent_mgr.h"
#include "local_scheduler/instance_control/instance_ctrl_message.h"
#include "local_scheduler/local_scheduler_service/local_sched_srv.h"

namespace functionsystem::local_scheduler {

namespace {
constexpr uint64_t RESUME_RETRY_MS = 10;
}

litebus::Future<KillResponse> SnapCtrlActor::HandleSnapStart(const std::string &requestID,
                                                             const std::string &checkpointID,
                                                             const std::string &payload)
{
    // 1. 验证 checkpointID
    if (checkpointID.empty()) {
        YRLOG_ERROR("{}|HandleSnapStart: empty checkpointID", requestID);
        KillResponse errorRsp;
        errorRsp.set_code(static_cast<common::ErrorCode>(static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID)));
        errorRsp.set_message("empty checkpointID");
        return errorRsp;
    }

    // 2. 解析 SnapStartOptions payload
    SnapStartOptions options;
    if (!payload.empty() && !options.ParseFromString(payload)) {
        YRLOG_ERROR("{}|failed to parse SnapStartOptions payload", requestID);
        KillResponse errorRsp;
        errorRsp.set_code(static_cast<common::ErrorCode>(static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID)));
        errorRsp.set_message("invalid SnapStartOptions payload");
        return errorRsp;
    }

    YRLOG_INFO("{}|start snapstart from checkpoint: {}", requestID, checkpointID);

    // 3. 构造 RestoreSnapshotRequest
    auto req = std::make_shared<messages::RestoreSnapshotRequest>();
    req->set_requestid(requestID);
    req->set_checkpointid(checkpointID);
    *req->mutable_snapstartoptions() = options;

    // 4. 通过 localSchedSrv_ 转发到 function_master 的 ckpt_manager
    ASSERT_IF_NULL(localSchedSrv_);
    return localSchedSrv_->SnapStartCheckpoint(req).Then(
        [requestID, checkpointID](const messages::RestoreSnapshotResponse &rsp) -> KillResponse {
            KillResponse killRsp;
            killRsp.set_code(static_cast<common::ErrorCode>(rsp.code()));
            killRsp.set_message(rsp.message());

            if (rsp.code() == common::ERR_NONE) {
                YRLOG_INFO("{}|snapstart checkpoint {} succeeded, new instanceID: {}", requestID, checkpointID,
                           rsp.snapstartinfo().instanceid());
                if (!rsp.has_snapstartinfo() || rsp.snapstartinfo().instanceid().empty()) {
                    killRsp.set_code(common::ERR_INNER_SYSTEM_ERROR);
                    killRsp.set_message("snapstart response has no authoritative started instance");
                    return killRsp;
                }
                killRsp.set_payload(rsp.snapstartinfo().SerializeAsString());
            } else {
                YRLOG_ERROR("{}|snapstart checkpoint {} failed: {}", requestID, checkpointID, rsp.message());
            }

            return killRsp;
        });
}

void SnapCtrlActor::ReplayCommittedResumeFinalize(
    const std::shared_ptr<messages::ScheduleRequest> &sourceRequest,
    const resources::InstanceInfo &authoritative,
    const resume_identity::TrustedResumeIdentity &identity)
{
    const auto registration = resumeAttempts_.RegisterCommittedWinner(
        identity, sourceRequest, authoritative);
    if (registration.result == ResumeAttemptRegistration::Result::CONFLICT) {
        YRLOG_WARN("{}|ignore invalid committed Resume finalization replay",
                   identity.targetAttemptID);
        return;
    }
    if (registration.result == ResumeAttemptRegistration::Result::COALESCED) {
        YRLOG_INFO("{}|reuse in-flight committed Resume finalization replay",
                   identity.targetAttemptID);
        return;
    }
    CleanupTrustedResumeWinner(registration.context);
}

void SnapCtrlActor::SnapStart(
    const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleResp,
    const std::shared_ptr<messages::ScheduleRequest> &scheduleReq, const schedule_decision::ScheduleResult &result,
    const TransitionResult &transResult,
    const std::shared_ptr<const resume_identity::TrustedResumeIdentity> &trustedResumeIdentity)
{
    const auto &instanceID = scheduleReq->instance().instanceid();
    const auto &requestID = scheduleReq->requestid();

    YRLOG_INFO("{}|{}|SnapStarted: start snapstart instance initialization flow", requestID, instanceID);

    // todo(lwy) :Check transition result

    // 1. DeployInstance - call InstanceCtrl to deploy the snapstart instance
    ASSERT_IF_NULL(instanceCtrl_);
    YRLOG_INFO("{}|{}|calling DeploySnapStartInstance", requestID, instanceID);
    if (trustedResumeIdentity != nullptr) {
        ASSERT_IF_NULL(instanceControlView_);
        auto stateMachine = instanceControlView_->GetInstance(instanceID);
        if (stateMachine == nullptr
            || !trustedResumeIdentity->MatchesSchedule(*scheduleReq)) {
            scheduleResp->SetValue(GenScheduleResponse(StatusCode::SCHEDULE_CONFLICTED,
                                                       "trusted resume identity changed before materialize",
                                                       *scheduleReq));
            return;
        }
        const auto registration = resumeAttempts_.Register(
            *trustedResumeIdentity, stateMachine->GetInstanceInfo(), scheduleReq, scheduleResp);
        if (registration.result == ResumeAttemptRegistration::Result::COALESCED) {
            return;
        }
        if (registration.result == ResumeAttemptRegistration::Result::CONFLICT) {
            scheduleResp->SetValue(GenScheduleResponse(StatusCode::SCHEDULE_CONFLICTED,
                                                       "trusted resume replay conflicts with active attempt",
                                                       *scheduleReq));
            return;
        }
        const auto &context = registration.context;
        instanceCtrl_->DeploySnapStartInstance(context->allocatedRequest, context->identity)
            .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnDeploySnapStartInstanceComplete,
                                       context->completions.front(), context->allocatedRequest,
                                       std::make_shared<const resume_identity::TrustedResumeIdentity>(
                                           context->identity),
                                       context, std::placeholders::_1));
        return;
    }
    instanceCtrl_->DeploySnapStartInstance(scheduleReq)
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnDeploySnapStartInstanceComplete, scheduleResp,
                                   scheduleReq, trustedResumeIdentity, nullptr, std::placeholders::_1));
}

void SnapCtrlActor::OnDeploySnapStartInstanceComplete(
    const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleResp,
    const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
    const std::shared_ptr<const resume_identity::TrustedResumeIdentity> &trustedResumeIdentity,
    const std::shared_ptr<ResumeContext> &resumeContext,
    const litebus::Future<messages::DeployInstanceResponse> &deployFuture)
{
    const auto &instanceID = scheduleReq->instance().instanceid();
    const auto &requestID = scheduleReq->requestid();

    if (trustedResumeIdentity != nullptr
        && !resumeAttempts_.IsCurrent(resumeContext, ResumePhase::DEPLOYING)) {
        return;
    }
    if (deployFuture.IsError()) {
        YRLOG_ERROR("{}|{}|DeploySnapStartInstance future failed, error code: {}", requestID, instanceID,
                    deployFuture.GetErrorCode());
        if (resumeContext != nullptr) {
            (void)litebus::AsyncAfter(RESUME_RETRY_MS, GetAID(),
                                      &SnapCtrlActor::RetryTrustedResumeAfterResultUnknown,
                                      resumeContext);
        } else {
            scheduleResp->SetValue(GenScheduleResponse(StatusCode::FAILED,
                                                       "DeploySnapStartInstance failed", *scheduleReq));
        }
        return;
    }

    const auto &deployResponse = deployFuture.Get();
    if (deployResponse.code() != 0) {
        YRLOG_ERROR("{}|{}|deploy snapstart instance failed, code: {}, message: {}", requestID, instanceID,
                    deployResponse.code(), deployResponse.message());
        if (resumeContext != nullptr) {
            if (resume_identity::IsResultUnknownStatusCode(deployResponse.code())) {
                (void)litebus::AsyncAfter(RESUME_RETRY_MS, GetAID(),
                                          &SnapCtrlActor::RetryTrustedResumeAfterResultUnknown,
                                          resumeContext);
                return;
            }
            CleanupTrustedResumeLoser(resumeContext,
                Status(static_cast<StatusCode>(deployResponse.code()), deployResponse.message()));
        } else {
            scheduleResp->SetValue(GenScheduleResponse(static_cast<StatusCode>(deployResponse.code()),
                                                       deployResponse.message(), *scheduleReq));
        }
        return;
    }

    const auto &runtimeID = deployResponse.runtimeid();
    const auto &address = deployResponse.address();
    YRLOG_INFO("{}|{}|deploy snapstart instance succeeded, runtimeID: {}, address: {}", requestID, instanceID,
               runtimeID, address);

    // Update scheduleReq with runtime details
    scheduleReq->mutable_instance()->set_runtimeid(runtimeID);
    scheduleReq->mutable_instance()->set_runtimeaddress(address);
    scheduleReq->mutable_instance()->set_executortype(deployResponse.executortype());
    scheduleReq->mutable_instance()->set_containerid(deployResponse.containerid());
    scheduleReq->mutable_instance()->set_containerip(deployResponse.containerip());
    scheduleReq->mutable_instance()->set_starttime(deployResponse.timeinfo());
    (*scheduleReq->mutable_instance()->mutable_extensions())["PID"] = std::to_string(deployResponse.pid());
    scheduleReq->mutable_instance()->mutable_extensions()->erase("portForward");
    if (!deployResponse.portmappings().empty()) {
        (*scheduleReq->mutable_instance()->mutable_extensions())["portForward"] = deployResponse.portmappings();
    }

    // Combined R2/R3 stops after materialization and Restore dispatch/convergence.
    // R4 owns SnapStarted, readiness, the final PAUSED->RUNNING CAS, and confirmed-winner cleanup.
    if (trustedResumeIdentity != nullptr) {
        resumeContext->candidate = scheduleReq->instance();
        const auto deterministicRuntimeID = resume_identity::RuntimeID(
            trustedResumeIdentity->logicalInstanceID, trustedResumeIdentity->targetAttemptID);
        if (resumeContext->candidate.runtimeid() != deterministicRuntimeID) {
            CleanupTrustedResumeLoser(resumeContext,
                Status(StatusCode::SCHEDULE_CONFLICTED,
                       "deployed resume runtime ID is not deterministic"));
            return;
        }
        resumeContext->phase = ResumePhase::CREATING_CLIENT;
        instanceCtrl_->CreateInstanceClient(instanceID, runtimeID, address)
            .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnTrustedResumeClientCreated,
                                       resumeContext, std::placeholders::_1));
        return;
    }

    // 2. CreateInstanceClient
    ASSERT_IF_NULL(instanceCtrl_);
    YRLOG_INFO("{}|{}|creating instance client", requestID, instanceID);
    instanceCtrl_->CreateInstanceClient(instanceID, runtimeID, address)
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnCreateInstanceClientComplete, scheduleResp, scheduleReq,
                                   std::placeholders::_1));
}

void SnapCtrlActor::RetryTrustedResumeAfterResultUnknown(
    const std::shared_ptr<ResumeContext> &context)
{
    if (!resumeAttempts_.IsCurrent(context, ResumePhase::DEPLOYING)) {
        return;
    }
    ASSERT_IF_NULL(instanceControlView_);
    auto stateMachine = instanceControlView_->GetInstance(context->identity.logicalInstanceID);
    if (stateMachine == nullptr
        || !context->identity.MatchesAuthoritative(stateMachine->GetInstanceInfo())) {
        CleanupTrustedResumeLoser(
            context,
            Status(StatusCode::SCHEDULE_CONFLICTED,
                   "trusted resume generation changed while Restore result was unknown"));
        return;
    }
    instanceCtrl_->DeploySnapStartInstance(context->allocatedRequest, context->identity)
        .OnComplete(litebus::Defer(
            GetAID(), &SnapCtrlActor::OnDeploySnapStartInstanceComplete,
            context->completions.front(), context->allocatedRequest,
            std::make_shared<const resume_identity::TrustedResumeIdentity>(context->identity),
            context, std::placeholders::_1));
}

void SnapCtrlActor::OnTrustedResumeClientCreated(
    const std::shared_ptr<ResumeContext> &context,
    const litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> &clientFuture)
{
    if (!resumeAttempts_.IsCurrent(context, ResumePhase::CREATING_CLIENT)) {
        return;
    }
    ASSERT_IF_NULL(instanceControlView_);
    auto stateMachine = instanceControlView_->GetInstance(context->identity.logicalInstanceID);
    if (stateMachine == nullptr
        || !context->identity.MatchesAuthoritative(stateMachine->GetInstanceInfo())) {
        CleanupTrustedResumeLoser(context,
            Status(StatusCode::SCHEDULE_CONFLICTED,
                   "authoritative paused identity changed before client completion"));
        return;
    }
    if (clientFuture.IsError() || clientFuture.Get() == nullptr) {
        CleanupTrustedResumeLoser(context,
            Status(StatusCode::FAILED, "failed to create trusted resume client"));
        return;
    }
    context->phase = ResumePhase::STARTING;
    runtime::SnapStartedRequest request;
    clientFuture.Get()->SnapStarted(std::move(request))
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnTrustedResumeSnapStarted,
                                   context, std::placeholders::_1));
}

void SnapCtrlActor::OnTrustedResumeSnapStarted(
    const std::shared_ptr<ResumeContext> &context,
    const litebus::Future<runtime::SnapStartedResponse> &startedFuture)
{
    if (!resumeAttempts_.IsCurrent(context, ResumePhase::STARTING)) {
        return;
    }
    ASSERT_IF_NULL(instanceControlView_);
    auto stateMachine = instanceControlView_->GetInstance(context->identity.logicalInstanceID);
    if (stateMachine == nullptr
        || !context->identity.MatchesAuthoritative(stateMachine->GetInstanceInfo())) {
        CleanupTrustedResumeLoser(context,
            Status(StatusCode::SCHEDULE_CONFLICTED,
                   "authoritative paused identity changed before SnapStarted completion"));
        return;
    }
    if (startedFuture.IsError()) {
        CleanupTrustedResumeLoser(context,
            Status(StatusCode::FAILED, "trusted resume SnapStarted future failed"));
        return;
    }
    const auto &started = startedFuture.Get();
    if (started.code() != common::ERR_NONE) {
        CleanupTrustedResumeLoser(context,
            Status(static_cast<StatusCode>(started.code()),
                   "trusted resume SnapStarted failed: " + started.message()));
        return;
    }
    CommitTrustedResume(context);
}

void SnapCtrlActor::CommitTrustedResume(const std::shared_ptr<ResumeContext> &context)
{
    if (!resumeAttempts_.IsCurrent(context, ResumePhase::STARTING)) {
        return;
    }
    ASSERT_IF_NULL(instanceControlView_);
    auto stateMachine = instanceControlView_->GetInstance(context->identity.logicalInstanceID);
    if (stateMachine == nullptr
        || !context->identity.MatchesAuthoritative(stateMachine->GetInstanceInfo())) {
        CleanupTrustedResumeLoser(context,
            Status(StatusCode::SCHEDULE_CONFLICTED,
                   "authoritative paused identity changed immediately before running CAS"));
        return;
    }
    context->runningRequest = std::make_shared<messages::ScheduleRequest>(*context->allocatedRequest);
    auto *candidate = context->runningRequest->mutable_instance();
    candidate->mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
    *candidate->mutable_snapshotinfo() = context->identity.snapshot;
    TransContext transition{ InstanceState::RUNNING, context->identity.expectedVersion, "running" };
    transition.scheduleReq = context->runningRequest;
    context->phase = ResumePhase::COMMITTING;
    instanceCtrl_->TransInstanceState(stateMachine, transition)
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnTrustedResumeCommitted,
                                   context, std::placeholders::_1));
}

void SnapCtrlActor::OnTrustedResumeCommitted(
    const std::shared_ptr<ResumeContext> &context,
    const litebus::Future<TransitionResult> &transitionFuture)
{
    if (!resumeAttempts_.IsCurrent(context, ResumePhase::COMMITTING)) {
        return;
    }
    if (!transitionFuture.IsError() && transitionFuture.Get().status.IsOk()) {
        ConfirmTrustedResumeWinner(context);
        return;
    }
    ReconcileTrustedResume(context, transitionFuture.IsError());
}

void SnapCtrlActor::ReconcileTrustedResume(const std::shared_ptr<ResumeContext> &context,
                                           bool resultUnknown)
{
    if (!resumeAttempts_.IsCurrent(context, ResumePhase::COMMITTING)
        && !resumeAttempts_.IsCurrent(context, ResumePhase::RECONCILING)) {
        return;
    }
    ASSERT_IF_NULL(instanceControlView_);
    auto stateMachine = instanceControlView_->GetInstance(context->identity.logicalInstanceID);
    if (stateMachine == nullptr) {
        CleanupTrustedResumeLoser(context,
            Status(StatusCode::ERR_INSTANCE_NOT_FOUND,
                   "resume state machine disappeared during CAS reconciliation"));
        return;
    }
    context->phase = ResumePhase::RECONCILING;
    stateMachine->SyncInstanceFromMetaStore()
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnTrustedResumeReconciled,
                                   context, resultUnknown, std::placeholders::_1));
}

void SnapCtrlActor::OnTrustedResumeReconciled(
    const std::shared_ptr<ResumeContext> &context, bool resultUnknown,
    const litebus::Future<resources::InstanceInfo> &syncFuture)
{
    if (!resumeAttempts_.IsCurrent(context, ResumePhase::RECONCILING)) {
        return;
    }
    if (syncFuture.IsError()) {
        (void)litebus::AsyncAfter(RESUME_RETRY_MS, GetAID(),
                                  &SnapCtrlActor::ReconcileTrustedResume, context, resultUnknown);
        return;
    }
    const auto &authoritative = syncFuture.Get();
    if (resumeAttempts_.IsExactWinner(context, authoritative)) {
        ConfirmTrustedResumeWinner(context, &authoritative);
        return;
    }
    if (resultUnknown && context->identity.MatchesAuthoritative(authoritative)) {
        context->phase = ResumePhase::STARTING;
        CommitTrustedResume(context);
        return;
    }
    CleanupTrustedResumeLoser(context,
        Status(StatusCode::SCHEDULE_CONFLICTED,
               "authoritative state does not identify this resume candidate as winner"));
}

void SnapCtrlActor::ConfirmTrustedResumeWinner(
    const std::shared_ptr<ResumeContext> &context,
    const resources::InstanceInfo *authoritative)
{
    if (!resumeAttempts_.IsCurrent(context, ResumePhase::COMMITTING)
        && !resumeAttempts_.IsCurrent(context, ResumePhase::RECONCILING)) {
        return;
    }
    if (authoritative != nullptr) {
        ASSERT_IF_NULL(instanceControlView_);
        auto stateMachine = instanceControlView_->GetInstance(context->identity.logicalInstanceID);
        const auto current = stateMachine == nullptr
            ? resources::InstanceInfo{} : stateMachine->GetInstanceInfo();
        if (stateMachine == nullptr
            || (!context->identity.MatchesAuthoritative(current)
                && !resumeAttempts_.IsExactWinner(context, current))) {
            CleanupTrustedResumeLoser(context,
                Status(StatusCode::SCHEDULE_CONFLICTED,
                       "resume winner callback no longer matches current local generation"));
            return;
        }
        stateMachine->UpdateInstanceInfo(*authoritative);
        stateMachine->SetVersion(authoritative->version());
    } else {
        ASSERT_IF_NULL(instanceControlView_);
        auto stateMachine = instanceControlView_->GetInstance(context->identity.logicalInstanceID);
        if (stateMachine == nullptr
            || !resumeAttempts_.IsExactWinner(context, stateMachine->GetInstanceInfo())) {
            CleanupTrustedResumeLoser(context,
                Status(StatusCode::SCHEDULE_CONFLICTED,
                       "successful resume CAS callback no longer matches current winner"));
            return;
        }
    }
    BeginTrustedResumeWinnerCleanup(context);
}

void SnapCtrlActor::BeginTrustedResumeWinnerCleanup(const std::shared_ptr<ResumeContext> &context)
{
    context->phase = ResumePhase::WINNER_CLEANUP;
    if (!context->heartbeatStarted) {
        context->heartbeatStarted = true;
        instanceCtrl_->StartHeartbeat(context->identity.logicalInstanceID, 0,
                                      context->candidate.runtimeid(), StatusCode::SUCCESS);
    }
    CleanupTrustedResumeWinner(context);
}

void SnapCtrlActor::CleanupTrustedResumeWinner(const std::shared_ptr<ResumeContext> &context)
{
    if (!resumeAttempts_.IsCurrent(context, ResumePhase::WINNER_CLEANUP)) {
        return;
    }
    FinalizeTrustedResumeAttempt(context, true);
}

void SnapCtrlActor::OnTrustedResumeFinalized(
    const std::shared_ptr<ResumeContext> &context, bool winner,
    const litebus::Future<::messages::SnapshotAttemptFinalizeResponse> &finalizeFuture)
{
    const auto expected = winner ? ResumePhase::WINNER_CLEANUP : ResumePhase::LOSER_CLEANUP;
    if (!resumeAttempts_.IsCurrent(context, expected)) {
        return;
    }
    if (!winner) {
        if (finalizeFuture.IsError()
            || finalizeFuture.Get().code() != static_cast<int32_t>(StatusCode::SUCCESS)
            || finalizeFuture.Get().resultunknown()
            || !finalizeFuture.Get().localcleanupcomplete()) {
            (void)litebus::AsyncAfter(RESUME_RETRY_MS, GetAID(),
                                      &SnapCtrlActor::FinalizeTrustedResumeAttempt,
                                      context, false);
            return;
        }
        ReleaseTrustedResumeLoserResources(context);
        return;
    }
    if (finalizeFuture.IsError()
        || finalizeFuture.Get().code() != static_cast<int32_t>(StatusCode::SUCCESS)
        || finalizeFuture.Get().resultunknown()
        || !finalizeFuture.Get().remotecleanupcomplete()) {
        CompleteTrustedResume(context, Status::OK());
        return;
    }
    ClearCommittedResumeSnapshot(context);
}

void SnapCtrlActor::ReleaseTrustedResumeLoserResources(
    const std::shared_ptr<ResumeContext> &context)
{
    if (!resumeAttempts_.IsCurrent(context, ResumePhase::LOSER_CLEANUP)) {
        return;
    }
    if (instanceCtrl_ == nullptr) {
        (void)litebus::AsyncAfter(RESUME_RETRY_MS, GetAID(),
                                  &SnapCtrlActor::ReleaseTrustedResumeLoserResources,
                                  context);
        return;
    }
    instanceCtrl_->ReleasePausedInstanceResources(context->allocatedRequest->instance())
        .OnComplete(litebus::Defer(GetAID(),
                                   &SnapCtrlActor::OnTrustedResumeLoserResourcesReleased,
                                   context, std::placeholders::_1));
}

void SnapCtrlActor::OnTrustedResumeLoserResourcesReleased(
    const std::shared_ptr<ResumeContext> &context,
    const litebus::Future<Status> &releaseFuture)
{
    if (!resumeAttempts_.IsCurrent(context, ResumePhase::LOSER_CLEANUP)) {
        return;
    }
    if (releaseFuture.IsError() || releaseFuture.Get().IsError()) {
        (void)litebus::AsyncAfter(RESUME_RETRY_MS, GetAID(),
                                  &SnapCtrlActor::ReleaseTrustedResumeLoserResources,
                                  context);
        return;
    }
    CompleteTrustedResume(context, context->failure);
}

void SnapCtrlActor::CleanupTrustedResumeLoser(
    const std::shared_ptr<ResumeContext> &context, const Status &failure)
{
    if (context == nullptr) {
        return;
    }
    if (!resumeAttempts_.Contains(context)) {
        return;
    }
    context->failure = failure;
    context->phase = ResumePhase::LOSER_CLEANUP;
    FinalizeTrustedResumeAttempt(context, false);
}

void SnapCtrlActor::FinalizeTrustedResumeAttempt(const std::shared_ptr<ResumeContext> &context,
                                                 bool winner)
{
    const auto expected = winner ? ResumePhase::WINNER_CLEANUP : ResumePhase::LOSER_CLEANUP;
    if (!resumeAttempts_.IsCurrent(context, expected)) {
        return;
    }
    if (functionAgentMgr_ == nullptr) {
        CompleteTrustedResume(context, winner ? Status::OK() : context->failure);
        return;
    }
    ::messages::SnapshotAttemptFinalizeRequest request;
    request.set_protocolversion(1);
    request.set_operation(winner ? ::messages::RESUME_COMMITTED : ::messages::RESUME_ABORTED);
    request.set_tenantid(context->identity.tenantID);
    request.set_instanceid(context->identity.logicalInstanceID);
    request.set_snapshotid(context->identity.snapshot.checkpointid());
    request.set_attemptid(context->identity.targetAttemptID);
    request.set_runtimeid(context->candidate.runtimeid().empty()
        ? resume_identity::RuntimeID(context->identity.logicalInstanceID,
                                     context->identity.targetAttemptID)
        : context->candidate.runtimeid());
    request.set_expectedsize(context->identity.snapshot.size());
    request.set_expectedsha256(context->identity.snapshot.sha256());
    request.set_expectedstorage(context->identity.snapshot.storage());
    const auto &target = context->candidate.functionagentid().empty()
        ? context->allocatedRequest->instance() : context->candidate;
    functionAgentMgr_->FinalizeSnapshotAttempt(target, request)
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnTrustedResumeFinalized,
                                   context, winner, std::placeholders::_1));
}

void SnapCtrlActor::ClearCommittedResumeSnapshot(const std::shared_ptr<ResumeContext> &context)
{
    if (!resumeAttempts_.IsCurrent(context, ResumePhase::WINNER_CLEANUP)) {
        return;
    }
    ASSERT_IF_NULL(instanceControlView_);
    auto stateMachine = instanceControlView_->GetInstance(context->identity.logicalInstanceID);
    if (stateMachine == nullptr) {
        CompleteTrustedResume(context, Status::OK());
        return;
    }
    const auto current = stateMachine->GetInstanceInfo();
    if (!resumeAttempts_.IsExactWinner(context, current)
        || !current.has_snapshotinfo()
        || !resume_identity::SnapshotIdentityMatches(
            current.snapshotinfo(), context->identity.snapshot)) {
        CompleteTrustedResume(context, Status::OK());
        return;
    }
    auto clearRequest = std::make_shared<messages::ScheduleRequest>();
    clearRequest->set_requestid(context->identity.targetAttemptID);
    clearRequest->mutable_instance()->CopyFrom(current);
    clearRequest->mutable_instance()->clear_snapshotinfo();
    TransContext transition{ InstanceState::RUNNING, current.version(), "resume snapshot cleaned" };
    transition.scheduleReq = clearRequest;
    context->phase = ResumePhase::CLEARING_SNAPSHOT;
    instanceCtrl_->TransInstanceState(stateMachine, transition)
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnCommittedResumeSnapshotCleared,
                                   context, std::placeholders::_1));
}

void SnapCtrlActor::OnCommittedResumeSnapshotCleared(
    const std::shared_ptr<ResumeContext> &context,
    const litebus::Future<TransitionResult> &)
{
    if (!resumeAttempts_.IsCurrent(context, ResumePhase::CLEARING_SNAPSHOT)) {
        return;
    }
    CompleteTrustedResume(context, Status::OK());
}

void SnapCtrlActor::CompleteTrustedResume(const std::shared_ptr<ResumeContext> &context,
                                          const Status &status)
{
    auto completions = resumeAttempts_.Remove(context);
    if (!completions.has_value()) {
        return;
    }
    auto response = GenScheduleResponse(status.IsOk() ? StatusCode::SUCCESS : status.StatusCode(),
                                        status.IsOk() ? "success" : status.RawMessage(),
                                        *context->allocatedRequest);
    for (const auto &completion : *completions) {
        completion->SetValue(response);
    }
}

void SnapCtrlActor::OnCreateInstanceClientComplete(
    const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleResp,
    const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
    const litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> &clientResult)
{
    const auto &instanceID = scheduleReq->instance().instanceid();
    const auto &requestID = scheduleReq->requestid();
    const auto &runtimeID = scheduleReq->instance().runtimeid();

    if (clientResult.IsError() || clientResult.Get() == nullptr) {
        YRLOG_ERROR("{}|{}|failed to create instance client, error code: {}", requestID, instanceID,
                    clientResult.GetErrorCode());
        scheduleResp->SetValue(
            GenScheduleResponse(StatusCode::FAILED, "failed to create instance client", *scheduleReq));
        return;
    }

    auto client = clientResult.Get();
    YRLOG_INFO("{}|{}|instance client created successfully", requestID, instanceID);

    // 3. StartHeartbeat
    ASSERT_IF_NULL(instanceCtrl_);
    YRLOG_INFO("{}|{}|starting heartbeat for snapstart instance", requestID, instanceID);
    instanceCtrl_->StartHeartbeat(instanceID, 0, runtimeID, StatusCode::SUCCESS);

    // 4. Call SnapStarted RPC
    YRLOG_INFO("{}|{}|calling SnapStarted RPC on runtime", requestID, instanceID);
    runtime::SnapStartedRequest snapStartedReq{};
    client->SnapStarted(std::move(snapStartedReq))
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnSnapStartedRpcComplete, scheduleResp, scheduleReq,
                                   std::placeholders::_1));
}

void SnapCtrlActor::OnSnapStartedRpcComplete(
    const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleResp,
    const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
    const litebus::Future<runtime::SnapStartedResponse> &snapStartedResult)
{
    const auto &instanceID = scheduleReq->instance().instanceid();
    const auto &requestID = scheduleReq->requestid();

    if (snapStartedResult.IsError()) {
        YRLOG_ERROR("{}|{}|SnapStarted RPC failed, error code: {}", requestID, instanceID,
                    snapStartedResult.GetErrorCode());
        scheduleResp->SetValue(GenScheduleResponse(StatusCode::FAILED, "SnapStarted RPC failed", *scheduleReq));
        return;
    }

    auto response = snapStartedResult.Get();
    if (response.code() != common::ERR_NONE) {
        YRLOG_ERROR("{}|{}|SnapStarted RPC returned error: code={}, message={}", requestID, instanceID, response.code(),
                    response.message());
        scheduleResp->SetValue(
            GenScheduleResponse(static_cast<StatusCode>(response.code()), response.message(), *scheduleReq));
        return;
    }

    YRLOG_INFO("{}|{}|SnapStarted RPC succeeded", requestID, instanceID);

    // 5. TransInstanceState to RUNNING
    ASSERT_IF_NULL(instanceControlView_);
    auto stateMachine = instanceControlView_->GetInstance(instanceID);
    if (stateMachine == nullptr) {
        YRLOG_ERROR("{}|{}|failed to get instance state machine", requestID, instanceID);
        scheduleResp->SetValue(
            GenScheduleResponse(StatusCode::ERR_INSTANCE_NOT_FOUND, "instance state machine not found", *scheduleReq));
        return;
    }

    YRLOG_INFO("{}|{}|transitioning instance state to RUNNING", requestID, instanceID);
    TransContext transContext{ InstanceState::RUNNING, stateMachine->GetVersion(), "running" };
    transContext.scheduleReq = scheduleReq;

    ASSERT_IF_NULL(instanceCtrl_);
    instanceCtrl_->TransInstanceState(stateMachine, transContext)
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnTransInstanceStateComplete, scheduleResp, scheduleReq,
                                   std::placeholders::_1));
}

void SnapCtrlActor::OnTransInstanceStateComplete(
    const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> scheduleResp,
    const std::shared_ptr<messages::ScheduleRequest> &scheduleReq, const litebus::Future<TransitionResult> &transResult)
{
    const auto &instanceID = scheduleReq->instance().instanceid();
    const auto &requestID = scheduleReq->requestid();

    if (transResult.IsError()) {
        YRLOG_ERROR("{}|{}|failed to transition instance to RUNNING state, error code: {}", requestID, instanceID,
                    transResult.GetErrorCode());
        scheduleResp->SetValue(
            GenScheduleResponse(StatusCode::ERR_ETCD_OPERATION_ERROR, "failed to update instance state", *scheduleReq));
        return;
    }

    const auto &result = transResult.Get();
    if (result.status.IsError()) {
        YRLOG_ERROR("{}|{}|failed to transition instance to RUNNING state: {}", requestID, instanceID,
                    result.status.GetMessage());
        scheduleResp->SetValue(
            GenScheduleResponse(result.status.StatusCode(), result.status.GetMessage(), *scheduleReq));
        return;
    }

    // 6. SetValue to complete schedule
    YRLOG_INFO("{}|{}|snapstart instance initialized successfully, state: RUNNING", requestID, instanceID);
    scheduleResp->SetValue(GenScheduleResponse(StatusCode::SUCCESS, "success", *scheduleReq));
}

}  // namespace functionsystem::local_scheduler
