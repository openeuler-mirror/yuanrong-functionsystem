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

#include <algorithm>
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
constexpr uint64_t PAUSE_PHYSICAL_FACT_RETRY_MS = 10;

bool MatchesPauseSourcePhysicalIdentity(const resources::InstanceInfo &source,
                                        const resources::InstanceInfo &latest)
{
    return source.instanceid() == latest.instanceid()
           && source.requestid() == latest.requestid()
           && source.version() == latest.version()
           && source.functionproxyid() == latest.functionproxyid()
           && source.runtimeid() == latest.runtimeid()
           && source.functionagentid() == latest.functionagentid()
           && source.containerid() == latest.containerid()
           && source.unitid() == latest.unitid()
           && source.tenantid() == latest.tenantid()
           && source.runtimeaddress() == latest.runtimeaddress();
}

bool IsExactPausedCommit(const resources::InstanceInfo &source,
                         const resources::InstanceInfo &latest,
                         const std::string &snapshotID, const std::string &storageBackend,
                         uint64_t publishedSize, const std::string &publishedSha256,
                         const std::string &createTime, int32_t ttlSeconds)
{
    if (static_cast<InstanceState>(latest.instancestatus().code()) != InstanceState::PAUSED
        || source.instanceid() != latest.instanceid()
        || source.requestid() != latest.requestid()
        || source.version() + 1 != latest.version()
        || source.tenantid() != latest.tenantid()
        || !resume_identity::IsAuthoritativePausedControlIdentity(latest)
        || !latest.has_snapshotinfo()) {
        return false;
    }
    const auto &snapshot = latest.snapshotinfo();
    return snapshot.checkpointid() == snapshotID
           && snapshot.storage() == storageBackend
           && snapshot.size() == static_cast<int64_t>(publishedSize)
           && snapshot.sha256() == publishedSha256
           && snapshot.createtime() == createTime
           && snapshot.ttlseconds() == ttlSeconds
           && snapshot.status() == resources::SNAPSHOT_READY
           && snapshot.sourcenodeid() == source.functionproxyid();
}

}

litebus::Future<KillResponse> SnapCtrlActor::HandlePauseResumeSnapshot(
    const std::string &requestID, const std::string &instanceID,
    const std::shared_ptr<InstanceStateMachine> &stateMachine, int32_t ttlSeconds,
    uint64_t checkpointTimeoutMs)
{
    const auto instanceInfo = stateMachine->GetInstanceInfo();
    const auto state = static_cast<InstanceState>(instanceInfo.instancestatus().code());
    if (instanceInfo.instanceid() != instanceID || instanceInfo.requestid().empty()) {
        YRLOG_ERROR("{}|{}|pause instance identity is incomplete or mismatched", requestID, instanceID);
        KillResponse response;
        response.set_code(common::ERR_STATE_MACHINE_ERROR);
        response.set_message("pause instance identity is incomplete or mismatched");
        return response;
    }
    if (auto operation = instanceLifecycles_.find(instanceID);
        operation != instanceLifecycles_.end()
        && operation->second.phase == InstanceLifecyclePhase::PREPARING_DELETE) {
        KillResponse response;
        response.set_code(common::ERR_STATE_MACHINE_ERROR);
        response.set_message("authorized instance deletion is in progress");
        return response;
    }
    if (state == InstanceState::PAUSED) {
        KillResponse response;
        const bool validSnapshot = resume_identity::IsAuthoritativePausedControlIdentity(instanceInfo)
                                   && instanceInfo.has_snapshotinfo()
                                   && resume_identity::IsCompleteReadySnapshot(instanceInfo.snapshotinfo())
                                   && instanceInfo.snapshotinfo().checkpointid() == requestID;
        response.set_code(validSnapshot ? common::ERR_NONE : common::ERR_STATE_MACHINE_ERROR);
        if (!validSnapshot) {
            response.set_message("paused instance has invalid snapshot metadata");
            return response;
        }
        if (auto iter = instanceLifecycles_.find(instanceID); iter != instanceLifecycles_.end()) {
            const auto &pauseContext = iter->second.pauseContext;
            if (pauseContext != nullptr && pauseContext->operationRequestID == requestID) {
                return pauseContext->completion->GetFuture();
            }
            response.set_code(common::ERR_STATE_MACHINE_ERROR);
            response.set_message("another pause finalization is in progress");
            return response;
        }
        const auto &snapshot = instanceInfo.snapshotinfo();
        auto context = std::make_shared<PauseContext>();
        context->operationRequestID = requestID;
        context->sourceInstanceInfo = instanceInfo;
        context->ttlSeconds = snapshot.ttlseconds();
        context->snapshotCreateTime = snapshot.createtime();
        context->storageBackend = snapshot.storage();
        context->publishedSize = static_cast<uint64_t>(snapshot.size());
        context->publishedSha256 = snapshot.sha256();
        context->published = true;
        context->phase = PausePhase::CONVERGING;
        try {
            (void)std::stoll(snapshot.createtime());
        } catch (const std::exception &) {
            response.set_code(common::ERR_STATE_MACHINE_ERROR);
            response.set_message("paused snapshot creation time is invalid");
            return response;
        }
        context->completion = std::make_shared<litebus::Promise<KillResponse>>();
        instanceLifecycles_.emplace(
            instanceID,
            InstanceLifecycleState{ InstanceLifecyclePhase::PAUSING, nextLifecycleGeneration_++, context,
                                    nullptr });
        FinalizePauseAfterResourceRelease(instanceID, context)
            .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnPauseCheckpointComplete,
                                       instanceID, context, std::placeholders::_1));
        return context->completion->GetFuture();
    }
    if (state != InstanceState::RUNNING) {
        YRLOG_ERROR("{}|{}|pause requires RUNNING instance, current state: {}", requestID, instanceID,
                    instanceInfo.instancestatus().code());
        KillResponse response;
        response.set_code(common::ERR_STATE_MACHINE_ERROR);
        response.set_message("pause requires a running instance");
        return response;
    }
    if (instanceInfo.functionproxyid() != nodeID_) {
        YRLOG_ERROR("{}|{}|pause owner mismatch, expected: {}, actual: {}", requestID, instanceID, nodeID_,
                    instanceInfo.functionproxyid());
        KillResponse response;
        response.set_code(common::ERR_STATE_MACHINE_ERROR);
        response.set_message("pause request reached a non-owner proxy");
        return response;
    }

    if (auto iter = instanceLifecycles_.find(instanceID); iter != instanceLifecycles_.end()) {
        const auto &pauseContext = iter->second.pauseContext;
        if (iter->second.phase == InstanceLifecyclePhase::PAUSING && pauseContext != nullptr
            && pauseContext->operationRequestID == requestID
            && MatchesPauseSourcePhysicalIdentity(pauseContext->sourceInstanceInfo, instanceInfo)) {
            YRLOG_INFO("{}|{}|reuse in-flight pause attempt", requestID, instanceID);
            return pauseContext->completion->GetFuture();
        }
        KillResponse response;
        response.set_code(common::ERR_STATE_MACHINE_ERROR);
        response.set_message("instance identity changed during pause");
        return response;
    }

    auto context = std::make_shared<PauseContext>();
    context->operationRequestID = requestID;
    context->sourceInstanceInfo = instanceInfo;
    if (checkpointTimeoutMs > 0) {
        (*context->sourceInstanceInfo.mutable_createoptions())["YR_CHECKPOINT_TIMEOUT_MS"] =
            std::to_string(checkpointTimeoutMs);
    }
    context->ttlSeconds = ttlSeconds;
    context->prepareDeadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(pauseRetryPolicy_.operationTimeoutMs);
    context->prepareRetryDelayMs = pauseRetryPolicy_.initialDelayMs;
    context->completion = std::make_shared<litebus::Promise<KillResponse>>();
    instanceLifecycles_.emplace(
        instanceID,
        InstanceLifecycleState{ InstanceLifecyclePhase::PAUSING, nextLifecycleGeneration_++, context, nullptr });
    if (instanceCtrl_ == nullptr) {
        KillResponse response;
        response.set_code(common::ERR_INNER_SYSTEM_ERROR);
        response.set_message("instance control is not available for pause gate");
        CompletePauseContext(instanceID, context, response);
        return context->completion->GetFuture();
    }
    if (instanceInfo.has_snapshotinfo()) {
        DeletePauseSnapshot(instanceInfo).OnComplete(
            litebus::Defer(GetAID(), [this, instanceID, context](const litebus::Future<Status> &future) {
                if (future.IsError() || future.Get().IsError()) {
                    KillResponse response;
                    response.set_code(common::ERR_LOCAL_SCHEDULER_OPERATION_ERROR);
                    response.set_message(future.IsError()
                                             ? "retained pause snapshot cleanup future failed"
                                             : future.Get().RawMessage());
                    CompletePauseContext(instanceID, context, response);
                    return;
                }
                auto validation = ValidatePauseContinuation(instanceID, context);
                if (validation.IsError()) {
                    KillResponse response;
                    response.set_code(common::ERR_LOCAL_SCHEDULER_OPERATION_ERROR);
                    response.set_message(validation.RawMessage());
                    CompletePauseContext(instanceID, context, response);
                    return;
                }
                instanceCtrl_->BeginPauseGate(context->sourceInstanceInfo)
                    .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnPauseGateComplete,
                                               instanceID, context, std::placeholders::_1));
            }));
        return context->completion->GetFuture();
    }
    instanceCtrl_->BeginPauseGate(instanceInfo)
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnPauseGateComplete, instanceID, context,
                                   std::placeholders::_1));
    return context->completion->GetFuture();
}

litebus::Future<DeletePreparation> SnapCtrlActor::PrepareForAuthorizedDelete(const std::string &instanceID)
{
    auto iter = instanceLifecycles_.find(instanceID);
    if (iter != instanceLifecycles_.end()
        && iter->second.phase == InstanceLifecyclePhase::PREPARING_DELETE) {
        return iter->second.deletePreparation->GetFuture();
    }
    const auto generation = iter == instanceLifecycles_.end()
        ? nextLifecycleGeneration_++ : iter->second.generation;
    auto preparation = std::make_shared<litebus::Promise<DeletePreparation>>();
    if (iter == instanceLifecycles_.end()) {
        instanceLifecycles_.emplace(
            instanceID,
            InstanceLifecycleState{ InstanceLifecyclePhase::PREPARING_DELETE, generation, nullptr, preparation });
        preparation->SetValue(DeletePreparation{ generation });
        return preparation->GetFuture();
    }
    auto context = iter->second.pauseContext;
    iter->second.phase = InstanceLifecyclePhase::PREPARING_DELETE;
    iter->second.deletePreparation = preparation;
    if (context->phase == PausePhase::PREPARE) {
        KillResponse response;
        response.set_code(common::ERR_STATE_MACHINE_ERROR);
        response.set_message("pause canceled by authorized instance deletion");
        CompletePauseContext(instanceID, context, response);
    }
    return preparation->GetFuture();
}

litebus::Future<Status> SnapCtrlActor::FinishAuthorizedDelete(
    const std::string &instanceID, uint64_t generation)
{
    auto iter = instanceLifecycles_.find(instanceID);
    if (iter == instanceLifecycles_.end()) {
        return Status::OK();
    }
    if (iter->second.phase != InstanceLifecyclePhase::PREPARING_DELETE
        || iter->second.generation != generation) {
        return Status(StatusCode::SCHEDULE_CONFLICTED,
                      "authorized delete generation no longer owns the instance lifecycle");
    }
    instanceLifecycles_.erase(iter);
    return Status::OK();
}

litebus::Future<Status> SnapCtrlActor::DeletePauseSnapshot(const resources::InstanceInfo &instanceInfo)
{
    const auto state = static_cast<InstanceState>(instanceInfo.instancestatus().code());
    if (state != InstanceState::PAUSED
        && !(state == InstanceState::RUNNING && instanceInfo.has_snapshotinfo())) {
        return Status::OK();
    }
    if (instanceInfo.tenantid().empty()) {
        return Status(StatusCode::FAILED, "tenant ID is empty for paused snapshot deletion");
    }
    if (instanceInfo.instanceid().empty()) {
        return Status(StatusCode::FAILED, "instance ID is empty for paused snapshot deletion");
    }
    if (functionAgentMgr_ == nullptr) {
        return Status(StatusCode::FAILED, "function agent manager is unavailable for paused snapshot deletion");
    }
    ::messages::SnapshotAttemptFinalizeRequest request;
    request.set_protocolversion(1);
    request.set_operation(::messages::PAUSED_DELETED);
    request.set_tenantid(instanceInfo.tenantid());
    request.set_instanceid(instanceInfo.instanceid());
    const auto hasSnapshot = instanceInfo.has_snapshotinfo()
        && !instanceInfo.snapshotinfo().checkpointid().empty();
    if (!hasSnapshot) {
        // There is no safe remote key to infer. The immutable object, if any,
        // remains TTL-owned rather than falling back to the legacy fixed key.
        return Status::OK();
    }
    const auto &snapshot = instanceInfo.snapshotinfo();
    if (!resume_identity::IsCompleteReadySnapshot(snapshot)) {
        return Status(StatusCode::FAILED, "paused snapshot metadata is incomplete");
    }
    request.set_snapshotid(snapshot.checkpointid());
    request.set_expectedsize(snapshot.size());
    request.set_expectedsha256(snapshot.sha256());
    request.set_expectedstorage(snapshot.storage());
    request.set_attemptid("paused-delete/" + instanceInfo.instanceid() + "/"
                          + request.snapshotid());
    auto result = std::make_shared<litebus::Promise<Status>>();
    functionAgentMgr_->FinalizeSnapshotAttemptOnAnyAgent(request).OnComplete(
        [result](const litebus::Future<::messages::SnapshotAttemptFinalizeResponse> &future) {
            if (future.IsError()) {
                result->SetValue(Status(StatusCode::ERR_INNER_COMMUNICATION,
                                        "paused snapshot cleanup future failed"));
                return;
            }
            const auto &response = future.Get();
            if (response.code() != static_cast<int32_t>(StatusCode::SUCCESS)
                || !response.remotecleanupcomplete()) {
                result->SetValue(Status(static_cast<StatusCode>(response.code()), response.message()));
                return;
            }
            result->SetValue(Status::OK());
        });
    return result->GetFuture();
}

void SnapCtrlActor::OnPauseGateComplete(const std::string &instanceID,
                                        const std::shared_ptr<PauseContext> &context,
                                        const litebus::Future<Status> &gateFuture)
{
    if (gateFuture.IsError() || gateFuture.Get().IsError()) {
        KillResponse response;
        response.set_code(common::ERR_LOCAL_SCHEDULER_OPERATION_ERROR);
        response.set_message(gateFuture.IsError() ? "pause gate future failed" : gateFuture.Get().RawMessage());
        CompletePauseContext(instanceID, context, response);
        return;
    }
    auto validation = ValidatePauseContinuation(instanceID, context);
    if (validation.IsError()) {
        KillResponse response;
        response.set_code(common::ERR_STATE_MACHINE_ERROR);
        response.set_message(validation.RawMessage());
        CompletePauseContext(instanceID, context, response);
        return;
    }
    context->phase = PausePhase::PREPARE;
    PreparePauseSource(instanceID, context);
}

void SnapCtrlActor::PreparePauseSource(const std::string &instanceID,
                                       const std::shared_ptr<PauseContext> &context)
{
    auto validation = ValidatePauseContinuation(instanceID, context);
    if (validation.IsError()) {
        KillResponse response;
        response.set_code(common::ERR_STATE_MACHINE_ERROR);
        response.set_message(validation.RawMessage());
        CompletePauseContext(instanceID, context, response);
        return;
    }
    if (DeleteOwnsLifecycle(instanceID, context)) {
        KillResponse response;
        response.set_code(common::ERR_STATE_MACHINE_ERROR);
        response.set_message("pause canceled by authorized instance deletion");
        CompletePauseContext(instanceID, context, response);
        return;
    }
    if (std::chrono::steady_clock::now() >= context->prepareDeadline) {
        KillResponse response;
        response.set_code(common::ERR_INNER_COMMUNICATION);
        response.set_message("pause operation deadline exceeded while preparing source");
        RetryPreparePauseGate(instanceID, context, response);
        return;
    }
    clientManager_->GetControlInterfacePosixClient(instanceID)
        .OnComplete(litebus::Defer(GetAID(), [this, instanceID, context](
            const litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> &clientFuture) {
            auto validation = ValidatePauseContinuation(instanceID, context);
            if (validation.IsError()) {
                KillResponse response;
                response.set_code(common::ERR_STATE_MACHINE_ERROR);
                response.set_message(validation.RawMessage());
                CompletePauseContext(instanceID, context, response);
                return;
            }
            if (clientFuture.IsError() || clientFuture.Get() == nullptr) {
                RetryPausePrepare(instanceID, context, "GetControlInterfacePosixClient");
                return;
            }
            runtime::PrepareSnapRequest request;
            clientFuture.Get()->PrepareSnap(std::move(request))
                .OnComplete(litebus::Defer(GetAID(), [this, instanceID, context](
                    const litebus::Future<runtime::PrepareSnapResponse> &prepareFuture) {
                    auto validation = ValidatePauseContinuation(instanceID, context);
                    if (validation.IsError()) {
                        KillResponse response;
                        response.set_code(common::ERR_STATE_MACHINE_ERROR);
                        response.set_message(validation.RawMessage());
                        CompletePauseContext(instanceID, context, response);
                        return;
                    }
                    if (prepareFuture.IsError()) {
                        RetryPausePrepare(instanceID, context, "PrepareSnap");
                        return;
                    }
                    const auto &prepare = prepareFuture.Get();
                    if (prepare.code() != common::ERR_NONE) {
                        KillResponse originalError;
                        originalError.set_code(static_cast<common::ErrorCode>(prepare.code()));
                        originalError.set_message(prepare.message());
                        if (DeleteOwnsLifecycle(instanceID, context)) {
                            CompletePauseContext(instanceID, context, originalError);
                            return;
                        }
                        RetryPreparePauseGate(instanceID, context, originalError);
                        return;
                    }
                    OnPausePrepareComplete(instanceID, context, litebus::Future<Status>(Status::OK()));
                }));
        }));
}

void SnapCtrlActor::RetryPausePrepare(const std::string &instanceID,
                                      const std::shared_ptr<PauseContext> &context,
                                      const std::string &failedOperation)
{
    auto validation = ValidatePauseContinuation(instanceID, context);
    if (validation.IsError()) {
        KillResponse response;
        response.set_code(common::ERR_STATE_MACHINE_ERROR);
        response.set_message(validation.RawMessage());
        CompletePauseContext(instanceID, context, response);
        return;
    }
    if (DeleteOwnsLifecycle(instanceID, context)) {
        KillResponse response;
        response.set_code(common::ERR_STATE_MACHINE_ERROR);
        response.set_message("pause canceled by authorized instance deletion");
        CompletePauseContext(instanceID, context, response);
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= context->prepareDeadline) {
        KillResponse response;
        response.set_code(common::ERR_INNER_COMMUNICATION);
        response.set_message("pause operation deadline exceeded after " + failedOperation);
        RetryPreparePauseGate(instanceID, context, response);
        return;
    }

    const auto remainingMs = std::max<int64_t>(
        1, std::chrono::duration_cast<std::chrono::milliseconds>(context->prepareDeadline - now).count());
    const auto configuredMaximum = std::max<uint64_t>(1, pauseRetryPolicy_.maximumDelayMs);
    const auto delayMs = std::min<uint64_t>(
        std::max<uint64_t>(1, context->prepareRetryDelayMs),
        std::min<uint64_t>(configuredMaximum, static_cast<uint64_t>(remainingMs)));
    context->prepareRetryDelayMs = delayMs >= configuredMaximum / 2
        ? configuredMaximum
        : std::min(configuredMaximum, delayMs * 2);
    (void)litebus::AsyncAfter(delayMs, GetAID(), &SnapCtrlActor::PreparePauseSource, instanceID, context);
}

void SnapCtrlActor::RetryPreparePauseGate(const std::string &instanceID,
                                          const std::shared_ptr<PauseContext> &context,
                                          const KillResponse &originalError)
{
    auto validation = ValidatePauseContinuation(instanceID, context);
    if (validation.IsError()) {
        KillResponse response;
        response.set_code(common::ERR_STATE_MACHINE_ERROR);
        response.set_message(validation.RawMessage());
        CompletePauseContext(instanceID, context, response);
        return;
    }
    if (DeleteOwnsLifecycle(instanceID, context)) {
        CompletePauseContext(instanceID, context, originalError);
        return;
    }
    if (instanceCtrl_ == nullptr) {
        KillResponse response;
        response.set_code(common::ERR_INNER_SYSTEM_ERROR);
        response.set_message("instance control is unavailable for gate recovery");
        CompletePauseContext(instanceID, context, response);
        return;
    }
    instanceCtrl_->RecoverPauseGate(context->sourceInstanceInfo)
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnPreparePauseGateRecovered,
                                   instanceID, context, originalError, std::placeholders::_1));
}

void SnapCtrlActor::OnPreparePauseGateRecovered(const std::string &instanceID,
                                                const std::shared_ptr<PauseContext> &context,
                                                const KillResponse &originalError,
                                                const litebus::Future<Status> &recoverFuture)
{
    auto validation = ValidatePauseContinuation(instanceID, context);
    if (validation.IsError()) {
        KillResponse response;
        response.set_code(common::ERR_STATE_MACHINE_ERROR);
        response.set_message(validation.RawMessage());
        CompletePauseContext(instanceID, context, response);
        return;
    }
    if (recoverFuture.IsError() || recoverFuture.Get().IsError()) {
        (void)litebus::AsyncAfter(PAUSE_PHYSICAL_FACT_RETRY_MS, GetAID(),
                                  &SnapCtrlActor::RetryPreparePauseGate, instanceID, context, originalError);
        return;
    }
    CompletePauseContext(instanceID, context, originalError);
}

void SnapCtrlActor::OnPausePrepareComplete(const std::string &instanceID,
                                           const std::shared_ptr<PauseContext> &context,
                                           const litebus::Future<Status> &prepareFuture)
{
    if (prepareFuture.IsError()) {
        KillResponse response;
        response.set_code(common::ERR_INNER_COMMUNICATION);
        response.set_message("PrepareSnap future failed");
        CompletePauseContext(instanceID, context, response);
        return;
    }
    const auto &prepareStatus = prepareFuture.Get();
    if (prepareStatus.IsError()) {
        KillResponse response;
        response.set_code(Status::GetPosixErrorCode(prepareStatus.StatusCode()));
        response.set_message(prepareStatus.RawMessage());
        CompletePauseContext(instanceID, context, response);
        return;
    }

    ASSERT_IF_NULL(instanceControlView_);
    auto stateMachine = instanceControlView_->GetInstance(instanceID);
    if (stateMachine == nullptr) {
        KillResponse response;
        response.set_code(common::ERR_STATE_MACHINE_ERROR);
        response.set_message("instance disappeared during pause");
        CompletePauseContext(instanceID, context, response);
        return;
    }
    const auto latestInfo = stateMachine->GetInstanceInfo();
    const auto latestState = static_cast<InstanceState>(latestInfo.instancestatus().code());
    if (latestState != InstanceState::RUNNING
        || !MatchesPauseSourcePhysicalIdentity(context->sourceInstanceInfo, latestInfo)) {
        KillResponse response;
        response.set_code(common::ERR_STATE_MACHINE_ERROR);
        response.set_message("instance identity or state changed during pause");
        CompletePauseContext(instanceID, context, response);
        return;
    }

    context->phase = PausePhase::CHECKPOINT;
    ContinuePauseCheckpoint(instanceID, context)
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnPauseCheckpointComplete, instanceID, context,
                                   std::placeholders::_1));
}

namespace {
KillResponse PauseFailure(const Status &status)
{
    KillResponse response;
    response.set_code(common::ERR_LOCAL_SCHEDULER_OPERATION_ERROR);
    response.set_message(status.RawMessage());
    return response;
}
}  // namespace

bool SnapCtrlActor::IsCurrentPauseContext(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context) const
{
    const auto iter = instanceLifecycles_.find(instanceID);
    return iter != instanceLifecycles_.end() && iter->second.pauseContext == context;
}

bool SnapCtrlActor::DeleteOwnsLifecycle(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context) const
{
    const auto iter = instanceLifecycles_.find(instanceID);
    return iter != instanceLifecycles_.end()
        && iter->second.pauseContext == context
        && iter->second.phase == InstanceLifecyclePhase::PREPARING_DELETE;
}

Status SnapCtrlActor::ValidatePauseContinuation(const std::string &instanceID,
                                                const std::shared_ptr<PauseContext> &context) const
{
    if (!IsCurrentPauseContext(instanceID, context)) {
        return Status(StatusCode::ERR_STATE_MACHINE_ERROR, "pause context was replaced");
    }
    if (instanceControlView_ == nullptr) {
        return Status(StatusCode::ERR_STATE_MACHINE_ERROR, "instance control view is unavailable");
    }
    auto stateMachine = instanceControlView_->GetInstance(instanceID);
    if (stateMachine == nullptr) {
        return Status(StatusCode::ERR_INSTANCE_NOT_FOUND, "instance disappeared during pause");
    }
    const auto latestInfo = stateMachine->GetInstanceInfo();
    if (static_cast<InstanceState>(latestInfo.instancestatus().code()) != InstanceState::RUNNING
        || !MatchesPauseSourcePhysicalIdentity(context->sourceInstanceInfo, latestInfo)) {
        return Status(StatusCode::ERR_STATE_MACHINE_ERROR, "instance identity or state changed during pause");
    }
    return Status::OK();
}

litebus::Future<KillResponse> SnapCtrlActor::ContinuePauseCheckpoint(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context)
{
    auto validation = ValidatePauseContinuation(instanceID, context);
    if (validation.IsError()) {
        return PauseFailure(validation);
    }
    if (functionAgentMgr_ == nullptr) {
        return PauseFailure(Status(StatusCode::FAILED, "pause snapshot dependencies are unavailable"));
    }
    auto result = std::make_shared<litebus::Promise<KillResponse>>();
    functionAgentMgr_->SnapshotRuntime(context->operationRequestID, context->sourceInstanceInfo,
                                       context->ttlSeconds,
                                       common::PAUSE_RESUME, context->operationRequestID, "")
        .OnComplete(litebus::Defer(GetAID(), [this, instanceID, context, result](
            const litebus::Future<messages::SnapshotRuntimeResponse> &snapshotFuture) {
            if (snapshotFuture.IsError()) {
                auto validation = ValidatePauseContinuation(instanceID, context);
                if (validation.IsError()) {
                    result->Associate(FailPauseAndCleanupTemporary(instanceID, context, validation));
                    return;
                }
                (void)litebus::AsyncAfter(PAUSE_PHYSICAL_FACT_RETRY_MS, GetAID(),
                                          &SnapCtrlActor::RetryPauseCheckpoint, instanceID, context, result);
                return;
            }
            result->Associate(HandlePauseCheckpointResponse(instanceID, context, snapshotFuture.Get()));
        }));
    return result->GetFuture();
}

litebus::Future<KillResponse> SnapCtrlActor::HandlePauseCheckpointResponse(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    const messages::SnapshotRuntimeResponse &response)
{
    if (response.code() == common::ERR_NONE && response.has_localsnapshot()
        && (!response.has_snapshotinfo()
            || response.snapshotinfo().status() != resources::SNAPSHOT_READY)) {
        return functionAgentMgr_->PublishSnapshotArtifact(
            context->operationRequestID, context->sourceInstanceInfo,
            context->operationRequestID)
            .Then([this, instanceID, context](
                      const messages::SnapshotRuntimeResponse &published)
                      -> litebus::Future<KillResponse> {
                return HandlePauseCheckpointResponse(instanceID, context, published);
            });
    }
    const auto validation = ValidatePauseContinuation(instanceID, context);
    const auto convergence = ClassifyPauseCheckpointResponse(response, validation);
    if (convergence != ConvergenceResult::COMMITTED) {
        if (convergence == ConvergenceResult::IDENTITY_CHANGED) {
            return FailPauseAndCleanupTemporary(instanceID, context, validation);
        }
        KillResponse originalError;
        originalError.set_code(static_cast<common::ErrorCode>(response.code()));
        originalError.set_message(response.message());
        if (convergence == ConvergenceResult::RESULT_UNKNOWN) {
            auto retry = std::make_shared<litebus::Promise<KillResponse>>();
            (void)litebus::AsyncAfter(PAUSE_PHYSICAL_FACT_RETRY_MS, GetAID(),
                                      &SnapCtrlActor::RetryPauseCheckpoint, instanceID, context, retry);
            return retry->GetFuture();
        }
        if (convergence == ConvergenceResult::SOURCE_RUNNING) {
            return RecoverRunningPauseSource(instanceID, context, originalError);
        }
        return originalError;
    }
    if (!response.has_snapshotinfo()
        || response.snapshotinfo().checkpointid() != context->operationRequestID
        || !resume_identity::IsCompleteReadySnapshot(response.snapshotinfo())
        || response.snapshotinfo().ttlseconds() != context->ttlSeconds
        || response.has_physicalfact()) {
        return FailPauseAndCleanupTemporary(
            instanceID, context, Status(StatusCode::FAILED, "pause checkpoint response is not trustworthy"));
    }
    context->storageBackend = response.snapshotinfo().storage();
    context->publishedSize = static_cast<uint64_t>(response.snapshotinfo().size());
    context->publishedSha256 = response.snapshotinfo().sha256();
    context->sourceInstanceInfo.mutable_snapshotinfo()->CopyFrom(response.snapshotinfo());
    context->published = true;
    context->snapshotCreateTime = response.snapshotinfo().createtime();
    try {
        (void)std::stoll(context->snapshotCreateTime);
    } catch (const std::exception &) {
        return FailPauseAndCleanupTemporary(
            instanceID, context, Status(StatusCode::FAILED, "pause snapshot creation time is invalid"));
    }
    const auto committedValidation = ValidatePauseContinuation(instanceID, context);
    if (committedValidation.IsError()) {
        // The Agent response proves that an immutable artifact was published.
        // Record that fact before fencing a stale source identity so abort can
        // clean the exact attempt instead of orphaning the artifact.
        return FailPauseAndCleanupTemporary(instanceID, context, committedValidation);
    }
    return ReleasePauseSourceRuntime(instanceID, context);
}

SnapCtrlActor::ConvergenceResult SnapCtrlActor::ClassifyPauseCheckpointResponse(
    const messages::SnapshotRuntimeResponse &response,
    const Status &continuationStatus) const
{
    if (response.code() == common::ERR_NONE) {
        return ConvergenceResult::COMMITTED;
    }
    if (continuationStatus.IsError()) {
        return ConvergenceResult::IDENTITY_CHANGED;
    }
    if (response.resultunknown() || !response.has_physicalfact()
        || response.physicalfact().state() == runtime::v1::SANDBOX_STATE_UNKNOWN) {
        return ConvergenceResult::RESULT_UNKNOWN;
    }
    if (response.physicalfact().state() == runtime::v1::SANDBOX_STATE_RUNNING) {
        return ConvergenceResult::SOURCE_RUNNING;
    }
    return ConvergenceResult::TERMINAL_FAILURE;
}

SnapCtrlActor::ConvergenceResult SnapCtrlActor::ClassifyPausedCommit(
    const std::shared_ptr<PauseContext> &context,
    const resources::InstanceInfo &latest) const
{
    if (IsExactPausedCommit(context->sourceInstanceInfo, latest, context->operationRequestID,
                            context->storageBackend, context->publishedSize,
                            context->publishedSha256, context->snapshotCreateTime,
                            context->ttlSeconds)) {
        return ConvergenceResult::COMMITTED;
    }
    if (static_cast<InstanceState>(latest.instancestatus().code()) == InstanceState::RUNNING
        && MatchesPauseSourcePhysicalIdentity(context->sourceInstanceInfo, latest)) {
        return ConvergenceResult::SOURCE_RUNNING;
    }
    return ConvergenceResult::IDENTITY_CHANGED;
}

void SnapCtrlActor::RetryPauseCheckpoint(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    const std::shared_ptr<litebus::Promise<KillResponse>> &promise)
{
    promise->Associate(ContinuePauseCheckpoint(instanceID, context));
}

litebus::Future<KillResponse> SnapCtrlActor::RecoverRunningPauseSource(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    const KillResponse &originalError)
{
    auto validation = ValidatePauseContinuation(instanceID, context);
    if (validation.IsError()) {
        return PauseFailure(validation);
    }
    if (clientManager_ == nullptr || instanceCtrl_ == nullptr) {
        return PauseFailure(Status(StatusCode::FAILED, "source rescue dependencies are unavailable"));
    }
    auto result = std::make_shared<litebus::Promise<KillResponse>>();
    RetryRunningPauseClient(instanceID, context, originalError, result);
    return result->GetFuture();
}

void SnapCtrlActor::RetryRunningPauseClient(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    const KillResponse &originalError, const std::shared_ptr<litebus::Promise<KillResponse>> &result)
{
    auto validation = ValidatePauseContinuation(instanceID, context);
    if (validation.IsError()) {
        result->SetValue(PauseFailure(validation));
        return;
    }
    if (clientManager_ == nullptr) {
        result->SetValue(PauseFailure(Status(StatusCode::FAILED,
                                             "source rescue client manager is unavailable")));
        return;
    }
    clientManager_->GetControlInterfacePosixClient(instanceID)
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnRunningPauseClient,
                                   instanceID, context, originalError, result, std::placeholders::_1));
}

void SnapCtrlActor::OnRunningPauseClient(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    const KillResponse &originalError, const std::shared_ptr<litebus::Promise<KillResponse>> &result,
    const litebus::Future<std::shared_ptr<ControlInterfacePosixClient>> &clientFuture)
{
    auto validation = ValidatePauseContinuation(instanceID, context);
    if (validation.IsError()) {
        result->SetValue(PauseFailure(validation));
        return;
    }
    if (clientFuture.IsError() || clientFuture.Get() == nullptr) {
        (void)litebus::AsyncAfter(PAUSE_PHYSICAL_FACT_RETRY_MS, GetAID(),
                                  &SnapCtrlActor::RetryRunningPauseClient,
                                  instanceID, context, originalError, result);
        return;
    }
    runtime::SnapStartedRequest request;
    clientFuture.Get()->SnapStarted(std::move(request))
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnRunningPauseStarted,
                                   instanceID, context, originalError, result, std::placeholders::_1));
}

void SnapCtrlActor::OnRunningPauseStarted(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    const KillResponse &originalError, const std::shared_ptr<litebus::Promise<KillResponse>> &result,
    const litebus::Future<runtime::SnapStartedResponse> &startedFuture)
{
    auto validation = ValidatePauseContinuation(instanceID, context);
    if (validation.IsError()) {
        result->SetValue(PauseFailure(validation));
        return;
    }
    if (startedFuture.IsError()) {
        (void)litebus::AsyncAfter(PAUSE_PHYSICAL_FACT_RETRY_MS, GetAID(),
                                  &SnapCtrlActor::RetryRunningPauseClient,
                                  instanceID, context, originalError, result);
        return;
    }
    const auto &started = startedFuture.Get();
    if (started.code() != common::ERR_NONE) {
        result->SetValue(PauseFailure(Status(StatusCode::FAILED,
                                             "source rescue SnapStarted failed: " + started.message())));
        return;
    }
    if (DeleteOwnsLifecycle(instanceID, context)) {
        result->SetValue(originalError);
        return;
    }
    RetryRunningPauseGate(instanceID, context, originalError, result);
}

void SnapCtrlActor::RetryRunningPauseGate(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    const KillResponse &originalError, const std::shared_ptr<litebus::Promise<KillResponse>> &result)
{
    auto validation = ValidatePauseContinuation(instanceID, context);
    if (validation.IsError()) {
        result->SetValue(PauseFailure(validation));
        return;
    }
    if (DeleteOwnsLifecycle(instanceID, context)) {
        result->SetValue(originalError);
        return;
    }
    if (instanceCtrl_ == nullptr) {
        result->SetValue(PauseFailure(Status(StatusCode::FAILED,
                                             "source rescue instance control is unavailable")));
        return;
    }
    instanceCtrl_->RecoverPauseGate(context->sourceInstanceInfo)
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnRunningPauseGateRecovered,
                                   instanceID, context, originalError, result, std::placeholders::_1));
}

void SnapCtrlActor::OnRunningPauseGateRecovered(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    const KillResponse &originalError, const std::shared_ptr<litebus::Promise<KillResponse>> &result,
    const litebus::Future<Status> &recoverFuture)
{
    auto validation = ValidatePauseContinuation(instanceID, context);
    if (validation.IsError()) {
        result->SetValue(PauseFailure(validation));
        return;
    }
    if (recoverFuture.IsError() || recoverFuture.Get().IsError()) {
        (void)litebus::AsyncAfter(PAUSE_PHYSICAL_FACT_RETRY_MS, GetAID(),
                                  &SnapCtrlActor::RetryRunningPauseGate,
                                  instanceID, context, originalError, result);
        return;
    }
    result->SetValue(originalError);
}

litebus::Future<KillResponse> SnapCtrlActor::ReleasePauseSourceRuntime(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context)
{
    auto validation = ValidatePauseContinuation(instanceID, context);
    if (validation.IsError()) {
        return FailPauseAndCleanupTemporary(instanceID, context, validation);
    }
    auto result = std::make_shared<litebus::Promise<KillResponse>>();
    instanceCtrl_->ReleaseRuntimeForPause(context->sourceInstanceInfo, context->operationRequestID)
        .OnComplete(litebus::Defer(GetAID(), [this, instanceID, context, result](
            const litebus::Future<Status> &releaseFuture) {
            auto validation = ValidatePauseContinuation(instanceID, context);
            if (validation.IsError()) {
                result->Associate(FailPauseAndCleanupTemporary(instanceID, context, validation));
                return;
            }
            if (releaseFuture.IsError()) {
                (void)litebus::AsyncAfter(PAUSE_PHYSICAL_FACT_RETRY_MS, GetAID(),
                                          &SnapCtrlActor::RetryReleasePauseSourceRuntime,
                                          instanceID, context, result);
                return;
            }
            const auto &status = releaseFuture.Get();
            if (status.IsError()) {
                (void)litebus::AsyncAfter(PAUSE_PHYSICAL_FACT_RETRY_MS, GetAID(),
                                          &SnapCtrlActor::RetryReleasePauseSourceRuntime,
                                          instanceID, context, result);
                return;
            }
            result->Associate(CommitPausedState(instanceID, context));
        }));
    return result->GetFuture();
}

void SnapCtrlActor::RetryReleasePauseSourceRuntime(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    const std::shared_ptr<litebus::Promise<KillResponse>> &promise)
{
    promise->Associate(ReleasePauseSourceRuntime(instanceID, context));
}

litebus::Future<KillResponse> SnapCtrlActor::CleanupPauseAttemptRemoteArtifacts(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    const KillResponse &originalError)
{
    return FinalizePauseAttempt(instanceID, context, ::messages::PAUSE_ABORTED, originalError);
}

litebus::Future<KillResponse> SnapCtrlActor::CommitPausedState(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context)
{
    auto validation = ValidatePauseContinuation(instanceID, context);
    if (validation.IsError()) {
        return FailPauseAndCleanupTemporary(instanceID, context, validation);
    }
    auto stateMachine = instanceControlView_->GetInstance(instanceID);
    auto scheduleRequest = stateMachine == nullptr ? nullptr : stateMachine->GetScheduleRequest();
    if (stateMachine == nullptr || scheduleRequest == nullptr || instanceCtrl_ == nullptr) {
        return FailPauseAndCleanupTemporary(
            instanceID, context,
            Status(StatusCode::ERR_STATE_MACHINE_ERROR, "pause state transition dependencies are unavailable"));
    }
    auto *snapshotInfo = scheduleRequest->mutable_instance()->mutable_snapshotinfo();
    snapshotInfo->set_checkpointid(context->operationRequestID);
    snapshotInfo->set_storage(context->storageBackend);
    snapshotInfo->set_size(static_cast<int64_t>(context->publishedSize));
    snapshotInfo->set_sha256(context->publishedSha256);
    snapshotInfo->set_createtime(context->snapshotCreateTime);
    snapshotInfo->set_ttlseconds(context->ttlSeconds);
    snapshotInfo->set_status(resources::SNAPSHOT_READY);
    snapshotInfo->set_sourcenodeid(context->sourceInstanceInfo.functionproxyid());
    TransContext transition{ InstanceState::PAUSED, context->sourceInstanceInfo.version(), "paused" };
    transition.scheduleReq = scheduleRequest;
    auto result = std::make_shared<litebus::Promise<KillResponse>>();
    instanceCtrl_->TransInstanceState(stateMachine, transition)
        .OnComplete(litebus::Defer(GetAID(), [this, instanceID, context, result](
            const litebus::Future<TransitionResult> &transitionFuture) {
            if (transitionFuture.IsError()) {
                result->Associate(ReconcilePausedCommit(
                    instanceID, context,
                    PauseFailure(Status(StatusCode::FAILED, "pause state transition future failed"))));
                return;
            }
            const auto &transitionResult = transitionFuture.Get();
            if (transitionResult.status.IsError()) {
                result->Associate(ReconcilePausedCommit(instanceID, context,
                                                        PauseFailure(transitionResult.status)));
                return;
            }
            result->Associate(FinalizePauseAfterResourceRelease(instanceID, context));
        }));
    return result->GetFuture();
}

litebus::Future<KillResponse> SnapCtrlActor::FinalizeSuccessfulPause(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context)
{
    KillResponse response;
    response.set_code(common::ERR_NONE);
    SnapInfo info;
    info.set_snapshotid(context->operationRequestID);
    info.set_size(static_cast<int64_t>(context->publishedSize));
    response.set_payload(info.SerializeAsString());
    return FinalizePauseAttempt(instanceID, context, ::messages::PAUSE_COMMITTED, response);
}

litebus::Future<KillResponse> SnapCtrlActor::FinalizePauseAfterResourceRelease(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context)
{
    auto result = std::make_shared<litebus::Promise<KillResponse>>();
    RetryPausedResourceRelease(instanceID, context, result);
    return result->GetFuture();
}

void SnapCtrlActor::RetryPausedResourceRelease(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    const std::shared_ptr<litebus::Promise<KillResponse>> &result)
{
    if (!IsCurrentPauseContext(instanceID, context) || instanceCtrl_ == nullptr) {
        result->SetValue(PauseFailure(Status(StatusCode::FAILED,
                                             "instance control is unavailable for PAUSED resource release")));
        return;
    }
    instanceCtrl_->ReleasePausedInstanceResources(context->sourceInstanceInfo)
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnPausedResourceReleased,
                                   instanceID, context, result, std::placeholders::_1));
}

void SnapCtrlActor::OnPausedResourceReleased(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    const std::shared_ptr<litebus::Promise<KillResponse>> &result,
    const litebus::Future<Status> &releaseFuture)
{
    if (releaseFuture.IsError() || releaseFuture.Get().IsError()) {
        (void)litebus::AsyncAfter(PAUSE_PHYSICAL_FACT_RETRY_MS, GetAID(),
                                  &SnapCtrlActor::RetryPausedResourceRelease,
                                  instanceID, context, result);
        return;
    }
    result->Associate(FinalizeSuccessfulPause(instanceID, context));
}

litebus::Future<KillResponse> SnapCtrlActor::FinalizePauseAttempt(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    ::messages::SnapshotAttemptFinalizeOperation operation, const KillResponse &terminalResponse)
{
    auto result = std::make_shared<litebus::Promise<KillResponse>>();
    RetryPauseAttemptFinalization(instanceID, context, operation, terminalResponse, result);
    return result->GetFuture();
}

void SnapCtrlActor::RetryPauseAttemptFinalization(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    ::messages::SnapshotAttemptFinalizeOperation operation, const KillResponse &terminalResponse,
    const std::shared_ptr<litebus::Promise<KillResponse>> &result)
{
    if (!IsCurrentPauseContext(instanceID, context)) {
        result->SetValue(terminalResponse);
        return;
    }
    if (functionAgentMgr_ == nullptr) {
        result->SetValue(PauseFailure(Status(StatusCode::FAILED,
                                             "function agent manager is unavailable for pause finalization")));
        return;
    }
    ::messages::SnapshotAttemptFinalizeRequest request;
    request.set_protocolversion(1);
    request.set_operation(operation);
    request.set_tenantid(context->sourceInstanceInfo.tenantid());
    request.set_instanceid(context->sourceInstanceInfo.instanceid());
    request.set_snapshotid(context->operationRequestID);
    request.set_attemptid(context->operationRequestID);
    request.set_runtimeid(context->sourceInstanceInfo.runtimeid());
    request.set_expectedsize(context->publishedSize);
    request.set_expectedsha256(context->publishedSha256);
    auto finalized = context->sourceInstanceInfo.functionagentid().empty()
                         ? functionAgentMgr_->FinalizeSnapshotAttemptOnAnyAgent(request)
                         : functionAgentMgr_->FinalizeSnapshotAttempt(context->sourceInstanceInfo, request);
    finalized.OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnPauseAttemptFinalized,
                                        instanceID, context, operation, terminalResponse, result,
                                        std::placeholders::_1));
}

void SnapCtrlActor::OnPauseAttemptFinalized(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    ::messages::SnapshotAttemptFinalizeOperation operation, const KillResponse &terminalResponse,
    const std::shared_ptr<litebus::Promise<KillResponse>> &result,
    const litebus::Future<::messages::SnapshotAttemptFinalizeResponse> &finalizeFuture)
{
    if (finalizeFuture.IsError() || finalizeFuture.Get().code() != static_cast<int32_t>(StatusCode::SUCCESS)
        || finalizeFuture.Get().resultunknown() || !finalizeFuture.Get().localcleanupcomplete()
        || !finalizeFuture.Get().remotecleanupcomplete()) {
        (void)litebus::AsyncAfter(PAUSE_PHYSICAL_FACT_RETRY_MS, GetAID(),
                                  &SnapCtrlActor::RetryPauseAttemptFinalization,
                                  instanceID, context, operation, terminalResponse, result);
        return;
    }
    result->SetValue(terminalResponse);
}

litebus::Future<KillResponse> SnapCtrlActor::ReconcilePausedCommit(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    const KillResponse &commitError)
{
    auto result = std::make_shared<litebus::Promise<KillResponse>>();
    RetryPausedCommitReconcile(instanceID, context, commitError, result);
    return result->GetFuture();
}

void SnapCtrlActor::RetryPausedCommitReconcile(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    const KillResponse &commitError, const std::shared_ptr<litebus::Promise<KillResponse>> &result)
{
    const bool current = IsCurrentPauseContext(instanceID, context);
    auto stateMachine = instanceControlView_ == nullptr ? nullptr : instanceControlView_->GetInstance(instanceID);
    if (!current || stateMachine == nullptr) {
        result->Associate(CleanupPauseAttemptRemoteArtifacts(instanceID, context, commitError));
        return;
    }
    stateMachine->SyncInstanceFromMetaStore()
        .OnComplete(litebus::Defer(GetAID(), &SnapCtrlActor::OnPausedCommitSynced,
                                   instanceID, context, commitError, result, std::placeholders::_1));
}

void SnapCtrlActor::OnPausedCommitSynced(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context,
    const KillResponse &commitError, const std::shared_ptr<litebus::Promise<KillResponse>> &result,
    const litebus::Future<resources::InstanceInfo> &syncFuture)
{
    if (!IsCurrentPauseContext(instanceID, context)) {
        result->Associate(CleanupPauseAttemptRemoteArtifacts(instanceID, context, commitError));
        return;
    }
    if (syncFuture.IsError()) {
        (void)litebus::AsyncAfter(PAUSE_PHYSICAL_FACT_RETRY_MS, GetAID(),
                                  &SnapCtrlActor::RetryPausedCommitReconcile,
                                  instanceID, context, commitError, result);
        return;
    }
    const auto &latest = syncFuture.Get();
    const auto convergence = ClassifyPausedCommit(context, latest);
    if (convergence == ConvergenceResult::COMMITTED) {
        result->Associate(FinalizePauseAfterResourceRelease(instanceID, context));
        return;
    }
    if (convergence == ConvergenceResult::SOURCE_RUNNING) {
        result->Associate(CommitPausedState(instanceID, context));
        return;
    }
    result->Associate(CleanupPauseAttemptRemoteArtifacts(instanceID, context, commitError));
}

litebus::Future<KillResponse> SnapCtrlActor::FailPauseAndCleanupTemporary(
    const std::string &instanceID, const std::shared_ptr<PauseContext> &context, const Status &status)
{
    auto failure = PauseFailure(status);
    if (context != nullptr && context->published && context->publishedSize > 0
        && !context->publishedSha256.empty()
        && (context->storageBackend == "obs" || context->storageBackend == "datasystem")) {
        return CleanupPauseAttemptRemoteArtifacts(instanceID, context, failure);
    }
    return failure;
}

void SnapCtrlActor::OnPauseCheckpointComplete(const std::string &instanceID,
                                              const std::shared_ptr<PauseContext> &context,
                                              const litebus::Future<KillResponse> &checkpointFuture)
{
    if (checkpointFuture.IsError()) {
        YRLOG_ERROR("{}|pause checkpoint stage future escaped stage-specific convergence", instanceID);
        return;
    }
    CompletePauseContext(instanceID, context, checkpointFuture.Get());
}

void SnapCtrlActor::CompletePauseContext(const std::string &instanceID,
                                         const std::shared_ptr<PauseContext> &context,
                                         const KillResponse &response)
{
    auto iter = instanceLifecycles_.find(instanceID);
    if (iter == instanceLifecycles_.end() || iter->second.pauseContext != context) {
        YRLOG_WARN("{}|ignore stale pause completion", instanceID);
        return;
    }
    auto completion = context->completion;
    const bool deleteOwnsLifecycle = iter->second.phase == InstanceLifecyclePhase::PREPARING_DELETE;
    const auto generation = iter->second.generation;
    auto deletePreparation = iter->second.deletePreparation;
    if (deleteOwnsLifecycle) {
        iter->second.pauseContext.reset();
    } else {
        instanceLifecycles_.erase(iter);
    }
    completion->SetValue(response);
    if (deleteOwnsLifecycle && deletePreparation != nullptr) {
        deletePreparation->SetValue(DeletePreparation{ generation });
    }
}

}  // namespace functionsystem::local_scheduler
