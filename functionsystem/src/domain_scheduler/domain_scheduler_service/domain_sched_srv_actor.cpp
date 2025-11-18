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
#include "domain_sched_srv_actor.h"

#include <async/async.hpp>
#include <async/asyncafter.hpp>
#include <async/defer.hpp>

#include "common/constants/actor_name.h"
#include "common/constants/constants.h"
#include "common/constants/metastore_keys.h"
#include "common/explorer/explorer.h"
#include "common/logs/logging.h"
#include "common/metrics/metrics_adapter.h"
#include "common/resource_view/resource_type.h"

namespace functionsystem::domain_scheduler {
const uint32_t DEFAULT_REGISTER_INTERVAL = 5000;
const uint32_t DEFAULT_MAX_REGISTER_TIMES = 10;
const uint32_t DEFAULT_PING_RECEIVE_LOST_TIMEOUT = 6000;
const uint32_t RESOURCE_UPDATE_INTERVAL = 1000;
const uint32_t CLUSTER_METRICS_INTERVAL = 60 * 1000;  // ms
const uint32_t PUT_READY_RES_CYCLE_MS = 5000;         // ms
const int MAX_RETURN_SCHEDULING_QUEUE_SIZE = 10000;

using namespace functionsystem::explorer;

DomainSchedSrvActor::DomainSchedSrvActor(const std::string &name,
                                         const std::shared_ptr<MetaStoreClient> &metaStoreClient,
                                         uint32_t receivedPingTimeout, uint32_t maxRegisterTimes,
                                         uint32_t registerIntervalMs, uint32_t putReadyResCycleMs)
    : ActorBase(name + DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX),
      domainName_(name),
      metaStoreClient_(metaStoreClient),
      maxRegisterTimes_(maxRegisterTimes ? maxRegisterTimes : DEFAULT_MAX_REGISTER_TIMES),
      registerIntervalMs_(registerIntervalMs ? registerIntervalMs : DEFAULT_REGISTER_INTERVAL),
      putReadyResCycleMs_(putReadyResCycleMs ? putReadyResCycleMs : PUT_READY_RES_CYCLE_MS),
      receivedPingTimeout_(receivedPingTimeout ? receivedPingTimeout : DEFAULT_PING_RECEIVE_LOST_TIMEOUT)
{
}

void DomainSchedSrvActor::StartPingPong(const std::string &address)
{
    if (pingpong_ == nullptr) {
        pingpong_ = std::make_unique<HeartbeatClientDriver>(
            litebus::os::Join(componentName_, domainName_, '-'),
            // while connection lost, try to register
            [aid(GetAID()), domainName = domainName_](const litebus::AID &lostDst) {
                YRLOG_WARN("pingpong {} timeout, from domain to master. dst: {}", domainName, std::string(lostDst));
                litebus::Async(aid, &DomainSchedSrvActor::PingPongLost, lostDst);
            });
    }
    litebus::AID heartBeatAID(HEARTBEAT_OBSERVER_BASENAME + DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME, address);
    YRLOG_INFO("start domain {} ping pong actor. address: {}", domainName_, std::string(heartBeatAID));

    (void)pingpong_->Start(heartBeatAID);
}

void DomainSchedSrvActor::PingPongLost(const litebus::AID &lostDst)
{
    // while connection lost, try to register
    if (lostDst == uplayer_.aid) {
        RegisterToLeader();
        return;
    }
    if (lostDst == global_.aid) {
        (void)RegisterToGlobal().OnComplete([](const litebus::Future<Status> &status) {
            if (status.IsOK()) {
                return;
            }
            YRLOG_ERROR("reRegister to global failed! exit.");
            BUS_EXIT(static_cast<int32_t>(StatusCode::FAILED));
        });
        return;
    }
}

litebus::Future<Status> DomainSchedSrvActor::RegisterToGlobal()
{
    YRLOG_DEBUG("begin register to global");
    global_.registered = litebus::Promise<Status>();
    global_.aid = masterAid_;
    RegisterTrigger(global_);
    return global_.registered.GetFuture();
}

void DomainSchedSrvActor::RegisterToLeader()
{
    RegisterTrigger(uplayer_);
    (void)uplayer_.registered.GetFuture().OnComplete([aid(GetAID())](const litebus::Future<Status> &result) {
        if (result.IsError()) {
            YRLOG_ERROR("register to up domain failed! code {}. try to reregister", result.GetErrorCode());
            litebus::Async(aid, &DomainSchedSrvActor::RegisterToLeader);
            return;
        }
        YRLOG_INFO("register to UpDomain succeed.");
        (void)litebus::Async(aid, &DomainSchedSrvActor::UpdateResourceToUpLayer);
    });
}

void DomainSchedSrvActor::RegisterTrigger(RegisterUp &registry)
{
    YRLOG_INFO("register domain {} to {}", domainName_, std::string(registry.aid));
    auto req = std::make_shared<messages::Register>();
    req->set_name(domainName_);
    req->set_address(GetAID().UnfixUrl());

    ASSERT_IF_NULL(resourceViewMgr_);
    resourceViewMgr_->GetResources().Then(
        litebus::Defer(GetAID(), &DomainSchedSrvActor::SendRegisterWithRes, registry.aid, req, std::placeholders::_1));

    registry.reRegisterTimer = litebus::AsyncAfter(litebus::Duration(registerIntervalMs_), GetAID(),
                                                   &DomainSchedSrvActor::RegisterTimeout, registry.aid);
}

Status DomainSchedSrvActor::SendRegisterWithRes(
    const litebus::AID aid, const std::shared_ptr<messages::Register> &req,
    const std::unordered_map<ResourceType, std::shared_ptr<ResourceUnit>> &resources)
{
    ASSERT_IF_NULL(req);
    for (auto &[type, resource] : resources) {
        ASSERT_IF_NULL(resource);
        (*req->mutable_resources())[static_cast<int32_t>(type)] = std::move(*resource);
    }
    (void)Send(aid, "Register", req->SerializeAsString());
    return Status::OK();
}

void DomainSchedSrvActor::RegisterTimeout(const litebus::AID &aid)
{
    RegisterUp &registry = global_;
    if (aid == global_.aid) {
        global_.aid = masterAid_;
        registry = global_;
    } else if (aid == uplayer_.aid) {
        registry = uplayer_;
    } else {
        YRLOG_WARN("invalid actor {}", std::string(aid));
        return;
    }
    registry.timeouts++;
    if (registry.timeouts > maxRegisterTimes_) {
        YRLOG_ERROR("Register to {} failed. tried {} times in {} ms", std::string(aid), maxRegisterTimes_,
                    maxRegisterTimes_ * registerIntervalMs_);
        registry.registered.SetFailed(static_cast<int32_t>(StatusCode::PARAMETER_ERROR));
        (void)litebus::TimerTools::Cancel(registry.reRegisterTimer);
        return;
    }
    RegisterTrigger(registry);
}

void DomainSchedSrvActor::Registered(const messages::Registered &message, RegisterUp &registry)
{
    if (registry.registered.GetFuture().IsOK()) {
        YRLOG_INFO("{} registered has been set", std::string(registry.aid));
        return;
    }
    (void)litebus::TimerTools::Cancel(registry.reRegisterTimer);
    if (message.code() != 0) {
        YRLOG_INFO("{} registered message code: {}", std::string(registry.aid), message.code());
        registry.registered.SetFailed(message.code());
        return;
    }

    YRLOG_INFO("{} registered successfully", std::string(registry.aid));
    registry.registered.SetValue(Status::OK());
}

void DomainSchedSrvActor::UpdateLeader(const std::string &name, const std::string &address)
{
    if (uplayer_.aid.Name() == name) {
        return;
    }
    uplayer_.aid.SetName(name + DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX);
    uplayer_.aid.SetUrl(address);
    uplayer_.aid.SetProtocol(litebus::BUS_TCP);
    (void)litebus::TimerTools::Cancel(uplayer_.reRegisterTimer);
    uplayer_.registered = litebus::Promise<Status>();
    uplayer_.timeouts = 0;
    RegisterToLeader();
}

void DomainSchedSrvActor::PutReadyResCycle()
{
    if (putReadyResTimer_ != nullptr) {
        return;
    }
    YRLOG_INFO("begin put ready res");
    putReadyResTimer_ = std::make_shared<litebus::Timer>(
        litebus::AsyncAfter(litebus::Duration(putReadyResCycleMs_), GetAID(), &DomainSchedSrvActor::PutReadyRes));
}

static std::string GetVecPrintInfo(const std::unordered_set<std::string> &set)
{
    std::string info;
    for (const auto &e : set) {
        info += e + " ";
    }
    return info;
}

void DomainSchedSrvActor::PutReadyRes()
{
    static auto prevReadyAgentCnt = std::make_shared<uint32_t>();
    static auto prevReadyAgent = std::make_shared<std::unordered_set<std::string>>();
    auto curReadyAgent = std::make_shared<std::unordered_set<std::string>>();
    ASSERT_IF_NULL(resourceViewMgr_);
    (void)resourceViewMgr_->GetInf(ResourceType::PRIMARY)->GetFullResourceView()
        .Then(litebus::Defer(GetAID(), &DomainSchedSrvActor::CountReadyRes, std::placeholders::_1, curReadyAgent))
        .Then(litebus::Defer(GetAID(), &DomainSchedSrvActor::DoPutReadyRes, std::placeholders::_1, prevReadyAgentCnt,
                             curReadyAgent, prevReadyAgent));
    putReadyResTimer_ = std::make_shared<litebus::Timer>(
        litebus::AsyncAfter(litebus::Duration(putReadyResCycleMs_), GetAID(), &DomainSchedSrvActor::PutReadyRes));
}

Status DomainSchedSrvActor::DoPutReadyRes(uint32_t readyResCnt, const std::shared_ptr<uint32_t> &prevrescnt,
                                          const std::shared_ptr<std::unordered_set<std::string>> &curReadyAgent,
                                          const std::shared_ptr<std::unordered_set<std::string>> &prevReadyAgent)
{
    if (readyResCnt != *prevrescnt) {
        YRLOG_INFO("ready agent changed from {} to {}", *prevrescnt, readyResCnt);

        std::unordered_set<std::string> increaseAgent;
        std::unordered_set<std::string> decreateAgent;
        for (const auto &agent : *prevReadyAgent) {
            if (curReadyAgent->count(agent) == 0) {
                decreateAgent.emplace(agent);
            }
        }
        for (const auto &agent : *curReadyAgent) {
            if (prevReadyAgent->count(agent) == 0) {
                increaseAgent.emplace(agent);
            }
        }
        YRLOG_INFO("agent num increase, info: {}", GetVecPrintInfo(increaseAgent));
        YRLOG_INFO("agent num decrease, info: {}", GetVecPrintInfo(decreateAgent));

        *prevrescnt = readyResCnt;
        *prevReadyAgent = *curReadyAgent;
        RETURN_STATUS_IF_NULL(metaStoreClient_, StatusCode::FAILED, "meta client is nullptr");
        (void)metaStoreClient_->Put(READY_AGENT_CNT_KEY, std::to_string(readyResCnt), {});
    }
    return Status::OK();
}

bool IsValidCpuResource(const resource_view::ResourceUnit &unit)
{
    auto &capacity = unit.capacity();
    if (auto iter(capacity.resources().find(resource_view::CPU_RESOURCE_NAME)); iter != capacity.resources().end()) {
        return (iter->second.scalar().value() > 1);
    }
    return false;
}

uint32_t DomainSchedSrvActor::CountReadyRes(const std::shared_ptr<resource_view::ResourceUnit> &unit,
                                            const std::shared_ptr<std::unordered_set<std::string>> &curReadyAgent)
{
    return DoCountReadyRes(*unit, curReadyAgent);
}

uint32_t DomainSchedSrvActor::DoCountReadyRes(const resource_view::ResourceUnit &unit,
                                              const std::shared_ptr<std::unordered_set<std::string>> &curReadyAgent)
{
    uint32_t readyResCnt = 0;

    const auto fragment = unit.fragment();
    for (const auto &childNode : fragment) {
        if (childNode.first.find(FUNCTION_AGENT_ID_PREFIX) != childNode.first.npos) {
            if (childNode.second.status() == static_cast<uint32_t>(UnitStatus::TO_BE_DELETED)) {
                continue;
            }
            if (IsValidCpuResource(childNode.second)) {
                (void)curReadyAgent->emplace(childNode.first);
                ++readyResCnt;
            }
            continue;
        }

        if (childNode.second.fragment_size() != 0) {
            readyResCnt += DoCountReadyRes(childNode.second, curReadyAgent);
        }
    }
    return readyResCnt;
}

void DomainSchedSrvActor::Registered(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    YRLOG_INFO("received registered from {}", std::string(from));
    messages::Registered message;
    if (!message.ParseFromString(msg)) {
        YRLOG_WARN("received registered from {}, invalid msg {}", std::string(from), msg);
        message.set_code(static_cast<int32_t>(StatusCode::PARAMETER_ERROR));
    }
    if (from.Name() == global_.aid.Name()) {
        ASSERT_IF_NULL(instanceCtrl_);
        ASSERT_IF_NULL(underlayer_);
        ASSERT_IF_NULL(resourceViewMgr_);
        Registered(message, global_);
        if (message.topo().has_leader()) {
            auto leader = message.topo().leader();
            UpdateLeader(leader.name(), leader.address());
        } else {
            PutReadyResCycle();
            isHeader_ = true;
            instanceCtrl_->SetDomainLevel(true);
            underlayer_->SetDomainLevel(isHeader_);
            resourceViewMgr_->GetInf()->UpdateIsHeader(isHeader_);
            metrics::MetricsAdapter::GetInstance().RegisterPodResource();
        }
        underlayer_->UpdateUnderlayerTopo(message.topo());
        return;
    }

    if (from.Name() == uplayer_.aid.Name()) {
        Registered(message, uplayer_);
        return;
    }
}

void DomainSchedSrvActor::UpdateSchedTopoView(const litebus::AID &from, std::string && /* name */, std::string &&msg)
{
    messages::ScheduleTopology topo;
    if (!topo.ParseFromString(msg)) {
        YRLOG_ERROR("failed to update topo, invalid topo msg.");
        return;
    }
    ASSERT_IF_NULL(resourceViewMgr_);
    ASSERT_IF_NULL(instanceCtrl_);
    ASSERT_IF_NULL(underlayer_);
    YRLOG_INFO("received Topo updated from {} msg {}", std::string(from), topo.ShortDebugString());
    if (topo.has_leader()) {
        UpdateLeader(topo.leader().name(), topo.leader().address());
        if (putReadyResTimer_ != nullptr) {
            (void)litebus::TimerTools::Cancel(*putReadyResTimer_);
        }
        isHeader_ = false;
        instanceCtrl_->SetDomainLevel(false);
        underlayer_->SetDomainLevel(isHeader_);
        resourceViewMgr_->GetInf()->UpdateIsHeader(isHeader_);
        metrics::MetricsAdapter::GetInstance().GetMetricsContext().ErasePodResource();
        StartPingPong(topo.leader().address());
    } else {
        PutReadyResCycle();
        isHeader_ = true;
        instanceCtrl_->SetDomainLevel(true);
        underlayer_->SetDomainLevel(isHeader_);
        resourceViewMgr_->GetInf()->UpdateIsHeader(isHeader_);
        metrics::MetricsAdapter::GetInstance().RegisterPodResource();
        StartPingPong(masterAid_.Url());
    }
    underlayer_->UpdateUnderlayerTopo(topo);
}

void DomainSchedSrvActor::UpdateResourceToUpLayer()
{
    if (!uplayer_.registered.GetFuture().IsOK()) {
        YRLOG_DEBUG("not registered with {}, will stop reporting resources", std::string(uplayer_.aid));
        return;
    }
    ASSERT_IF_NULL(resourceViewMgr_);
    (void)resourceViewMgr_->GetInf(ResourceType::PRIMARY)->GetSerializedResourceView()
        .OnComplete(litebus::Defer(GetAID(), &DomainSchedSrvActor::UpdateResourceToSubscriber, uplayer_.aid,
                                   std::placeholders::_1))
        .OnComplete([aid(GetAID())](const litebus::Future<std::string> &) {
            (void)litebus::AsyncAfter(RESOURCE_UPDATE_INTERVAL, aid, &DomainSchedSrvActor::UpdateResourceToUpLayer);
        });
}

void DomainSchedSrvActor::PullResources(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    YRLOG_DEBUG("{} Pull Resources", std::string(from));
    ASSERT_IF_NULL(resourceViewMgr_);
    (void)resourceViewMgr_->GetInf(ResourceType::PRIMARY)->GetSerializedResourceView().OnComplete(
        litebus::Defer(GetAID(), &DomainSchedSrvActor::UpdateResourceToSubscriber, from, std::placeholders::_1));
}

void DomainSchedSrvActor::UpdateResourceToSubscriber(const litebus::AID &to, const litebus::Future<std::string> &view)
{
    if (view.IsError()) {
        YRLOG_ERROR("Get resource of domain from resource pool err! code ({})", view.GetErrorCode());
        return;
    }
    auto msg = view.Get();
    (void)Send(to, "UpdateResources", std::move(msg));
}

void DomainSchedSrvActor::ResponseForwardSchedule(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto rsp = std::make_shared<messages::ScheduleResponse>();
    if (!rsp->ParseFromString(msg)) {
        YRLOG_ERROR("invalid schedule response from({}), {}", std::string(from), msg);
        return;
    }
    YRLOG_DEBUG("{}|receviced schedule response from({}), {}", rsp->requestid(), std::string(from), msg);
    scheduleSync_.Synchronized(rsp->requestid(), rsp);
}

void DomainSchedSrvActor::Schedule(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto req = std::make_shared<messages::ScheduleRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("invalid schedule request {}. ignored", msg);
        return;
    }

    if (recivedSchedulingReq_.find(req->requestid()) != recivedSchedulingReq_.end()) {
        YRLOG_WARN("{}|{}|received repeated schedule request from {}, ignore it", req->traceid(), req->requestid(),
                   std::string(from));
        return;
    }

    YRLOG_INFO("{}|{}|received schedule request from {}", req->traceid(), req->requestid(), std::string(from));
    (void)recivedSchedulingReq_.insert(req->requestid());
    ASSERT_IF_NULL(instanceCtrl_);
    instanceCtrl_->Schedule(req)
        .Then(litebus::Defer(GetAID(), &DomainSchedSrvActor::CollectCurrentResource, std::placeholders::_1))
        .OnComplete(litebus::Defer(GetAID(), &DomainSchedSrvActor::ScheduleCallback, from, std::placeholders::_1, req));
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> DomainSchedSrvActor::CollectCurrentResource(
    const litebus::Future<std::shared_ptr<messages::ScheduleResponse>> &response)
{
    const auto &resp = response.Get();
    ASSERT_IF_NULL(resp);
    ASSERT_IF_NULL(resourceViewMgr_);
    return resourceViewMgr_->GetChanges().Then(
        [resp](const std::unordered_map<ResourceType, std::shared_ptr<ResourceUnitChanges>> &changes)
            -> litebus::Future<std::shared_ptr<messages::ScheduleResponse>> {
            for (const auto &[type, change] : changes) {
                (*resp->mutable_updateresources())[static_cast<int32_t>(type)] = std::move(*change);
            }
            return resp;
        });
}

void DomainSchedSrvActor::ScheduleCallback(const litebus::AID &to,
                                           const litebus::Future<std::shared_ptr<messages::ScheduleResponse>> &response,
                                           const std::shared_ptr<messages::ScheduleRequest> &req)
{
    YRLOG_INFO("(schedule)send schedule response from {} to {}", std::string(GetAID()), std::string(to));
    (void)recivedSchedulingReq_.erase(req->requestid());
    const auto &resp = response.Get();
    ASSERT_IF_NULL(resp);
    (void)Send(to, "ResponseSchedule", resp->SerializeAsString());
}

litebus::Future<Status> DomainSchedSrvActor::NotifySchedAbnormal(const messages::NotifySchedAbnormalRequest &req)
{
    YRLOG_WARN("notify scheduler abnormal {}", req.schedname());
    // When an upper-layer domain exists, the abnormal is reported to the corresponding upper-layer domain.
    // Otherwise, the abnormal is reported to the global scheduler.
    if (uplayer_.registered.GetFuture().IsOK()) {
        auto future = notifyAbnormalSync_.AddSynchronizer(req.schedname());
        (void)Send(uplayer_.aid, "NotifySchedAbnormal", req.SerializeAsString());
        return future;
    }
    if (global_.registered.GetFuture().IsOK()) {
        auto future = notifyAbnormalSync_.AddSynchronizer(req.schedname());
        (void)Send(global_.aid, "NotifySchedAbnormal", req.SerializeAsString());
        return future;
    }
    std::string err = "no global or domain can receive report";
    YRLOG_ERROR(err);
    return Status(StatusCode::DOMAIN_SCHEDULER_REGISTER_ERR, err);
}

litebus::Future<Status> DomainSchedSrvActor::NotifyWorkerStatus(const messages::NotifyWorkerStatusRequest &req)
{
    YRLOG_INFO("report worker({}) healthy({})", req.workerip(), req.healthy());
    if (uplayer_.registered.GetFuture().IsOK()) {
        auto future = notifyWorkerStatusSync_.AddSynchronizer(req.workerip() + "_" + std::to_string(req.healthy()));
        (void)Send(uplayer_.aid, "NotifyWorkerStatus", req.SerializeAsString());
        return future;
    }
    if (global_.registered.GetFuture().IsOK()) {
        auto future = notifyWorkerStatusSync_.AddSynchronizer(req.workerip() + "_" + std::to_string(req.healthy()));
        (void)Send(global_.aid, "NotifyWorkerStatus", req.SerializeAsString());
        return future;
    }
    std::string err = "no global or domain can receive report";
    YRLOG_ERROR(err);
    return Status(StatusCode::DOMAIN_SCHEDULER_REGISTER_ERR, err);
}

void DomainSchedSrvActor::ResponseNotifySchedAbnormal(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    YRLOG_INFO("received Notify abnormal response from {}, {}", std::string(from), msg);
    messages::NotifySchedAbnormalResponse rsp;
    if (!rsp.ParseFromString(msg)) {
        YRLOG_ERROR("invalid Notify response {}. ignored", msg);
        return;
    }
    notifyAbnormalSync_.Synchronized(rsp.schedname(), Status::OK());
}

void DomainSchedSrvActor::ResponseNotifyWorkerStatus(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::NotifyWorkerStatusResponse rsp;
    if (!rsp.ParseFromString(msg)) {
        YRLOG_ERROR("invalid Notify response {}. ignored", msg);
        return;
    }
    YRLOG_INFO("received NotifyWorkerStatus response from({}) node({}) healthy({})", std::string(from), rsp.workerip(),
               rsp.healthy());
    notifyWorkerStatusSync_.Synchronized(rsp.workerip() + "_" + std::to_string(rsp.healthy()), Status::OK());
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> DomainSchedSrvActor::ForwardSchedule(
    const std::shared_ptr<messages::ScheduleRequest> &req)
{
    // Forwarding can be performed only when the upper-layer domain scheduler exists.
    if (!uplayer_.registered.GetFuture().IsOK()) {
        YRLOG_WARN("{}|{}|no valid uplayer to forward schedule", req->traceid(), req->requestid());
        auto response = std::make_shared<messages::ScheduleResponse>();
        response->set_code(static_cast<int32_t>(StatusCode::DOMAIN_SCHEDULER_FORWARD_ERR));
        response->set_message("no available uplayer to forward schedule");
        response->set_requestid(req->requestid());
        return response;
    }
    YRLOG_DEBUG("{}|{}|forward schedule to uplayer scheduler({})", req->traceid(), req->requestid(),
                std::string(uplayer_.aid));
    auto future = scheduleSync_.AddSynchronizer(req->requestid());
    (void)Send(uplayer_.aid, "ForwardSchedule", req->SerializeAsString());
    return future;
}

void DomainSchedSrvActor::Finalize()
{
    (void)litebus::TimerTools::Cancel(uplayer_.reRegisterTimer);
    (void)litebus::TimerTools::Cancel(global_.reRegisterTimer);
    StopCollectClusterResourceState();
    if (putReadyResTimer_ != nullptr) {
        (void)litebus::TimerTools::Cancel(*putReadyResTimer_);
    }
}

void DomainSchedSrvActor::Init()
{
    (void)Explorer::GetInstance().AddLeaderChangedCallback(
        "DomainSchedSrv", [aid(GetAID())](const LeaderInfo &leaderInfo) {
            litebus::Async(aid, &DomainSchedSrvActor::UpdateMasterInfo, leaderInfo);
        });

    Receive("UpdateSchedTopoView", &DomainSchedSrvActor::UpdateSchedTopoView);
    Receive("Registered", &DomainSchedSrvActor::Registered);
    Receive("PullResources", &DomainSchedSrvActor::PullResources);
    Receive("ResponseForwardSchedule", &DomainSchedSrvActor::ResponseForwardSchedule);
    Receive("Schedule", &DomainSchedSrvActor::Schedule);
    Receive("ResponseNotifySchedAbnormal", &DomainSchedSrvActor::ResponseNotifySchedAbnormal);
    Receive("ResponseNotifyWorkerStatus", &DomainSchedSrvActor::ResponseNotifyWorkerStatus);
    Receive("QueryAgentInfo", &DomainSchedSrvActor::QueryAgentInfo);
    Receive("QueryResourcesInfo", &DomainSchedSrvActor::QueryResourcesInfo);
    Receive("TryCancelSchedule", &DomainSchedSrvActor::TryCancelSchedule);
    Receive("GetSchedulingQueue", &DomainSchedSrvActor::GetSchedulingQueue);
}
void DomainSchedSrvActor::UpdateMasterInfo(const explorer::LeaderInfo &leaderInfo)
{
    masterAid_ = litebus::AID(DOMAIN_SCHED_MGR_ACTOR_NAME, leaderInfo.address);
    masterAid_.SetProtocol(litebus::BUS_TCP);

    if (masterAid_.Url() != global_.aid.Url()) {
        YRLOG_INFO("new global AID: {}, and prev global AID: {}", std::string(masterAid_), std::string(global_.aid));
        (void)litebus::TimerTools::Cancel(global_.reRegisterTimer);
        (void)litebus::TimerTools::Cancel(uplayer_.reRegisterTimer);
        ASSERT_IF_NULL(instanceCtrl_);
        global_.aid = masterAid_;
        instanceCtrl_->SetScalerAddress(leaderInfo.address);
        ASSERT_IF_NULL(underlayer_);
        underlayer_->SetScalerAddress(leaderInfo.address);
        (void)RegisterToGlobal();
    }
}

litebus::Future<Status> DomainSchedSrvActor::EnableMetrics(const bool enableMetrics)
{
    enableMetrics_ = enableMetrics;
    if (enableMetrics_) {
        StartCollectClusterResourceState();
    }
    return Status::OK();
}

void DomainSchedSrvActor::StartCollectClusterResourceState()
{
    YRLOG_DEBUG("start collect cluster resource state");
    if (!isHeader_) {
        StopCollectClusterResourceState();
    }
    CollectClusterResourceState();
    metricExportTimer_ =
        litebus::AsyncAfter(CLUSTER_METRICS_INTERVAL, GetAID(), &DomainSchedSrvActor::StartCollectClusterResourceState);
}

void DomainSchedSrvActor::StopCollectClusterResourceState()
{
    (void)litebus::TimerTools::Cancel(metricExportTimer_);
}

void DomainSchedSrvActor::CollectClusterResourceState()
{
    if (!isHeader_) {
        return;
    }
    ASSERT_IF_NULL(resourceViewMgr_);
    // should consider rg resource in the future
    (void)resourceViewMgr_->GetInf(ResourceType::PRIMARY)->GetResourceViewCopy().OnComplete(
        [](const litebus::Future<std::shared_ptr<resource_view::ResourceUnit>> &future) {
            if (future.IsOK()) {
                functionsystem::metrics::MetricsAdapter::GetInstance().ReportClusterSourceState(future.Get());
            }
        });
}

void DomainSchedSrvActor::ExtractAgentInfo(const resource_view::ResourceUnit &unit,
                                           google::protobuf::RepeatedPtrField<resources::AgentInfo> &agentInfos)
{
    const auto &fragment = unit.fragment();
    for (const auto &[id, frag] : fragment) {
        if (id.find(FUNCTION_AGENT_ID_PREFIX) != std::string::npos) {
            if (IsValidCpuResource(frag)) {
                resources::AgentInfo info;
                info.set_localid(frag.ownerid());
                info.set_agentid(id);
                info.set_alias(frag.alias());
                agentInfos.Add(std::move(info));
            }
            continue;
        }
        if (frag.fragment_size() != 0) {
            ExtractAgentInfo(frag, agentInfos);
        }
    }
}

void DomainSchedSrvActor::QueryAgentInfoCallBack(
    const litebus::AID &to, const std::string &requestID,
    const litebus::Future<std::shared_ptr<resource_view::ResourceUnit>> &future)
{
    messages::QueryAgentInfoResponse rsp;
    rsp.set_requestid(requestID);
    ASSERT_FS(future.IsOK());
    auto resource = future.Get();
    ASSERT_IF_NULL(resource);
    ExtractAgentInfo(*resource, *rsp.mutable_agentinfos());
    YRLOG_DEBUG("{}|send response query agent info request from({}), {}", requestID, std::string(to),
                rsp.agentinfos_size());
    Send(to, "ResponseQueryAgentInfo", rsp.SerializeAsString());
}

void DomainSchedSrvActor::QueryAgentInfo(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto req = std::make_shared<messages::QueryAgentInfoRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("invalid query agent info request from({}), {}", std::string(from), msg);
        return;
    }
    YRLOG_DEBUG("{}|received query agent info request from({}), {}", req->requestid(), std::string(from), msg);
    ASSERT_IF_NULL(resourceViewMgr_);
    resourceViewMgr_->GetInf(ResourceType::PRIMARY)->GetFullResourceView().OnComplete(
        litebus::Defer(GetAID(), &DomainSchedSrvActor::QueryAgentInfoCallBack, from, req->requestid(),
                       std::placeholders::_1));
}

void DomainSchedSrvActor::GetSchedulingQueue(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto req = std::make_shared<messages::QueryInstancesInfoRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("invalid get scheduling queue request from({}), {}", std::string(from), msg);
        return;
    }
    YRLOG_DEBUG("{}|received get scheduling queue request from({}), {}", req->requestid(), std::string(from), msg);

    ASSERT_IF_NULL(instanceCtrl_);
    ASSERT_IF_NULL(groupCtrl_);

    instanceCtrl_->GetSchedulerQueue()
        .Then(litebus::Defer(GetAID(), &DomainSchedSrvActor::CombineInstanceAndGroup, std::placeholders::_1))
        .OnComplete(litebus::Defer(GetAID(), &DomainSchedSrvActor::GetSchedulingQueueCallBack, from, req->requestid(),
                                   std::placeholders::_1));
}

litebus::Future<std::vector<std::shared_ptr<messages::ScheduleRequest>>> DomainSchedSrvActor::CombineInstanceAndGroup(
    const std::vector<std::shared_ptr<messages::ScheduleRequest>> &instanceQueue)
{
    return groupCtrl_->GetRequests().Then(
        [instanceQueue](std::vector<std::shared_ptr<messages::ScheduleRequest>> requests) {
            requests.insert(requests.end(), instanceQueue.begin(), instanceQueue.end());
            return requests;
        });
}

void DomainSchedSrvActor::GetSchedulingQueueCallBack(
    const litebus::AID &to, const std::string &requestID,
    const litebus::Future<std::vector<std::shared_ptr<messages::ScheduleRequest>>> &future)
{
    messages::QueryInstancesInfoResponse rsp;
    rsp.set_requestid(requestID);
    ASSERT_FS(future.IsOK());
    auto scheduleRequests = future.Get();

    int size = 0;
    for (std::shared_ptr<messages::ScheduleRequest> scheduleRequest : scheduleRequests) {
        ASSERT_IF_NULL(scheduleRequest);

        if (size >= MAX_RETURN_SCHEDULING_QUEUE_SIZE) {
            break;
        }

        *rsp.add_instanceinfos() = std::move((*scheduleRequest).instance());

        size++;
    }

    YRLOG_DEBUG("{}|send response get scheduling queue request from({}), instanceinfos size is {}", requestID,
                std::string(to), rsp.instanceinfos_size());

    Send(to, "ResponseGetSchedulingQueue", rsp.SerializeAsString());
}

void DomainSchedSrvActor::QueryResourcesInfo(const litebus::AID &from, std::string &&, std::string &&msg)
{
    auto req = std::make_shared<messages::QueryResourcesInfoRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("invalid query resource info request from({}), {}", std::string(from), msg);
        return;
    }
    YRLOG_DEBUG("{}|received query resource info request from({})", req->requestid(), std::string(from));
    ASSERT_IF_NULL(resourceViewMgr_);
    resourceViewMgr_->GetInf(ResourceType::PRIMARY)->GetResourceViewCopy().OnComplete(litebus::Defer(
        GetAID(), &DomainSchedSrvActor::QueryResourcesInfoCallBack, from, req->requestid(), std::placeholders::_1));
}

void DomainSchedSrvActor::QueryResourcesInfoCallBack(
    const litebus::AID &to, const std::string &requestID,
    const litebus::Future<std::shared_ptr<resource_view::ResourceUnit>> &future)
{
    messages::QueryResourcesInfoResponse rsp;
    rsp.set_requestid(requestID);
    ASSERT_FS(future.IsOK());
    auto resource = future.Get();
    ASSERT_IF_NULL(resource);
    resource->clear_instances();
    resource->clear_bucketindexs();
    (*rsp.mutable_resource()) = std::move(*resource);
    std::set<std::string> toFiltered;
    for (auto fragment : rsp.mutable_resource()->fragment()) {
        if (fragment.second.status() == static_cast<uint32_t>(UnitStatus::TO_BE_DELETED)) {
            toFiltered.insert(fragment.first);
        }
    }
    for (auto invalid : toFiltered) {
        rsp.mutable_resource()->mutable_fragment()->erase(invalid);
    }
    YRLOG_DEBUG("{}|send response query resource info request to({})", requestID, std::string(to));
    Send(to, "ResponseQueryResourcesInfo", rsp.SerializeAsString());
}

void DomainSchedSrvActor::TryCancelSchedule(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto cancelRequest = std::make_shared<messages::CancelSchedule>();
    if (!cancelRequest->ParseFromString(msg)) {
        YRLOG_WARN("received cancel schedule from {}, invalid msg {} ignore", std::string(from), msg);
        return;
    }
    YRLOG_INFO("received cancel schedule from {},  cancel({}) type({}) reason({}) msgid({})", std::string(from),
               cancelRequest->id(), fmt::underlying(cancelRequest->type()), cancelRequest->reason(),
               cancelRequest->msgid());
    if (cancelRequest->type() == messages::CancelType::REQUEST) {
        ASSERT_IF_NULL(instanceCtrl_);
        instanceCtrl_->TryCancelSchedule(cancelRequest);
    } else {
        ASSERT_IF_NULL(groupCtrl_);
        groupCtrl_->TryCancelSchedule(cancelRequest);
    }
    // instanceCtrl should also support cancel
    auto cancelRsp = messages::CancelScheduleResponse();
    cancelRsp.set_msgid(cancelRequest->msgid());
    Send(from, "TryCancelResponse", cancelRsp.SerializeAsString());
}

void DomainSchedSrvActor::SetComponentName(const std::string &componentName)
{
    componentName_ = componentName;
}

}  // namespace functionsystem::domain_scheduler
