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
#include "underlayer_sched_mgr_actor.h"

#include <async/async.hpp>
#include <async/asyncafter.hpp>
#include <async/defer.hpp>

#include "common/constants/actor_name.h"
#include "common/logs/logging.h"
#include "common/resource_view/resource_tool.h"
#include "common/utils/meta_store_kv_operation.h"
#include "common/metrics/metrics_adapter.h"

namespace functionsystem::domain_scheduler {
const uint64_t REGISTER_TIMEOUT = 60000;
const uint64_t NOTIFY_TIMEOUT = 15000;
const uint32_t GROUP_TIMEOUT = 5000;
void UnderlayerScheduler::AddRegisterTimer(const litebus::AID &aid, uint64_t timeOutMs)
{
    messages::NotifySchedAbnormalRequest req;
    req.set_schedname(name_);
    req.set_ip(GetIPFromAddress(address_));
    registerTimeOut_ = litebus::AsyncAfter(timeOutMs, aid, &UnderlayerSchedMgrActor::NotifyAbnormal, req);
}

UnderlayerScheduler::~UnderlayerScheduler()
{
    try {
        (void)litebus::TimerTools::Cancel(registerTimeOut_);
        if (resourceClearCallBack_) {
            resourceClearCallBack_(name_);
        }
    } catch (std::exception &e) {
        YRLOG_WARN("~UnderlayerScheduler exception e.what():{}", e.what());
    }
}

void UnderlayerScheduler::Registered(const litebus::AID &aid)
{
    (void)litebus::TimerTools::Cancel(registerTimeOut_);
    registered_.SetValue(true);
    aid_ = aid;
}

void UnderlayerSchedMgrActor::UpdateUnderlayerTopo(const messages::ScheduleTopology &req)
{
    std::unordered_map<std::string, std::shared_ptr<UnderlayerScheduler>> underlayers;
    for (auto i = 0; i < req.members_size(); i++) {
        std::string name = req.members(i).name();
        std::string address = req.members(i).address();
        if (underlayers_.find(name) != underlayers_.end() && underlayers_[name] != nullptr &&
            underlayers_[name]->GetAddress() == address) {
            underlayers[name] = underlayers_[name];
            continue;
        }
        YRLOG_INFO("update new underlayer name {} address {}", name, address);
        auto underlayerSched =
            std::make_shared<UnderlayerScheduler>(name, address, heartbeatMaxTimes_, heartbeatInterval_);

        // register timeout must be longer than heartbeat timeout
        underlayerSched->AddRegisterTimer(GetAID(), heartbeatMaxTimes_ * heartbeatInterval_);
        underlayers[name] = underlayerSched;
    }
    underlayers_ = std::move(underlayers);
    ASSERT_IF_NULL(instanceCtrl_);
    instanceCtrl_->UpdateMaxSchedRetryTimes(underlayers_.size());
}

UnderlayerSchedMgrActor::UnderlayerSchedMgrActor(const std::string &name)
    : ActorBase(name + DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX)
{
}

UnderlayerSchedMgrActor::UnderlayerSchedMgrActor(const std::string &name, const uint32_t &heartbeatTimes,
                                                 const uint32_t heartbeatInterval, const uint32_t &groupTimeout)
    : ActorBase(name + DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX),
      heartbeatMaxTimes_(heartbeatTimes),
      heartbeatInterval_(heartbeatInterval),
      groupTimeout_(groupTimeout ? groupTimeout : GROUP_TIMEOUT)
{
}

void UnderlayerSchedMgrActor::DeletePod(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto deletePodRequest = std::make_shared<messages::DeletePodRequest>();
    if (!deletePodRequest->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse request for DeletePod.");
        return;
    }
    if (!isScalerEnabled_) {
        auto resp = std::make_shared<messages::DeletePodResponse>();
        resp->set_code(0);
        Send(from, "DeletePodResponse", resp->SerializeAsString());
        YRLOG_WARN("failed to delete pod, scaler is not enabled");
        return;
    }
    auto future = deletePodMatch_.AddSynchronizer(deletePodRequest->requestid());
    (void)Send(scaler_, "DeletePod", deletePodRequest->SerializeAsString());
    future.OnComplete(
        litebus::Defer(GetAID(), &UnderlayerSchedMgrActor::OnDeletePodComplete, std::placeholders::_1, from));
}

void UnderlayerSchedMgrActor::DeletePodResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto deletePodResponse = std::make_shared<messages::DeletePodResponse>();
    if (!deletePodResponse->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse request for DeletePod.");
        return;
    }
    YRLOG_DEBUG("{}|receive delete pod response from {}, code is {}", deletePodResponse->requestid(), from.HashString(),
                deletePodResponse->code());
    deletePodMatch_.Synchronized(deletePodResponse->requestid(), deletePodResponse);
}

void UnderlayerSchedMgrActor::OnDeletePodComplete(
    const litebus::Future<std::shared_ptr<messages::DeletePodResponse>> &rsp, const litebus::AID &from)
{
    if (rsp.IsError()) {
        return;
    }
    (void)Send(from, "DeletePodResponse", rsp.Get()->SerializeAsString());
}

void UnderlayerSchedMgrActor::ResponsePreemptInstance(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::EvictAgentAck rsp;
    if (!rsp.ParseFromString(msg)) {
        YRLOG_ERROR("invalid Notify response {}. ignored", msg);
        return;
    }
    YRLOG_INFO("{}|receive preempt response", rsp.requestid());
    preemptInstanceSync_.Synchronized(rsp.requestid(), Status::OK());
}

void UnderlayerSchedMgrActor::SetScalerAddress(const std::string &address)
{
    scaler_.SetName(SCALER_ACTOR);
    scaler_.SetUrl(address);
    isScalerEnabled_ = true;
}

int UnderlayerSchedMgrActor::AddNewHeartbeatNode(const HeartbeatObserver::TimeOutHandler &handler,
                                                 std::shared_ptr<UnderlayerScheduler> underlayer)
{
    if (heartbeatObserver_ == nullptr) {
        heartbeatObserver_ = std::make_shared<HeartbeatObserveDriver>(DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME);
    }

    litebus::AID dst;
    dst.SetName(HEARTBEAT_CLIENT_BASENAME
                + litebus::os::Join(COMPONENT_NAME_FUNCTION_PROXY, underlayer->GetName(), '-'));
    dst.SetUrl(underlayer->GetAddress());
    dst.SetProtocol(litebus::BUS_TCP);
    heartbeatObserver_->AddNewHeartbeatNode(dst, heartbeatMaxTimes_ * heartbeatInterval_, handler);
    return 0;
}

void UnderlayerSchedMgrActor::Register(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::Register req;
    if (!req.ParseFromString(msg)) {
        YRLOG_WARN("invalid register msg from {} msg {}", std::string(from), msg);
        return;
    }

    YRLOG_INFO("received register from {} msg {}", std::string(from), req.ShortDebugString());
    messages::Registered rsp;
    rsp.set_code(static_cast<int32_t>(StatusCode::FAILED));
    if (underlayers_.find(req.name()) == underlayers_.end() || underlayers_[req.name()] == nullptr ||
        underlayers_[req.name()]->GetAddress() != from.Url()) {
        YRLOG_WARN("unknown register from {} name {}", std::string(from), req.name());
        rsp.set_message("the register name was not found in domain topology.");
        (void)Send(from, "Registered", rsp.SerializeAsString());
        return;
    }
    ASSERT_IF_NULL(resourceViewMgr_);
    auto registered = underlayers_[req.name()]->IsRegistered();
    if (registered.IsOK()) {
        if (registered.Get()) {
            YRLOG_INFO("{} already registered", std::string(from));
            rsp.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
            (void)Send(from, "Registered", rsp.SerializeAsString());
            return;
        }
    }

    std::shared_ptr<UnderlayerScheduler> underlayer = underlayers_[req.name()];

    auto ret = AddNewHeartbeatNode(
        [name(req.name()), address(req.address()), aid(GetAID())](const litebus::AID &from,
                                                                  functionsystem::HeartbeatConnection) {
            YRLOG_WARN("heartbeat timeout, from proxy to domain. from: {}.", std::string(from));
            litebus::Async(aid, &UnderlayerSchedMgrActor::HeartbeatLost, name, address);
        },
        underlayer);
    if (ret != static_cast<int>(StatusCode::SUCCESS)) {
        rsp.set_message("failed to build heartbeat");
        (void)Send(from, "Registered", rsp.SerializeAsString());
        return;
    }
    underlayers_[req.name()]->Registered(from);
    rsp.set_code(static_cast<int32_t>(StatusCode::SUCCESS));
    (void)Send(from, "Registered", rsp.SerializeAsString());
    // update resource
    (void)resourceViewMgr_->RegisterResourceUnit(req, from.Url());
    underlayers_[req.name()]->RegisterResourceClearCallback(
        [resource(resourceViewMgr_)](const std::string &id) { (void)resource->UnRegisterResourceUnit(id); });
}

void UnderlayerSchedMgrActor::HeartbeatLost(const std::string &name, const std::string &address)
{
    if (underlayers_.find(name) == underlayers_.end()) {
        YRLOG_INFO("{} NOT FOUND.", name);
        return;
    }
    messages::NotifySchedAbnormalRequest req;
    req.set_schedname(name);
    req.set_ip(GetIPFromAddress(address));
    (void)litebus::Async(GetAID(), &UnderlayerSchedMgrActor::AsyncNotifyAbnormal, req);
}

void UnderlayerSchedMgrActor::ForwardSchedule(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto req = std::make_shared<messages::ScheduleRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_WARN("invalid ForwardSchedule msg from {}, ignored", std::string(from));
        return;
    }
    if (recivedSchedulingReq_.find(req->requestid()) != recivedSchedulingReq_.end()) {
        YRLOG_WARN("{}|{}|received repeated ForwardSchedule request from {}. ignore it", req->traceid(),
                   req->requestid(), std::string(from));
        return;
    }
    (void)recivedSchedulingReq_.insert(req->requestid());
    YRLOG_INFO("{}|{}|received ForwardSchedule request from {}.", req->traceid(), req->requestid(), std::string(from));
    ASSERT_IF_NULL(resourceViewMgr_);
    for (auto &[type, resource] : *req->mutable_updateresources()) {
        auto changes = std::make_shared<resource_view::ResourceUnitChanges>(std::move(resource));
        (void)resourceViewMgr_->GetInf(static_cast<resource_view::ResourceType>(type))
            ->UpdateResourceUnitDelta(changes);
    }
    req->clear_updateresources();
    ASSERT_IF_NULL(instanceCtrl_);
    // When the top-level scheduler receives a forwarded request, it needs to add scheduling rounds. This is because the
    // global information is more complete and may still be scheduled to the same underlayer. Adding rounds can prevent
    // the request from being filtered by the underlayer.
    if (isHeader_) {
        req->set_scheduleround(req->scheduleround() >= UINT32_MAX ? 0 : req->scheduleround() + 1);
    }
    instanceCtrl_->Schedule(req)
        .Then(litebus::Defer(GetAID(), &UnderlayerSchedMgrActor::CheckForwardUplayer, req, std::placeholders::_1))
        .OnComplete(litebus::Defer(GetAID(), &UnderlayerSchedMgrActor::ForwardScheduleCallback, from, req,
                                   std::placeholders::_1));
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> UnderlayerSchedMgrActor::CheckForwardUplayer(
    const std::shared_ptr<messages::ScheduleRequest> &req,
    const litebus::Future<std::shared_ptr<messages::ScheduleResponse>> &rspFuture)
{
    const auto &rsp = rspFuture.Get();
    ASSERT_IF_NULL(rsp);

    // if version is wrong, need driver try to reschedule
    if (rsp->code() == static_cast<int32_t>(StatusCode::SUCCESS) ||
        rsp->code() == static_cast<int32_t>(StatusCode::INSTANCE_TRANSACTION_WRONG_VERSION)) {
        return rsp;
    }

    ASSERT_IF_NULL(domainSrv_);
    return domainSrv_->ForwardSchedule(req).Then(
        [rsp](const litebus::Future<std::shared_ptr<messages::ScheduleResponse>> &rspFuture)
            -> litebus::Future<std::shared_ptr<messages::ScheduleResponse>> {
            const auto &forwardRsp = rspFuture.Get();
            if (forwardRsp == nullptr) {
                YRLOG_ERROR("failed to get response of forward schedule, rsp is nullptr");
                return rsp;
            }
            if (forwardRsp->code() == StatusCode::DOMAIN_SCHEDULER_FORWARD_ERR) {
                return rsp;
            }
            return rspFuture;
        });
}

void UnderlayerSchedMgrActor::ForwardScheduleCallback(
    const litebus::AID &to, const std::shared_ptr<messages::ScheduleRequest> &req,
    const litebus::Future<std::shared_ptr<messages::ScheduleResponse>> &rspFuture)
{
    (void)recivedSchedulingReq_.erase(req->requestid());
    if (rspFuture.IsError()) {
        YRLOG_ERROR("{}|{}|send ForwardSchedule error response to {}. instance: {}, code: {}", req->traceid(),
                    req->requestid(), std::string(to), req->instance().instanceid(), rspFuture.GetErrorCode());
        messages::ScheduleResponse rsp;
        rsp.set_code(rspFuture.GetErrorCode());
        rsp.set_requestid(req->requestid());
        rsp.set_message("schedule request failed in domain");
        (void)Send(to, "ResponseForwardSchedule", rsp.SerializeAsString());
        return;
    }
    YRLOG_INFO("{}|{}|send ForwardSchedule ok response to {}. instance: {}", req->traceid(), req->requestid(),
               std::string(to), req->instance().instanceid());

    auto rsp = rspFuture.Get();
    ASSERT_IF_NULL(rsp);
    (void)Send(to, "ResponseForwardSchedule", rsp->SerializeAsString());
}

void UnderlayerSchedMgrActor::NotifySchedAbnormal(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::NotifySchedAbnormalRequest req;
    if (!req.ParseFromString(msg)) {
        YRLOG_WARN("invalid notify abnormal request msg from {} msg {}", std::string(from), msg);
        return;
    }
    YRLOG_INFO("receive from {} report sched({}) ip({}) abnormal", std::string(from), req.schedname(), req.ip());
    litebus::Async(GetAID(), &UnderlayerSchedMgrActor::AsyncNotifyAbnormal, req)
        .OnComplete(litebus::Defer(GetAID(), &UnderlayerSchedMgrActor::NotifySchedAbnormalCallBack, from, req));
}

void UnderlayerSchedMgrActor::NotifySchedAbnormalCallBack(const litebus::AID &to,
                                                          const messages::NotifySchedAbnormalRequest &req)
{
    messages::NotifySchedAbnormalResponse rsp;
    rsp.set_schedname(req.schedname());
    (void)Send(to, "ResponseNotifySchedAbnormal", rsp.SerializeAsString());
}

void UnderlayerSchedMgrActor::NotifyWorkerStatusCallBack(const litebus::Future<Status> &status, const litebus::AID &to,
                                                         const messages::NotifyWorkerStatusRequest &req)
{
    if (status.IsError()) {
        YRLOG_ERROR("failed to notify worker status to uplayer scheduler worker({}) healthy({})", req.workerip(),
                    req.healthy());
        return;
    }
    messages::NotifyWorkerStatusResponse rsp;
    rsp.set_workerip(req.workerip());
    rsp.set_healthy(req.healthy());
    (void)Send(to, "ResponseNotifyWorkerStatus", rsp.SerializeAsString());
}

void UnderlayerSchedMgrActor::NotifyWorkerStatus(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::NotifyWorkerStatusRequest req;
    if (!req.ParseFromString(msg)) {
        YRLOG_WARN("invalid notify abnormal request msg from {} msg {}", std::string(from), msg);
        return;
    }
    YRLOG_INFO("receive from {} report worker({}) healthy({})", std::string(from), req.workerip(), req.healthy());
    RETURN_IF_NULL(domainSrv_);
    domainSrv_->NotifyWorkerStatus(req).OnComplete(litebus::Defer(
        GetAID(), &UnderlayerSchedMgrActor::NotifyWorkerStatusCallBack, std::placeholders::_1, from, req));
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> UnderlayerSchedMgrActor::DispatchSchedule(
    const std::string &selectedName, const std::shared_ptr<messages::ScheduleRequest> &req)
{
    ASSERT_IF_NULL(req);

    if (underlayers_.find(selectedName) == underlayers_.end() || underlayers_[selectedName] == nullptr) {
        YRLOG_ERROR("{}|{}|failed to dispatch schedule. not found scheduler named {}.", req->traceid(),
                    req->requestid(), selectedName);
        auto rsp = std::make_shared<messages::ScheduleResponse>();
        rsp->set_code(static_cast<int32_t>(StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER));
        rsp->set_message("local scheduler " + selectedName + " is abnormal");
        rsp->set_requestid(req->requestid());
        return rsp;
    }
    YRLOG_INFO("{}|{}|dispatch schedule request to {}.", req->traceid(), req->requestid(), selectedName);
    const auto &aid = underlayers_[selectedName]->GetAID();
    InsertUnfinishedScheduleRequest(aid.Url(), req->requestid());
    auto future = requestMatch_.AddSynchronizer(aid.Url() + req->requestid());
    (void)future.OnComplete([aid(GetAID()), underlayer(aid.Url()), requestID(req->requestid())]() {
        litebus::Async(aid, &UnderlayerSchedMgrActor::DeleteUnfinishedScheduleReqeust, underlayer, requestID);
    });
    (void)Send(aid, "Schedule", req->SerializeAsString());
    return future;
}

void UnderlayerSchedMgrActor::DeleteUnfinishedScheduleReqeust(const std::string &to, const std::string &requestID)
{
    if (auto iter(unfinishedScheduleReqs_.find(to)); iter != unfinishedScheduleReqs_.end()) {
        (void)iter->second.erase(requestID);
    }
}

void UnderlayerSchedMgrActor::InsertUnfinishedScheduleRequest(const std::string &to, const std::string &requestID)
{
    (void)unfinishedScheduleReqs_[to].insert(requestID);
}

void UnderlayerSchedMgrActor::ClearAbnormalUnfinishedCache(const std::string &schedName)
{
    if (underlayers_.find(schedName) == underlayers_.end() || underlayers_[schedName] == nullptr) {
        return;
    }
    const auto &aid = underlayers_[schedName]->GetAID();
    if (unfinishedScheduleReqs_.find(aid.Url()) != unfinishedScheduleReqs_.end()) {
        auto requests = unfinishedScheduleReqs_[aid.Url()];
        auto rsp = std::make_shared<messages::ScheduleResponse>();
        rsp->set_code(static_cast<int32_t>(StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER));
        rsp->set_message("local scheduler " + schedName + " is abnormal");
        for (auto requestID : requests) {
            rsp->set_requestid(requestID);
            YRLOG_INFO("local scheduler ({}) is abnormaled. unfinished schedule request ({}) will be responsed.",
                       schedName, requestID);
            (void)requestMatch_.Synchronized(aid.Url() + requestID, rsp);
        }
        (void)unfinishedScheduleReqs_.erase(aid.Url());
    }
    (void)underlayers_.erase(schedName);
    ASSERT_IF_NULL(instanceCtrl_);
    instanceCtrl_->UpdateMaxSchedRetryTimes(underlayers_.size());
}

void UnderlayerSchedMgrActor::ResponseSchedule(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto rsp = std::make_shared<messages::ScheduleResponse>();
    if (!rsp->ParseFromString(msg)) {
        YRLOG_WARN("invalid schedule response from {} msg {}, ignored", std::string(from), msg);
        return;
    }
    ASSERT_IF_NULL(resourceViewMgr_);
    for (auto &[type, resource] : *rsp->mutable_updateresources()) {
        auto changes = std::make_shared<resource_view::ResourceUnitChanges>(std::move(resource));
        (void)resourceViewMgr_->GetInf(static_cast<resource_view::ResourceType>(type))
            ->UpdateResourceUnitDelta(changes);
    }
    rsp->clear_updateresources();
    auto status = requestMatch_.Synchronized(from.Url() + rsp->requestid(), rsp);
    if (status.IsError()) {
        YRLOG_WARN("{}|received schedule response from {}. code {} msg {}. no found request ignore it",
                   rsp->requestid(), rsp->code(), rsp->message(), from.HashString());
        return;
    }
    if (rsp->code() == 0) {
        YRLOG_INFO("{}|received schedule ok response. from {}", rsp->requestid(), from.HashString());
    } else {
        YRLOG_WARN("{}|received schedule error response. code {} message {}. from {}", rsp->requestid(), rsp->code(),
                   rsp->message(), from.HashString());
    }
}

void UnderlayerSchedMgrActor::NotifyAbnormal(const messages::NotifySchedAbnormalRequest &req)
{
    YRLOG_WARN("wait under scheduler({}) ip({}) register timeout", req.schedname(), req.ip());
    (void)litebus::Async(GetAID(), &UnderlayerSchedMgrActor::AsyncNotifyAbnormal, req);
}

litebus::Future<Status> UnderlayerSchedMgrActor::AsyncNotifyAbnormal(const messages::NotifySchedAbnormalRequest &req)
{
    metrics::MetricsAdapter::GetInstance().SendSchedulerAlarm(req.schedname() + "," + req.ip());
    ASSERT_IF_NULL(domainSrv_);
    return domainSrv_->NotifySchedAbnormal(req).OnComplete(
        [aid(GetAID()), req](const litebus::Future<Status> &statusFut) -> litebus::Future<Status> {
            if (statusFut.IsError()) {
                YRLOG_ERROR("notify schedule abnormal failed. report sched({}) ip({}) abnormal code:{}, retrying",
                            req.schedname(), req.ip(), statusFut.GetErrorCode());
                return litebus::Async(aid, &UnderlayerSchedMgrActor::AsyncNotifyAbnormal, req);
            }
            litebus::Async(aid, &UnderlayerSchedMgrActor::ClearAbnormalUnfinishedCache, req.schedname());
            return Status::OK();
        });
}

void UnderlayerSchedMgrActor::PreemptInstance(const std::vector<schedule_decision::PreemptResult> &preemptResults)
{
    std::unordered_map<std::string, std::shared_ptr<messages::EvictAgentRequest>> evictMap;
    for (auto &preemptResult : preemptResults) {
        if (preemptResult.status.IsError() || preemptResult.unitID.empty() ||
            preemptResult.preemptedInstances.empty()) {
            continue;
        }
        auto proxyID = preemptResult.ownerID;
        for (auto &ins : preemptResult.preemptedInstances) {
            if (evictMap.find(proxyID) == evictMap.end()) {
                evictMap[proxyID] = std::make_shared<messages::EvictAgentRequest>();
                evictMap[proxyID]->set_requestid(litebus::uuid_generator::UUID::GetRandomUUID().ToString());
                evictMap[proxyID]->set_timeoutsec(UINT32_MAX);
            }
            evictMap[proxyID]->add_instances(ins.instanceid());
        }
    }
    if (evictMap.empty()) {
        return;
    }
    for (auto iter : evictMap) {
        if (underlayers_.find(iter.first) == underlayers_.end() || underlayers_[iter.first] == nullptr) {
            YRLOG_WARN("failed to get proxyID of {}", iter.first);
            continue;
        }
        const auto &aid = underlayers_[iter.first]->GetAID();
        (void)AsyncPreemptInstance(aid, iter.second);
    }
}

litebus::Future<Status> UnderlayerSchedMgrActor::AsyncPreemptInstance(
    const litebus::AID &proxyID, const std::shared_ptr<messages::EvictAgentRequest> &req)
{
    auto future = preemptInstanceSync_.AddSynchronizer(req->requestid());
    YRLOG_INFO("{}|send preempt instance request to {}", req->requestid(), proxyID.HashString());
    (void)Send(proxyID, "PreemptInstances", req->SerializeAsString());
    return future.OnComplete(
        [aid(GetAID()), proxyID, req](const litebus::Future<Status> &statusFut) -> litebus::Future<Status> {
            if (statusFut.IsError()) {
                YRLOG_WARN("{}|failed to preempt instance request, code is {}, retrying", req->requestid(),
                           statusFut.GetErrorCode());
                return litebus::Async(aid, &UnderlayerSchedMgrActor::AsyncPreemptInstance, proxyID, req);
            }
            return Status::OK();
        });
}

litebus::Future<bool> UnderlayerSchedMgrActor::IsRegistered(const std::string &name)
{
    if (auto iter(underlayers_.find(name)); iter != underlayers_.end() && iter->second != nullptr) {
        return iter->second->IsRegistered();
    }
    return false;
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> UnderlayerSchedMgrActor::Reserve(
    const std::string &selectedName, const std::shared_ptr<messages::ScheduleRequest> &req)
{
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<messages::ScheduleResponse>>>();
    DoReserve(promise, selectedName, req);
    return promise->GetFuture();
}

void UnderlayerSchedMgrActor::DoReserve(
    const std::shared_ptr<litebus::Promise<std::shared_ptr<messages::ScheduleResponse>>> &promise,
    const std::string &selectedName, const std::shared_ptr<messages::ScheduleRequest> &req)
{
    if (underlayers_.find(selectedName) == underlayers_.end() || underlayers_[selectedName] == nullptr) {
        YRLOG_ERROR("{}|{}|failed to reserve instance({}). not found scheduler named {}.", req->traceid(),
                    req->requestid(), req->instance().instanceid(), req->instance().groupid());
        auto rsp = std::make_shared<messages::ScheduleResponse>();
        rsp->set_code(static_cast<int32_t>(StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER));
        rsp->set_message("failed to reserve, because of local scheduler " + selectedName + " is abnormal");
        rsp->set_requestid(req->requestid());
        promise->SetValue(rsp);
        return;
    }
    YRLOG_INFO("{}|{}|reserve instance({}) of group({}) resource to {}.", req->traceid(), req->requestid(),
               req->instance().instanceid(), req->instance().groupid(), selectedName);
    const auto &aid = underlayers_[selectedName]->GetAID();
    litebus::AID localAid(req->instance().scheduleoption().target() == resources::CreateTarget::RESOURCE_GROUP
                              ? "BundleMgrActor"
                              : LOCAL_GROUP_CTRL_ACTOR_NAME,
                          aid.Url());
    auto future = requestReserveMatch_.AddSynchronizer(localAid.Url() + req->requestid());
    Send(localAid, "Reserve", req->SerializeAsString());
    future.OnComplete([promise, selectedName, req,
                       aid(GetAID())](const litebus::Future<std::shared_ptr<messages::ScheduleResponse>> &future) {
        if (future.IsError()) {
            YRLOG_WARN("{}|{}|reserve instance({}) of group({}) resource to {} timeout.", req->traceid(),
                       req->requestid(), req->instance().instanceid(), req->instance().groupid(), selectedName);
            litebus::Async(aid, &UnderlayerSchedMgrActor::DoReserve, promise, selectedName, req);
            return;
        }
        promise->SetValue(future.Get());
    });
}

litebus::Future<Status> UnderlayerSchedMgrActor::UnReserve(const std::string &selectedName,
                                                           const std::shared_ptr<messages::ScheduleRequest> &req)
{
    auto promise = std::make_shared<litebus::Promise<Status>>();
    // requestBindMatch_ would not be called concurrency
    SendMethodWithRetry(promise, "UnReserve", &requestUnReserveMatch_, selectedName, req);
    return promise->GetFuture();
}

litebus::Future<Status> UnderlayerSchedMgrActor::Bind(const std::string &selectedName,
                                                      const std::shared_ptr<messages::ScheduleRequest> &req)
{
    auto promise = std::make_shared<litebus::Promise<Status>>();
    // requestBindMatch_ would not be called concurrency
    SendMethodWithRetry(promise, "Bind", &requestBindMatch_, selectedName, req);
    return promise->GetFuture();
}

void UnderlayerSchedMgrActor::SendMethodWithRetry(const std::shared_ptr<litebus::Promise<Status>> &promise,
                                                  const std::string &method,
                                                  RequestSyncHelper<UnderlayerSchedMgrActor, Status> *syncHelper,
                                                  const std::string &selectedName,
                                                  const std::shared_ptr<messages::ScheduleRequest> &req)
{
    if (underlayers_.find(selectedName) == underlayers_.end() || underlayers_[selectedName] == nullptr) {
        YRLOG_ERROR("{}|{}|failed to {} instance({}) of group {}. not found scheduler named {}.", req->traceid(),
                    req->requestid(), method, req->instance().instanceid(), req->instance().groupid(), selectedName);
        promise->SetValue(
            Status(StatusCode::DOMAIN_SCHEDULER_UNAVAILABLE_SCHEDULER,
                   "failed to " + method + ", because of local scheduler " + selectedName + " is abnormal"));
        return;
    }
    YRLOG_INFO("{}|{}|{} instance({}) of group({}) resource to {}.", req->traceid(), req->requestid(), method,
               req->instance().instanceid(), req->instance().groupid(), selectedName);
    const auto &aid = underlayers_[selectedName]->GetAID();
    litebus::AID localAid(req->instance().scheduleoption().target() == resources::CreateTarget::RESOURCE_GROUP
                              ? "BundleMgrActor"
                              : LOCAL_GROUP_CTRL_ACTOR_NAME,
                          aid.Url());
    auto future = syncHelper->AddSynchronizer(localAid.Url() + req->requestid());
    auto name = method;
    Send(localAid, std::move(name), req->SerializeAsString());
    future.OnComplete(
        [syncHelper, method, promise, selectedName, req, aid(GetAID())](const litebus::Future<Status> &future) {
            if (future.IsError()) {
                YRLOG_WARN("{}|{}|instance({}) of group({}) resource to {} timeout.", req->traceid(),
                           req->requestid(), req->instance().instanceid(), req->instance().groupid(), selectedName);
                litebus::Async(aid, &UnderlayerSchedMgrActor::SendMethodWithRetry, promise, method, syncHelper,
                               selectedName, req);
                return;
            }
            promise->SetValue(future.Get());
        });
}

litebus::Future<Status> UnderlayerSchedMgrActor::UnBind(const std::string &selectedName,
                                                        const std::shared_ptr<messages::ScheduleRequest> &req)
{
    auto promise = std::make_shared<litebus::Promise<Status>>();
    // requestUnBindMatch_ would not be called concurrency
    SendMethodWithRetry(promise, "UnBind", &requestUnBindMatch_, selectedName, req);
    return promise->GetFuture();
}

void UnderlayerSchedMgrActor::OnReserve(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto rsp = std::make_shared<messages::ScheduleResponse>();
    if (!rsp->ParseFromString(msg)) {
        YRLOG_WARN("invalid reserve response from {} msg {}, ignored", std::string(from), msg);
        return;
    }
    if (auto status = requestReserveMatch_.Synchronized(from.Url() + rsp->requestid(), rsp); status.IsError()) {
        YRLOG_WARN("{}|received reserve response. code {} msg {}. no found request ignore it. from {}",
                   rsp->requestid(), rsp->code(), rsp->message(), from.HashString());
        return;
    }
    ASSERT_IF_NULL(resourceViewMgr_);
    for (auto &[type, resource] : *rsp->mutable_updateresources()) {
        auto changes = std::make_shared<resource_view::ResourceUnitChanges>(std::move(resource));
        (void)resourceViewMgr_->GetInf(static_cast<resource_view::ResourceType>(type))
            ->UpdateResourceUnitDelta(changes);
    }
    YRLOG_INFO("{}|received reserve response. instance({}) code {} message {}. from {}", rsp->requestid(),
               rsp->instanceid(), rsp->code(), rsp->message(), from.HashString());
}

void UnderlayerSchedMgrActor::ReceiveGroupMethod(RequestSyncHelper<UnderlayerSchedMgrActor, Status> *syncHelper,
                                                 const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::GroupResponse rsp;
    if (!rsp.ParseFromString(msg)) {
        YRLOG_WARN("invalid {} response from {} msg {}, ignored", std::string(from), name, msg);
        return;
    }
    ASSERT_IF_NULL(resourceViewMgr_);
    for (auto &[type, resource] : *rsp.mutable_updateresources()) {
        auto changes = std::make_shared<resource_view::ResourceUnitChanges>(std::move(resource));
        (void)resourceViewMgr_->GetInf(static_cast<resource_view::ResourceType>(type))
            ->UpdateResourceUnitDelta(changes);
    }
    if (auto status = syncHelper->Synchronized(from.Url() + rsp.requestid(),
                                               Status(static_cast<StatusCode>(rsp.code()), rsp.message()));
        status.IsError()) {
        YRLOG_WARN("{}|{}|received {} from {}. code {} msg {}. no found request ignore it", rsp.traceid(),
                   rsp.requestid(), name, rsp.code(), rsp.message(), from.HashString());
        return;
    }
    YRLOG_INFO("{}|{}|received {} response. code {} message {}. from {}", rsp.traceid(), rsp.requestid(), name,
               rsp.code(), rsp.message(), from.HashString());
}

void UnderlayerSchedMgrActor::OnBind(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    ReceiveGroupMethod(&requestBindMatch_, from, std::move(name), std::move(msg));
}

void UnderlayerSchedMgrActor::OnUnReserve(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    ReceiveGroupMethod(&requestUnReserveMatch_, from, std::move(name), std::move(msg));
}

void UnderlayerSchedMgrActor::OnUnBind(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    ReceiveGroupMethod(&requestUnBindMatch_, from, std::move(name), std::move(msg));
}

void UnderlayerSchedMgrActor::Init()
{
    Receive("Register", &UnderlayerSchedMgrActor::Register);
    Receive("ForwardSchedule", &UnderlayerSchedMgrActor::ForwardSchedule);
    Receive("ResponseSchedule", &UnderlayerSchedMgrActor::ResponseSchedule);
    Receive("NotifySchedAbnormal", &UnderlayerSchedMgrActor::NotifySchedAbnormal);
    Receive("NotifyWorkerStatus", &UnderlayerSchedMgrActor::NotifyWorkerStatus);
    Receive("OnReserve", &UnderlayerSchedMgrActor::OnReserve);
    Receive("OnBind", &UnderlayerSchedMgrActor::OnBind);
    Receive("OnUnReserve", &UnderlayerSchedMgrActor::OnUnReserve);
    Receive("OnUnBind", &UnderlayerSchedMgrActor::OnUnBind);
    Receive("DeletePod", &UnderlayerSchedMgrActor::DeletePod);
    Receive("DeletePodResponse", &UnderlayerSchedMgrActor::DeletePodResponse);
    Receive("PreemptInstancesResponse", &UnderlayerSchedMgrActor::ResponsePreemptInstance);
}

}  // namespace functionsystem::domain_scheduler