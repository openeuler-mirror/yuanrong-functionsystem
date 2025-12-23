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

#include "async/async.hpp"
#include "async/asyncafter.hpp"
#include "async/defer.hpp"
#include "common/constants/actor_name.h"
#include "common/create_agent_decision/create_agent_decision.h"
#include "common/logs/logging.h"
#include "nlohmann/json.hpp"

namespace functionsystem::domain_scheduler {
const uint32_t MAX_REQUEST_RETRY_TIMES = 3;
const uint32_t CREATE_AGENT_RETRY_TIMES = 3;
const uint32_t MAX_RETRY_SCHEDULE_TIMES = 0;
using ScheduleResult = schedule_decision::ScheduleResult;
using Scheduler = schedule_decision::Scheduler;
litebus::Future<std::shared_ptr<messages::ScheduleResponse>> InstanceCtrlActor::Schedule(
    const std::shared_ptr<messages::ScheduleRequest> &req)
{
    ASSERT_IF_NULL(req);
    if (requestTrySchedTimes_.find(req->requestid()) == requestTrySchedTimes_.end()) {
        requestTrySchedTimes_[req->requestid()] = 0;
    }
    requestTrySchedTimes_[req->requestid()]++;

    schedulerQueueMap_[req->requestid()] = req;

    return ScheduleDecision(req);
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> InstanceCtrlActor::ScheduleDecision(
    const std::shared_ptr<messages::ScheduleRequest> &req)
{
    ASSERT_IF_NULL(scheduler_);
    auto requestID = req->requestid();
    uint64_t timeout = req->instance().scheduleoption().scheduletimeoutms();
    if (enableVerticalScale_) {
        auto createOpts = req->mutable_instance()->mutable_createoptions();
        (*createOpts)[ENABLE_VERTICAL_SCALE_KEY] = "true";
    }
    YRLOG_INFO("instance(req={}, priority={}, timeout={}, enableVerticalScale={}) schedule decision",
               requestID, req->instance().scheduleoption().priority(), timeout, enableVerticalScale_);
    auto cancelPromise = GetCancelTag(requestID);
    auto future = scheduler_->ScheduleDecision(req, cancelPromise->GetFuture());
    if (timeout > 0) {
        ASSERT_IF_NULL(recorder_);
        future = future.After(
            timeout,
            [requestID, recorder(recorder_), cancelPromise,
             timeout](const litebus::Future<ScheduleResult> &_1) -> litebus::Future<ScheduleResult> {
                std::string value = "\nthe instance cannot be scheduled within " + std::to_string(timeout) + " ms. ";
                return recorder->TryQueryScheduleErr(requestID)
                    .Then([value, cancelPromise](const Status &status) -> litebus::Future<ScheduleResult> {
                        if (cancelPromise->GetFuture().IsInit()) {
                            cancelPromise->SetFailed(static_cast<int32_t>(StatusCode::ERR_SCHEDULE_CANCELED));
                        }
                        if (!status.IsOk()) {
                            return ScheduleResult{
                                "", static_cast<int32_t>(status.StatusCode()), value + status.RawMessage(),
                                {}, "", {}, {}};
                        }
                        return ScheduleResult{
                            "", static_cast<int32_t>(StatusCode::ERR_SCHEDULE_CANCELED),
                            value + "the possible cause is that the scheduling queue is busy or the scheduling timeout"
                            " configuration is not proper.", {}, "", {}, {}};
                    });
            });
    }
    return future
        .Then(litebus::Defer(GetAID(), &InstanceCtrlActor::DispatchSchedule, std::placeholders::_1, req, 0))
        .Then(litebus::Defer(GetAID(), &InstanceCtrlActor::OnDispatchSchedule, std::placeholders::_1, req));
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> InstanceCtrlActor::DispatchSchedule(
    const litebus::Future<ScheduleResult> &result, const std::shared_ptr<messages::ScheduleRequest> &req,
    uint32_t dispatchTimes)
{
    schedulerQueueMap_.erase(req->requestid());
    auto schedResult = result.Get();
    if (schedResult.code == static_cast<int32_t>(StatusCode::INVALID_RESOURCE_PARAMETER)) {
        if (isHeader_) {
            schedResult.code = static_cast<int32_t>(StatusCode::PARAMETER_ERROR);
        } else {
            schedResult.code = static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH);
        }
    }

    if (schedResult.code == static_cast<int32_t>(StatusCode::PARAMETER_ERROR) ||
        schedResult.code == static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH) ||
        schedResult.code == static_cast<int32_t>(StatusCode::AFFINITY_SCHEDULE_FAILED) ||
        schedResult.code == static_cast<int32_t>(StatusCode::ERR_SCHEDULE_CANCELED) ||
        schedResult.code == static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID)) {
        return BuildErrorScheduleRsp(schedResult, req);
    }
    (void)cancelTag_.erase(req->requestid());
    YRLOG_DEBUG("{}|{}|scheduler({}) is selected", req->traceid(), req->requestid(), schedResult.id);
    auto promise = litebus::Promise<std::shared_ptr<messages::ScheduleResponse>>();
    ASSERT_IF_NULL(resourceViewMgr_);
    ASSERT_IF_NULL(underlayer_);
    req->mutable_candidateresult()->set_unitid(schedResult.unitID);
    (void)resourceViewMgr_->GetInf(resource_view::GetResourceType(req->instance()))
        ->GetUnitSnapshotInfo(schedResult.id)
        .Then([aid(GetAID()), req, underlayer(underlayer_), promise, schedResult,
               dispatchTimes](const resource_view::PullResourceRequest &snapshot) {
            req->mutable_unitsnapshot()->set_version(snapshot.version());
            req->mutable_unitsnapshot()->set_localviewinittime(snapshot.localviewinittime());
            (void)underlayer->DispatchSchedule(schedResult.id, req)
                .OnComplete(litebus::Defer(aid, &InstanceCtrlActor::CheckIsNeedReDispatch, std::placeholders::_1,
                                           promise, schedResult, req, dispatchTimes));
            return Status::OK();
        });

    return promise.GetFuture();
}

void InstanceCtrlActor::CheckIsNeedReDispatch(
    const litebus::Future<std::shared_ptr<messages::ScheduleResponse>> &rspFuture,
    const litebus::Promise<std::shared_ptr<messages::ScheduleResponse>> &promise, const ScheduleResult &schedResult,
    const std::shared_ptr<messages::ScheduleRequest> &req, uint32_t dispatchTimes)
{
    // Attempt to retry when the scheduling request fails util the under layer heartbeat lost
    if (rspFuture.IsError()) {
        YRLOG_WARN("{}|request {} scheduler to {} failed {} times. code {} ", req->traceid(), req->requestid(),
                   schedResult.id, dispatchTimes, rspFuture.GetErrorCode());
        promise.Associate(
            litebus::Async(GetAID(), &InstanceCtrlActor::DispatchSchedule, schedResult, req, dispatchTimes + 1));
        return;
    }
    const auto &rsp = rspFuture.Get();
    ASSERT_IF_NULL(rsp);
    ASSERT_IF_NULL(scheduler_);
    // update schedule context from underlayer
    *req->mutable_contexts() = rsp->contexts();
    // should be replaced by more specific errcode
    if (rsp->code() == static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH) ||
        rsp->code() == static_cast<int32_t>(StatusCode::ERR_RESOURCE_NOT_ENOUGH)) {
        rsp->set_code(static_cast<int32_t>(StatusCode::SCHEDULE_CONFLICTED));
    }
    promise.Associate(CheckReSchedulingIsRequired(rsp, req));
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> InstanceCtrlActor::CheckReSchedulingIsRequired(
    const std::shared_ptr<messages::ScheduleResponse> &rsp,
    const std::shared_ptr<messages::ScheduleRequest> &req)
{
    // Retry is required when a scheduling conflict occurs or the scheduler is unavailable while under layer abnormal
    // can be tolerated.
    if ((rsp->code() == static_cast<int32_t>(StatusCode::SCHEDULE_CONFLICTED) ||
         (isTolerateUnderlayerAbnormal_ &&
          rsp->code() == static_cast<int32_t>(StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER)))) {
        // schedule conflict don't count as a retry
        return ScheduleDecision(req);
    }
    (void)cancelTag_.erase(req->requestid());
    // SCHEDULE_CONFLICTED indicates that resources are insufficient.
    rsp->set_code(rsp->code() == static_cast<int32_t>(StatusCode::SCHEDULE_CONFLICTED)
                      ? static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH)
                      : rsp->code());
    YRLOG_INFO("{}|{}|schedule request response code: {} msg: {}", req->traceid(), req->requestid(), rsp->code(),
               rsp->message());
    return rsp;
}

void InstanceCtrlActor::UpdateMaxSchedRetryTimes(const uint32_t &retrys)
{
    maxSchedReTryTimes_ = retrys;
}

std::shared_ptr<messages::ScheduleResponse> InstanceCtrlActor::BuildErrorScheduleRsp(
    const schedule_decision::ScheduleResult &result, const std::shared_ptr<messages::ScheduleRequest> &req)
{
    auto rsp = std::make_shared<messages::ScheduleResponse>();
    if (result.code == static_cast<int32_t>(StatusCode::PARAMETER_ERROR)) {
        YRLOG_WARN(
            "{}|{}|invalid resource parameter, request resource is greater than each node's max resource. code: {} "
            "reason: {}",
            req->traceid(), req->requestid(), result.code, result.reason);
        rsp->set_code(static_cast<int32_t>(StatusCode::ERR_RESOURCE_CONFIG_ERROR));  // change to posix error code
        rsp->set_requestid(req->requestid());
        rsp->set_message("invalid resource parameter, request resource is greater than each node's max resource");
        return rsp;
    }

    if (result.code == static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH)) {
        YRLOG_WARN("{}|{}|could not find a suitable scheduler, code: {} reason: {}", req->traceid(), req->requestid(),
                   result.code, result.reason);
        rsp->set_code(static_cast<int32_t>(StatusCode::ERR_RESOURCE_NOT_ENOUGH));  // change to posix error code
        rsp->set_requestid(req->requestid());
        rsp->set_message(result.reason);
        return rsp;
    }

    if (result.code == static_cast<int32_t>(StatusCode::AFFINITY_SCHEDULE_FAILED)) {
        // in top domain, don't change to posix error code, retry later
        rsp->set_code(static_cast<int32_t>(isHeader_ ? StatusCode::AFFINITY_SCHEDULE_FAILED
                                                     : StatusCode::ERR_RESOURCE_NOT_ENOUGH));
        rsp->set_requestid(req->requestid());
        rsp->set_message(result.reason);
        return rsp;
    }

    if (result.code == static_cast<int32_t>(StatusCode::ERR_SCHEDULE_CANCELED) ||
        result.code == static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID)) {
        YRLOG_WARN("{}|{}|schedule is canceled. code: {} reason: {}",
                   req->traceid(), req->requestid(), result.code, result.reason);
        rsp->set_code(result.code);  // change to posix error code
        rsp->set_requestid(req->requestid());
        rsp->set_message(result.reason);
        return rsp;
    }
    YRLOG_ERROR("{}|{}|non-error response code: {} reason: {}", req->traceid(), req->requestid(), result.code,
                result.reason);
    return rsp;
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> InstanceCtrlActor::OnDispatchSchedule(
    litebus::Future<std::shared_ptr<messages::ScheduleResponse>> rsp,
    const std::shared_ptr<messages::ScheduleRequest> &req)
{
    if (rsp.IsError()) {
        YRLOG_ERROR("{}|{}|schedule failed, code({})", req->traceid(), req->requestid(), rsp.GetErrorCode());
        (void)cancelTag_.erase(req->requestid());
        return rsp;
    }
    auto scheduleRsp = rsp.Get();
    if (scheduleRsp->code() != static_cast<int32_t>(StatusCode::ERR_RESOURCE_CONFIG_ERROR) &&
        scheduleRsp->code() != static_cast<int32_t>(StatusCode::ERR_RESOURCE_NOT_ENOUGH) &&
        scheduleRsp->code() != static_cast<int32_t>(StatusCode::AFFINITY_SCHEDULE_FAILED)) {
        // success schedule or unknown error, such as INSTANCE_TRANSACTION_WRONG_VERSION don't retry
        (void)waitAgentCreatRetryTimes_.erase(req->requestid());
        (void)requestTrySchedTimes_.erase(req->requestid());
        (void)cancelTag_.erase(req->requestid());
        return rsp;
    }

    // retry create before retry wait,
    // don't create when AFFINITY_SCHEDULE_FAILED and No Affinity_Pool_ID in createOptions
    if (isHeader_ && (NeedCreateAgentInDomain(req->instance(), scheduleRsp->code()))) {
        auto retryTimes = waitAgentCreatRetryTimes_.find(req->requestid());
        if (retryTimes == waitAgentCreatRetryTimes_.end()) {
            YRLOG_INFO("{}|{}|could not find a suitable scheduler, try to create an agent", req->traceid(),
                       req->requestid());
            waitAgentCreatRetryTimes_[req->requestid()] = 0;
            CheckUUID(req);
            return litebus::Async(GetAID(), &InstanceCtrlActor::CreateAgent, req)
                .Then(litebus::Defer(GetAID(), &InstanceCtrlActor::OnCreateAgent, std::placeholders::_1, req,
                                     scheduleRsp));
        }
        auto confTimeout = req->instance().scheduleoption().initcalltimeout() * 1000;
        bool isTimeout = confTimeout > 0 ? (retryTimes->second * createAgentAwaitRetryInterval_ > confTimeout)
                                         : (retryTimes->second >= createAgentAwaitRetryTimes_);
        if (!isTimeout) {
            YRLOG_WARN("{}|{}|could not find a suitable scheduler, new agent is creating, try again", req->traceid(),
                       req->requestid());
            auto promise = litebus::Promise<std::shared_ptr<messages::ScheduleResponse>>();
            (void)litebus::AsyncAfter(createAgentAwaitRetryInterval_, GetAID(), &InstanceCtrlActor::RetrySchedule, req,
                                      promise);
            return promise.GetFuture();
        }
        YRLOG_ERROR("{}|{}|timeout to find a suitable scheduler", req->traceid(), req->requestid());
        (void)waitAgentCreatRetryTimes_.erase(req->requestid());
        (void)requestTrySchedTimes_.erase(req->requestid());
        (void)cancelTag_.erase(req->requestid());
        return rsp;
    }

    if (isHeader_ && requestTrySchedTimes_[req->requestid()] <= scheduleRetryTimes_ &&
        requestTrySchedTimes_[req->requestid()] >= 1) {
        req->set_scheduleround(req->scheduleround() >= UINT32_MAX ? 0 : req->scheduleround() + 1);
        auto promise = litebus::Promise<std::shared_ptr<messages::ScheduleResponse>>();
        (void)litebus::AsyncAfter(retryScheduleIntervals_[requestTrySchedTimes_[req->requestid()] - 1], GetAID(),
                                  &InstanceCtrlActor::RetrySchedule, req, promise);
        YRLOG_WARN("{}|{}|could not find a suitable scheduler, pod may be creating, retry times({}), try again",
                   req->traceid(), req->requestid(), requestTrySchedTimes_[req->requestid()]);
        return promise.GetFuture();
    }
    (void)waitAgentCreatRetryTimes_.erase(req->requestid());
    (void)requestTrySchedTimes_.erase(req->requestid());
    (void)cancelTag_.erase(req->requestid());
    return rsp;
}

void InstanceCtrlActor::CheckUUID(const std::shared_ptr<messages::ScheduleRequest> &req) const
{
    // scale pod by poolID, no need to bind UID
    if (NeedCreateAgentByPoolID(req->instance())) {
        return;
    }
    if (auto iter = req->instance().scheduleoption().resourceselector().find(RESOURCE_OWNER_KEY);
        iter == req->instance().scheduleoption().resourceselector().end() || iter->second == DEFAULT_OWNER_VALUE) {
        // change RESOURCE_OWNER_KEY from DEFAULT_OWNER_VALUE to uuid for affinity instance
        auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
        (*req->mutable_instance()->mutable_scheduleoption()->mutable_resourceselector())[RESOURCE_OWNER_KEY] =
            uuid.ToString();
        // the agent to be create is remarked a unique resource selector in the previous step.
        // Set schedule policy as monopoly to ensure that the new agent will be deleted after the ins is destroyed.
        req->mutable_instance()->mutable_scheduleoption()->set_schedpolicyname(MONOPOLY_SCHEDULE);
    }
}

void InstanceCtrlActor::RetrySchedule(const std::shared_ptr<messages::ScheduleRequest> &req,
                                      const litebus::Promise<std::shared_ptr<messages::ScheduleResponse>> &promise)
{
    YRLOG_DEBUG("{}|{}|retry schedule", req->traceid(), req->requestid());
    if (waitAgentCreatRetryTimes_.find(req->requestid()) != waitAgentCreatRetryTimes_.end()) {
        ++waitAgentCreatRetryTimes_[req->requestid()];
    }
    promise.Associate(litebus::Async(GetAID(), &InstanceCtrlActor::Schedule, req));
}

litebus::Future<std::shared_ptr<messages::CreateAgentResponse>> InstanceCtrlActor::CreateAgent(
    const std::shared_ptr<messages::ScheduleRequest> &req)
{
    auto request = std::make_shared<messages::CreateAgentRequest>();
    *request->mutable_instanceinfo() = *req->mutable_instance();

    auto promise = litebus::Promise<std::shared_ptr<messages::CreateAgentResponse>>();
    if (!isScalerEnabled_) {
        YRLOG_ERROR("{}|{}|scaler is not enabled", req->traceid(), req->requestid());
        auto rsp = std::make_shared<messages::CreateAgentResponse>();
        rsp->set_code(static_cast<int32_t>(StatusCode::FAILED));
        rsp->set_requestid(req->requestid());
        rsp->set_message("scaler is not enabled");
        promise.SetValue(rsp);
        return promise.GetFuture();
    }

    YRLOG_INFO("{}|{}|send create agent to {}", req->traceid(), req->requestid(), std::string(scaler_));
    (void)Send(scaler_, "CreateAgent", request->SerializeAsString());
    (void)createAgentPromises_.emplace(req->requestid(), promise);

    createAgentRetryTimers_[request->instanceinfo().requestid()] =
        litebus::AsyncAfter(createAgentRetryInterval_, GetAID(), &InstanceCtrlActor::RetryCreateAgent, request, 0);
    return promise.GetFuture();
}

void InstanceCtrlActor::RetryCreateAgent(const std::shared_ptr<messages::CreateAgentRequest> &req, uint32_t times)
{
    if (createAgentPromises_.find(req->instanceinfo().requestid()) == createAgentPromises_.end()) {
        YRLOG_DEBUG("request {} create agent has receive response, don't need to retry",
                    req->instanceinfo().requestid());
        return;
    }
    auto confTimeout = req->instanceinfo().scheduleoption().initcalltimeout() * 1000;
    bool isTimeout =
        confTimeout > 0 ? (times * createAgentRetryInterval_ > confTimeout) : (times > CREATE_AGENT_RETRY_TIMES);
    if (isTimeout) {
        YRLOG_ERROR("request {} create agent timeout", req->instanceinfo().requestid());
        auto createAgentRsp = std::make_shared<messages::CreateAgentResponse>();
        createAgentRsp->set_requestid(req->instanceinfo().requestid());
        createAgentRsp->set_code(static_cast<int32_t>(StatusCode::FAILED));
        createAgentRsp->set_message("create agent request(" + req->instanceinfo().requestid() + ") timeout");
        createAgentPromises_[req->instanceinfo().requestid()].SetValue(createAgentRsp);
        (void)createAgentPromises_.erase(req->instanceinfo().requestid());
        return;
    }

    YRLOG_INFO("request {} resend time({}) create agent to {}", req->instanceinfo().requestid(), times,
               std::string(scaler_));
    (void)Send(scaler_, "CreateAgent", req->SerializeAsString());
    createAgentRetryTimers_[req->instanceinfo().requestid()] =
        litebus::AsyncAfter(createAgentRetryInterval_, GetAID(), &InstanceCtrlActor::RetryCreateAgent, req, ++times);
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> InstanceCtrlActor::OnCreateAgent(
    const litebus::Future<std::shared_ptr<messages::CreateAgentResponse>> &createAgentRsp,
    const std::shared_ptr<messages::ScheduleRequest> &req,
    const std::shared_ptr<messages::ScheduleResponse> &scheduleRsp)
{
    if (createAgentRsp.IsError()) {
        YRLOG_ERROR("{}|{}|failed to get CreateAgentResponse", req->traceid(), req->requestid());
        (void)waitAgentCreatRetryTimes_.erase(req->requestid());
        (void)requestTrySchedTimes_.erase(req->requestid());
        (void)cancelTag_.erase(req->requestid());
        return scheduleRsp;
    }

    if (const auto &response = createAgentRsp.Get(); response->code() != static_cast<int32_t>(Status::OK())) {
        YRLOG_ERROR("{}|{}|create agent failed, code:{}, msg:{}", req->traceid(), req->requestid(), response->code(),
                    response->message());
        (void)waitAgentCreatRetryTimes_.erase(req->requestid());
        (void)requestTrySchedTimes_.erase(req->requestid());
        (void)cancelTag_.erase(req->requestid());
        // scale up by poolID, just return original response, not set createAgent resp
        if (!NeedCreateAgentByPoolID(req->instance())) {
            scheduleRsp->set_code(response->code());
            scheduleRsp->set_message(response->message());
        }
        return scheduleRsp;
    }

    req->clear_updateresources();
    req->set_scheduleround(req->scheduleround() >= UINT32_MAX ? 0 : req->scheduleround() + 1);

    // scale up by poolID, no need to clear createOptions
    if (!NeedCreateAgentByPoolID(req->instance())) {
        // reset createOptions
        req->mutable_instance()->mutable_createoptions()->clear();
        for (const auto &iter : createAgentRsp.Get()->updatedcreateoptions()) {
            (*req->mutable_instance()->mutable_createoptions())[iter.first] = iter.second;
        }
    }
    YRLOG_INFO("{}|{}|handle create agent response", req->traceid(), req->requestid());
    return litebus::Async(GetAID(), &InstanceCtrlActor::Schedule, req);
}

void InstanceCtrlActor::CreateAgentResponse(const litebus::AID &from, std::string && /* name */, std::string &&msg)
{
    auto createAgentResponse = std::make_shared<messages::CreateAgentResponse>();
    if (!createAgentResponse->ParseFromString(msg)) {
        YRLOG_ERROR("received create agent response from {}, invalid msg {}", std::string(from), msg);
        return;
    }

    auto requestID = createAgentResponse->requestid();
    auto promise = createAgentPromises_.find(requestID);
    if (promise == createAgentPromises_.end()) {
        YRLOG_ERROR("request {} create agent promise not found", requestID);
        return;
    }

    YRLOG_INFO("request {} receive create agent response", requestID);
    promise->second.SetValue(createAgentResponse);
    (void)createAgentPromises_.erase(requestID);
    if (createAgentRetryTimers_.find(requestID) != createAgentRetryTimers_.end()) {
        (void)litebus::TimerTools::Cancel(createAgentRetryTimers_[requestID]);
        (void)createAgentRetryTimers_.erase(requestID);
    }
}

void InstanceCtrlActor::Init()
{
    Receive("CreateAgentResponse", &InstanceCtrlActor::CreateAgentResponse);
}

void InstanceCtrlActor::SetScalerAddress(const std::string &address)
{
    scaler_.SetName(SCALER_ACTOR);
    scaler_.SetUrl(address);
    isScalerEnabled_ = true;
}

void InstanceCtrlActor::TryCancelSchedule(const std::shared_ptr<messages::CancelSchedule> &cancelRequest)
{
    if (auto it = cancelTag_.find(cancelRequest->id());
        it != cancelTag_.end() && it->second != nullptr && it->second->GetFuture().IsInit()) {
        YRLOG_INFO("{}|try cancel schedule instance, reason:({})", cancelRequest->id(), cancelRequest->reason());
        cancelTag_[cancelRequest->id()]->SetValue(cancelRequest->reason());
        (void)cancelTag_.erase(it);
    }
}

std::shared_ptr<litebus::Promise<std::string>> InstanceCtrlActor::GetCancelTag(const std::string &requestId)
{
    if (cancelTag_.find(requestId) == cancelTag_.end()) {
        cancelTag_[requestId] = std::make_shared<litebus::Promise<std::string>>();
    }
    return cancelTag_[requestId];
}
}  // namespace functionsystem::domain_scheduler