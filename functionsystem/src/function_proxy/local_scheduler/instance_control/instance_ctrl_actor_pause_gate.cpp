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

#include "instance_ctrl_actor.h"

#include "async/asyncafter.hpp"
#include "async/defer.hpp"
#include "common/logs/logging.h"
#include "common/types/instance_state.h"

namespace functionsystem::local_scheduler {
using namespace messages;
using namespace std::placeholders;

static const uint32_t PAUSE_GATE_COMPENSATION_RETRY_MS = 50;
static const size_t PAUSE_GATE_COMPENSATION_MAX_IN_FLIGHT = 2;

bool InstanceCtrlActor::IsSamePauseGateIdentity(const InstanceInfo &left, const InstanceInfo &right)
{
    return left.instanceid() == right.instanceid()
        && left.requestid() == right.requestid()
        && left.version() == right.version()
        && left.functionproxyid() == right.functionproxyid()
        && left.runtimeid() == right.runtimeid()
        && left.functionagentid() == right.functionagentid()
        && left.containerid() == right.containerid()
        && left.unitid() == right.unitid()
        && left.tenantid() == right.tenantid()
        && left.runtimeaddress() == right.runtimeaddress();
}

bool InstanceCtrlActor::IsPauseRuntimeFenced(
    const std::string &instanceID, const std::string &runtimeID)
{
    if (instanceID.empty() || runtimeID.empty()) {
        return false;
    }
    const auto gate = pauseGateContexts_.find(instanceID);
    return gate != pauseGateContexts_.end()
        && gate->second.phase != PauseGatePhase::RECOVERED
        && gate->second.identity.runtimeid() == runtimeID;
}

Status InstanceCtrlActor::ValidatePauseGateIdentity(const InstanceInfo &identity) const
{
    if (instanceControlView_ == nullptr) {
        return Status(StatusCode::FAILED, "instance control view is not available");
    }
    auto stateMachine = instanceControlView_->GetInstance(identity.instanceid());
    if (stateMachine == nullptr) {
        return Status(StatusCode::ERR_INSTANCE_NOT_FOUND,
                      fmt::format("instance({}) not found for pause gate", identity.instanceid()));
    }
    if (stateMachine->GetInstanceState() != InstanceState::RUNNING) {
        return Status(StatusCode::ERR_STATE_MACHINE_ERROR,
                      fmt::format("instance({}) is not RUNNING for pause gate", identity.instanceid()));
    }
    if (stateMachine->GetOwner() != nodeID_) {
        return Status(StatusCode::ERR_INSTANCE_INFO_INVALID,
                      fmt::format("instance({}) is not owned by this node", identity.instanceid()));
    }
    if (!IsSamePauseGateIdentity(identity, stateMachine->GetInstanceInfo())) {
        return Status(StatusCode::ERR_INSTANCE_INFO_INVALID,
                      fmt::format("instance({}) identity changed before pause gate", identity.instanceid()));
    }
    return Status::OK();
}

void InstanceCtrlActor::RetirePauseGateForDeletedInstance(const std::string &instanceID)
{
    auto iter = pauseGateContexts_.find(instanceID);
    if (iter == pauseGateContexts_.end()) {
        return;
    }
    pauseGateContexts_.erase(iter);
    YRLOG_INFO("retire pause gate because instance({}) was deleted", instanceID);
}

bool InstanceCtrlActor::CompletePauseGateOnFenceFailure(const std::string &instanceID, uint64_t token)
{
    auto iter = pauseGateContexts_.find(instanceID);
    if (iter == pauseGateContexts_.end() || iter->second.token != token) {
        return true;
    }
    auto validation = ValidatePauseGateIdentity(iter->second.identity);
    if (validation.IsOk()) {
        return false;
    }
    auto promise = iter->second.promise;
    pauseGateContexts_.erase(iter);
    promise->SetValue(validation);
    return true;
}

litebus::Future<Status> InstanceCtrlActor::BeginPauseGate(const InstanceInfo &identity)
{
    auto validation = ValidatePauseGateIdentity(identity);
    if (validation.IsError()) {
        return validation;
    }

    auto existing = pauseGateContexts_.find(identity.instanceid());
    if (existing != pauseGateContexts_.end()) {
        auto &context = existing->second;
        if (!IsSamePauseGateIdentity(context.identity, identity)) {
            if (context.phase != PauseGatePhase::GATED
                || identity.version() <= context.identity.version()) {
                return Status(StatusCode::ERR_INSTANCE_INFO_INVALID, "pause gate identity changed");
            }
            YRLOG_INFO("{}|retire committed pause gate for newer RUNNING generation, old version: {}, new version: {}",
                       identity.instanceid(), context.identity.version(), identity.version());
            pauseGateContexts_.erase(existing);
        } else {
            if (context.phase == PauseGatePhase::BEGINNING) {
                return context.promise->GetFuture();
            }
            if (context.phase == PauseGatePhase::COMPENSATING_IDLE
                || context.phase == PauseGatePhase::COMPENSATING_TRAFFIC) {
                auto result = context.promise->GetFuture();
                if (!context.continuationPending && !context.retryScheduled) {
                    const auto token = context.token;
                    if (context.phase == PauseGatePhase::COMPENSATING_IDLE) {
                        RetryBeginPauseIdleCompensation(identity.instanceid(), token);
                    } else {
                        RetryBeginPauseTrafficCompensation(identity.instanceid(), token);
                    }
                }
                return result;
            }
            if (context.phase == PauseGatePhase::GATED && context.trafficGated && context.heartbeatStopped
                && context.idleGated) {
                return Status::OK();
            }
            if (context.phase != PauseGatePhase::RECOVERED) {
                return Status(StatusCode::ERR_INSTANCE_BUSY, "pause gate recovery is still in progress");
            }
            pauseGateContexts_.erase(existing);
        }
    }

    if (observer_ == nullptr) {
        return Status(StatusCode::FAILED, "control plane observer is not available");
    }
    PauseGateContext context;
    context.identity.CopyFrom(identity);
    context.phase = PauseGatePhase::BEGINNING;
    context.token = ++nextPauseGateToken_;
    context.gateToken = context.token;
    context.promise = std::make_shared<litebus::Promise<Status>>();
    auto result = context.promise->GetFuture();
    const auto token = context.token;
    pauseGateContexts_.emplace(identity.instanceid(), std::move(context));
    observer_->SetLocalPauseTrafficGate(identity, token, true)
        .OnComplete(litebus::Defer(GetAID(), &InstanceCtrlActor::OnBeginPauseTrafficGated, identity.instanceid(),
                                   token, _1));
    return result;
}

void InstanceCtrlActor::OnBeginPauseTrafficGated(const std::string &instanceID,
                                                 uint64_t token,
                                                 const litebus::Future<Status> &future)
{
    auto iter = pauseGateContexts_.find(instanceID);
    if (iter == pauseGateContexts_.end() || iter->second.token != token
        || iter->second.phase != PauseGatePhase::BEGINNING) {
        return;
    }
    if (CompletePauseGateOnFenceFailure(instanceID, token)) {
        return;
    }
    iter = pauseGateContexts_.find(instanceID);
    auto status = future.IsError()
                      ? Status(static_cast<StatusCode>(future.GetErrorCode()), "failed to close local pause traffic")
                      : future.Get();
    if (status.IsError()) {
        auto promise = iter->second.promise;
        pauseGateContexts_.erase(iter);
        promise->SetValue(status);
        return;
    }

    auto &context = iter->second;
    context.trafficGated = true;
    StopHeartbeat(instanceID);
    context.heartbeatStopped = true;
    if (idleMgr_ == nullptr) {
        CompensateBeginPauseGate(instanceID, token, Status(StatusCode::FAILED, "idle manager is not available"));
        return;
    }
    idleMgr_->SetPauseGated(context.identity, context.gateToken, true)
        .OnComplete(litebus::Defer(GetAID(), &InstanceCtrlActor::OnBeginPauseIdleGated, instanceID, token, _1));
}

void InstanceCtrlActor::OnBeginPauseIdleGated(const std::string &instanceID,
                                              uint64_t token,
                                              const litebus::Future<Status> &future)
{
    auto iter = pauseGateContexts_.find(instanceID);
    if (iter == pauseGateContexts_.end() || iter->second.token != token
        || iter->second.phase != PauseGatePhase::BEGINNING) {
        return;
    }
    if (CompletePauseGateOnFenceFailure(instanceID, token)) {
        return;
    }
    iter = pauseGateContexts_.find(instanceID);
    auto status = future.IsError()
                      ? Status(static_cast<StatusCode>(future.GetErrorCode()), "failed to close local idle gate")
                      : future.Get();
    if (status.IsError()) {
        CompensateBeginPauseGate(instanceID, token, status);
        return;
    }
    iter->second.idleGated = true;
    iter->second.phase = PauseGatePhase::GATED;
    iter->second.promise->SetValue(Status::OK());
}

void InstanceCtrlActor::CompensateBeginPauseGate(const std::string &instanceID, uint64_t token,
                                                 const Status &failure)
{
    auto iter = pauseGateContexts_.find(instanceID);
    if (iter == pauseGateContexts_.end() || iter->second.token != token
        || CompletePauseGateOnFenceFailure(instanceID, token)) {
        return;
    }
    iter = pauseGateContexts_.find(instanceID);
    iter->second.originalFailure = failure;
    if (idleMgr_ == nullptr) {
        iter->second.phase = PauseGatePhase::COMPENSATING_TRAFFIC;
        if (iter->second.heartbeatStopped) {
            StartHeartbeat(instanceID, 0, iter->second.identity.runtimeid(),
                           static_cast<StatusCode>(iter->second.identity.instancestatus().errcode()));
            iter->second.heartbeatStopped = false;
            iter->second.heartbeatStarted = true;
        }
        RetryBeginPauseTrafficCompensation(instanceID, token);
        return;
    }
    iter->second.phase = PauseGatePhase::COMPENSATING_IDLE;
    RetryBeginPauseIdleCompensation(instanceID, token);
}

void InstanceCtrlActor::RetryBeginPauseIdleCompensation(const std::string &instanceID, uint64_t token)
{
    auto iter = pauseGateContexts_.find(instanceID);
    if (iter == pauseGateContexts_.end() || iter->second.token != token
        || iter->second.phase != PauseGatePhase::COMPENSATING_IDLE || iter->second.continuationPending
        || CompletePauseGateOnFenceFailure(instanceID, token)) {
        return;
    }
    if (idleMgr_ == nullptr) {
        ScheduleBeginPauseCompensationRetry(instanceID, token, true, 0);
        return;
    }
    iter = pauseGateContexts_.find(instanceID);
    auto &context = iter->second;
    if (context.compensationAttemptsInFlight.size() >= PAUSE_GATE_COMPENSATION_MAX_IN_FLIGHT) {
        ScheduleBeginPauseCompensationRetry(instanceID, token, true, context.compensationAttempt);
        return;
    }
    context.continuationPending = true;
    const auto attempt = ++context.compensationAttempt;
    context.compensationAttemptsInFlight.insert(attempt);
    idleMgr_->SetPauseGated(context.identity, context.gateToken, false)
        .OnComplete(litebus::Defer(GetAID(), &InstanceCtrlActor::OnBeginPauseIdleRecovered, instanceID, token,
                                   attempt, _1));
    ScheduleBeginPauseCompensationRetry(instanceID, token, true, attempt);
}

void InstanceCtrlActor::OnBeginPauseIdleRecovered(const std::string &instanceID,
                                                  uint64_t token,
                                                  uint64_t attempt,
                                                  const litebus::Future<Status> &future)
{
    auto iter = pauseGateContexts_.find(instanceID);
    if (iter == pauseGateContexts_.end() || iter->second.token != token
        || iter->second.compensationAttemptsInFlight.erase(attempt) == 0) {
        return;
    }
    if (iter->second.phase != PauseGatePhase::COMPENSATING_IDLE
        || iter->second.compensationAttempt != attempt) {
        return;
    }
    iter->second.continuationPending = false;
    if (CompletePauseGateOnFenceFailure(instanceID, token)) {
        return;
    }
    auto status = future.IsError()
                      ? Status(static_cast<StatusCode>(future.GetErrorCode()), "failed to recover local idle gate")
                      : future.Get();
    if (status.IsError()) {
        return;
    }
    iter = pauseGateContexts_.find(instanceID);
    auto &context = iter->second;
    context.retryScheduled = false;
    context.idleGated = false;
    if (context.heartbeatStopped) {
        StartHeartbeat(instanceID, 0, context.identity.runtimeid(),
                       static_cast<StatusCode>(context.identity.instancestatus().errcode()));
        context.heartbeatStopped = false;
        context.heartbeatStarted = true;
    }
    context.phase = PauseGatePhase::COMPENSATING_TRAFFIC;
    RetryBeginPauseTrafficCompensation(instanceID, token);
}

void InstanceCtrlActor::RetryBeginPauseTrafficCompensation(const std::string &instanceID, uint64_t token)
{
    auto iter = pauseGateContexts_.find(instanceID);
    if (iter == pauseGateContexts_.end() || iter->second.token != token
        || iter->second.phase != PauseGatePhase::COMPENSATING_TRAFFIC || iter->second.continuationPending
        || CompletePauseGateOnFenceFailure(instanceID, token)) {
        return;
    }
    if (observer_ == nullptr) {
        ScheduleBeginPauseCompensationRetry(instanceID, token, false, 0);
        return;
    }
    iter = pauseGateContexts_.find(instanceID);
    auto &context = iter->second;
    if (context.compensationAttemptsInFlight.size() >= PAUSE_GATE_COMPENSATION_MAX_IN_FLIGHT) {
        ScheduleBeginPauseCompensationRetry(instanceID, token, false, context.compensationAttempt);
        return;
    }
    context.continuationPending = true;
    const auto attempt = ++context.compensationAttempt;
    context.compensationAttemptsInFlight.insert(attempt);
    observer_->SetLocalPauseTrafficGate(context.identity, context.gateToken, false)
        .OnComplete(litebus::Defer(GetAID(), &InstanceCtrlActor::FinishBeginPauseCompensation, instanceID, token,
                                   attempt, _1));
    ScheduleBeginPauseCompensationRetry(instanceID, token, false, attempt);
}

void InstanceCtrlActor::FinishBeginPauseCompensation(const std::string &instanceID,
                                                     uint64_t token,
                                                     uint64_t attempt,
                                                     const litebus::Future<Status> &future)
{
    auto iter = pauseGateContexts_.find(instanceID);
    if (iter == pauseGateContexts_.end() || iter->second.token != token
        || iter->second.compensationAttemptsInFlight.erase(attempt) == 0) {
        return;
    }
    if (iter->second.phase != PauseGatePhase::COMPENSATING_TRAFFIC
        || iter->second.compensationAttempt != attempt) {
        return;
    }
    iter->second.continuationPending = false;
    if (CompletePauseGateOnFenceFailure(instanceID, token)) {
        return;
    }
    auto status = future.IsError()
                      ? Status(static_cast<StatusCode>(future.GetErrorCode()), "failed to reopen local pause traffic")
                      : future.Get();
    if (status.IsError()) {
        return;
    }
    iter = pauseGateContexts_.find(instanceID);
    auto promise = iter->second.promise;
    auto originalFailure = iter->second.originalFailure;
    pauseGateContexts_.erase(iter);
    promise->SetValue(originalFailure);
}

void InstanceCtrlActor::ScheduleBeginPauseCompensationRetry(const std::string &instanceID, uint64_t token,
                                                            bool idlePhase, uint64_t watchdogAttempt)
{
    auto iter = pauseGateContexts_.find(instanceID);
    const auto expectedPhase = idlePhase ? PauseGatePhase::COMPENSATING_IDLE
                                         : PauseGatePhase::COMPENSATING_TRAFFIC;
    if (iter == pauseGateContexts_.end() || iter->second.token != token || iter->second.phase != expectedPhase
        || iter->second.retryScheduled) {
        return;
    }
    iter->second.retryScheduled = true;
    const auto retrySequence = ++iter->second.compensationRetrySequence;
    (void)litebus::AsyncAfter(PAUSE_GATE_COMPENSATION_RETRY_MS, GetAID(),
                              &InstanceCtrlActor::RunBeginPauseCompensationRetry, instanceID, token, idlePhase,
                              retrySequence, watchdogAttempt);
}

void InstanceCtrlActor::RunBeginPauseCompensationRetry(const std::string &instanceID, uint64_t token,
                                                       bool idlePhase, uint64_t retrySequence,
                                                       uint64_t watchdogAttempt)
{
    auto iter = pauseGateContexts_.find(instanceID);
    const auto expectedPhase = idlePhase ? PauseGatePhase::COMPENSATING_IDLE
                                         : PauseGatePhase::COMPENSATING_TRAFFIC;
    if (iter == pauseGateContexts_.end() || iter->second.token != token || iter->second.phase != expectedPhase
        || !iter->second.retryScheduled || iter->second.compensationRetrySequence != retrySequence) {
        return;
    }
    iter->second.retryScheduled = false;
    if (CompletePauseGateOnFenceFailure(instanceID, token)) {
        return;
    }
    iter = pauseGateContexts_.find(instanceID);
    if (watchdogAttempt != 0 && iter->second.compensationAttempt == watchdogAttempt
        && iter->second.continuationPending) {
        iter->second.continuationPending = false;
    }
    if (idlePhase) {
        RetryBeginPauseIdleCompensation(instanceID, token);
        return;
    }
    RetryBeginPauseTrafficCompensation(instanceID, token);
}

litebus::Future<Status> InstanceCtrlActor::RecoverPauseGate(const InstanceInfo &identity)
{
    auto validation = ValidatePauseGateIdentity(identity);
    if (validation.IsError()) {
        return validation;
    }
    auto iter = pauseGateContexts_.find(identity.instanceid());
    if (iter == pauseGateContexts_.end() || !IsSamePauseGateIdentity(iter->second.identity, identity)) {
        return Status(StatusCode::ERR_INSTANCE_INFO_INVALID, "pause gate context does not match current identity");
    }
    auto &context = iter->second;
    if (context.phase == PauseGatePhase::RECOVERED) {
        return Status::OK();
    }
    if (context.phase == PauseGatePhase::BEGINNING || context.phase == PauseGatePhase::RECOVERING) {
        return context.promise->GetFuture();
    }
    context.phase = PauseGatePhase::RECOVERING;
    context.token = ++nextPauseGateToken_;
    context.promise = std::make_shared<litebus::Promise<Status>>();
    auto result = context.promise->GetFuture();
    StartRecoverPauseGate(identity.instanceid(), context.token);
    return result;
}

void InstanceCtrlActor::StartRecoverPauseGate(const std::string &instanceID, uint64_t token)
{
    auto iter = pauseGateContexts_.find(instanceID);
    if (iter == pauseGateContexts_.end() || iter->second.token != token
        || CompletePauseGateOnFenceFailure(instanceID, token)) {
        return;
    }
    iter = pauseGateContexts_.find(instanceID);
    if (!iter->second.idleGated) {
        OnRecoverPauseIdleCleared(instanceID, token, litebus::Future<Status>(Status::OK()));
        return;
    }
    if (idleMgr_ == nullptr) {
        iter->second.phase = PauseGatePhase::GATED;
        iter->second.promise->SetValue(Status(StatusCode::FAILED, "idle manager is not available"));
        return;
    }
    idleMgr_->SetPauseGated(iter->second.identity, iter->second.gateToken, false)
        .OnComplete(litebus::Defer(GetAID(), &InstanceCtrlActor::OnRecoverPauseIdleCleared, instanceID, token, _1));
}

void InstanceCtrlActor::OnRecoverPauseIdleCleared(const std::string &instanceID,
                                                  uint64_t token,
                                                  const litebus::Future<Status> &future)
{
    auto iter = pauseGateContexts_.find(instanceID);
    if (iter == pauseGateContexts_.end() || iter->second.token != token
        || iter->second.phase != PauseGatePhase::RECOVERING) {
        return;
    }
    if (CompletePauseGateOnFenceFailure(instanceID, token)) {
        return;
    }
    iter = pauseGateContexts_.find(instanceID);
    auto status = future.IsError()
                      ? Status(static_cast<StatusCode>(future.GetErrorCode()), "failed to reopen local idle gate")
                      : future.Get();
    if (status.IsError()) {
        iter->second.phase = PauseGatePhase::GATED;
        iter->second.promise->SetValue(status);
        return;
    }
    auto &context = iter->second;
    context.idleGated = false;
    ReopenPauseTraffic(instanceID, token);
}

void InstanceCtrlActor::ReopenPauseTraffic(const std::string &instanceID, uint64_t token)
{
    auto iter = pauseGateContexts_.find(instanceID);
    if (iter == pauseGateContexts_.end() || iter->second.token != token
        || iter->second.phase != PauseGatePhase::RECOVERING
        || CompletePauseGateOnFenceFailure(instanceID, token)) {
        return;
    }
    if (observer_ == nullptr) {
        iter->second.phase = PauseGatePhase::GATED;
        iter->second.promise->SetValue(Status(StatusCode::FAILED, "control plane observer is not available"));
        return;
    }
    observer_->SetLocalPauseTrafficGate(iter->second.identity, iter->second.gateToken, false)
        .OnComplete(litebus::Defer(GetAID(), &InstanceCtrlActor::OnRecoverPauseTrafficOpened, instanceID, token, _1));
}

void InstanceCtrlActor::OnRecoverPauseTrafficOpened(const std::string &instanceID,
                                                    uint64_t token,
                                                    const litebus::Future<Status> &future)
{
    auto iter = pauseGateContexts_.find(instanceID);
    if (iter == pauseGateContexts_.end() || iter->second.token != token
        || iter->second.phase != PauseGatePhase::RECOVERING) {
        return;
    }
    if (CompletePauseGateOnFenceFailure(instanceID, token)) {
        return;
    }
    iter = pauseGateContexts_.find(instanceID);
    auto status = future.IsError()
                      ? Status(static_cast<StatusCode>(future.GetErrorCode()), "failed to reopen local pause traffic")
                      : future.Get();
    if (status.IsError()) {
        iter->second.phase = PauseGatePhase::GATED;
        iter->second.promise->SetValue(status);
        return;
    }
    auto &context = iter->second;
    if (!context.heartbeatStarted) {
        StartHeartbeat(instanceID, 0, context.identity.runtimeid(),
                       static_cast<StatusCode>(context.identity.instancestatus().errcode()));
        context.heartbeatStarted = true;
        context.heartbeatStopped = false;
    }
    context.trafficGated = false;
    context.phase = PauseGatePhase::RECOVERED;
    context.promise->SetValue(Status::OK());
}

}  // namespace functionsystem::local_scheduler
