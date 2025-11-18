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

#include "local_sched_srv_actor.h"

#include <async/asyncafter.hpp>
#include <async/defer.hpp>

#include "common/constants/actor_name.h"
#include "common/explorer/explorer.h"
#include "common/logs/logging.h"
#include "common/resource_view/resource_tool.h"
#include "common/utils/generate_message.h"
#include "common/utils/time_trigger.h"
#include "utils/os_utils.hpp"

namespace functionsystem::local_scheduler {
const int64_t RESERVED = -300;
const int64_t UNREGISTER_TIMEOUT = 5000;
using namespace functionsystem::explorer;

LocalSchedSrvActor::LocalSchedSrvActor(const LocalSchedSrvActor::Param &param)
    : BasisActor(LOCAL_SCHED_SRV_ACTOR_NAME),
      nodeID_(param.nodeID),
      isK8sEnabled_(param.isK8sEnabled),
      registerCycleMs_(param.registerCycleMs),
      pingTimeOutMs_(param.pingTimeOutMs),
      updateResourceCycleMs_(param.updateResourceCycleMs),
      forwardRequestTimeOutMs_(param.forwardRequestTimeOutMs),
      groupTimeout_(param.groupScheduleTimeout),
      groupKillTimeout_(param.groupKillTimeout),
      componentName_(param.componentName)
{
}

void LocalSchedSrvActor::BindPingPongDriver(const std::shared_ptr<HeartbeatClientDriver> &pingPongDriver)
{
    pingPongDriver_ = pingPongDriver;
}

LocalSchedSrvActor::~LocalSchedSrvActor()
{
}

void LocalSchedSrvActor::BindInstanceCtrl(const std::shared_ptr<InstanceCtrl> &instanceCtrl)
{
    instanceCtrl_ = instanceCtrl;
}

void LocalSchedSrvActor::BindResourceView(const std::shared_ptr<resource_view::ResourceViewMgr> &resourceViewMgr)
{
    resourceViewMgr_ = resourceViewMgr;
}

void LocalSchedSrvActor::Schedule(const litebus::AID &from, std::string &&, std::string &&msg)
{
    if (!enableService_) {
        YRLOG_ERROR("local scheduler({}) service is not enabled, ignore schedule request from {}", nodeID_,
                    std::string(from));
        return;
    }
    auto req = std::make_shared<messages::ScheduleRequest>();
    if (!req->ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse message from string, from: {}, msg: {}", std::string(from), msg);
        auto rsp = GenScheduleResponse(static_cast<int32_t>(StatusCode::PARAMETER_ERROR),
                                       "failed to parse message from string", "", "");
        Send(from, "ResponseSchedule", std::move(rsp.SerializeAsString()));
        return;
    }
    if (from != domainSchedRegisterInfo_.aid) {
        YRLOG_WARN("schedule request ({}) from unexpected domain({}), expect ({}). ignore it.",
                   req->requestid(), std::string(from), std::string(domainSchedRegisterInfo_.aid));
        return;
    }
    if (req->requestid().empty()) {
        YRLOG_WARN("invalid param requestID is empty from: {}", std::string(from));
        auto rsp = GenScheduleResponse(static_cast<int32_t>(StatusCode::PARAMETER_ERROR),
                                       "requestID is empty", req->traceid(), "");
        Send(from, "ResponseSchedule", std::move(rsp.SerializeAsString()));
        return;
    }

    auto instanceCtrl = instanceCtrl_.lock();
    if (instanceCtrl == nullptr) {
        YRLOG_WARN("{}|{}|instance({}) control is null.", req->traceid(), req->requestid(),
                   req->instance().instanceid());
        auto rsp = GenScheduleResponse(static_cast<int32_t>(StatusCode::LS_INSTANCE_CTRL_IS_NULL),
                                       "instance ctrl is null", req->traceid(), req->requestid());
        Send(from, "ResponseSchedule", std::move(rsp.SerializeAsString()));
        return;
    }

    YRLOG_INFO("{}|{}|received schedule request from: {}", req->traceid(), req->requestid(), std::string(from));
    auto runtimePromise = std::make_shared<litebus::Promise<messages::ScheduleResponse>>();
    (void)instanceCtrl->Schedule(req, runtimePromise)
        .Then(litebus::Defer(GetAID(), &LocalSchedSrvActor::CollectCurrentResource, std::placeholders::_1))
        .Then(litebus::Defer(GetAID(), &LocalSchedSrvActor::ScheduleResp, std::placeholders::_1, *req, from));
}

litebus::Future<std::shared_ptr<messages::ScheduleResponse>> LocalSchedSrvActor::CollectCurrentResource(
    const messages::ScheduleResponse &resp)
{
    auto respShared = std::make_shared<messages::ScheduleResponse>(resp);
    ASSERT_IF_NULL(resourceViewMgr_);
    return resourceViewMgr_->GetChanges().Then(
        [respShared](const std::unordered_map<ResourceType, std::shared_ptr<ResourceUnitChanges>> &changes)
        -> litebus::Future<std::shared_ptr<messages::ScheduleResponse>> {
            for (const auto &[type, change] : changes) {
                if (change != nullptr) {
                    (*respShared->mutable_updateresources())[static_cast<int32_t>(type)] = std::move(*change);
                }
            }
            return respShared;
        });
}

Status LocalSchedSrvActor::ScheduleResp(const std::shared_ptr<messages::ScheduleResponse> &scheduleRsp,
                                        const messages::ScheduleRequest &req, const litebus::AID &from)
{
    ASSERT_IF_NULL(scheduleRsp);
    YRLOG_INFO("{}|send schedule instance({}) response to {}. code:{}.", req.requestid(), scheduleRsp->instanceid(),
               std::string(from), scheduleRsp->code());
    if (auto iter = req.instance().createoptions().find("SchedulingTarget");
        iter != req.instance().createoptions().end() && iter->second == "Pod") {
        YRLOG_INFO("Find pod schedule in LocalSchedSrvActor. add nodeID: {}", nodeID_);
        scheduleRsp->mutable_scheduleresult()->set_nodeid(nodeID_);
    }
    auto rspMessage = scheduleRsp->SerializeAsString();
    Send(from, "ResponseSchedule", std::move(rspMessage));
    auto type = resource_view::GetResourceType(req.instance()); // need to judge which type and del it
    (void)resourceViewMgr_->GetInf(type)
        ->TryDelResourceUnitChange(req.unitsnapshot().version(), req.unitsnapshot().localviewinittime());
    return Status::OK();
}

litebus::Future<messages::ScheduleResponse> LocalSchedSrvActor::ForwardSchedule(
    const std::shared_ptr<messages::ScheduleRequest> &req)
{
    auto forwardSchedulePromise = std::make_shared<litebus::Promise<messages::ScheduleResponse>>();
    forwardSchedulePromise_[req->requestid()] = forwardSchedulePromise;
    ForwardScheduleWithRetry(req, forwardSchedulePromise, 1);
    return forwardSchedulePromise->GetFuture();
}

litebus::Future<Status> LocalSchedSrvActor::NotifyWorkerStatus(const bool healthy)
{
    auto req = std::make_shared<messages::NotifyWorkerStatusRequest>();
    req->set_healthy(healthy);
    req->set_workerip(GetAID().GetIp());
    // cache ds-worker healthy for retry check
    dsWorkerHealthy_ = healthy;
    return litebus::Async(GetAID(), &LocalSchedSrvActor::AsyncNotifyWorkerStatus, req, false);
}

litebus::Future<Status> LocalSchedSrvActor::AsyncNotifyWorkerStatus(
    const std::shared_ptr<messages::NotifyWorkerStatusRequest> &req, const bool isRetry)
{
    if (isRetry && req->healthy() != dsWorkerHealthy_) {
        // if it is retry, but worker health has changed, no need to retry
        YRLOG_INFO("worker({}) healthy({}) changed, stop retrying", req->workerip(), req->healthy());
        return Status::OK();
    }
    YRLOG_INFO("report worker({}) healthy({}) to domain scheduler", req->workerip(), req->healthy());
    auto future = notifyWorkerStatusSync_.AddSynchronizer(req->workerip() + "_" + std::to_string(req->healthy()));
    (void)Send(domainSchedRegisterInfo_.aid, "NotifyWorkerStatus", std::move(req->SerializeAsString()));
    return future.OnComplete([aid(GetAID()), req](const litebus::Future<Status> &statusFut) -> litebus::Future<Status> {
        if (statusFut.IsError()) {
            YRLOG_WARN("failed to notify worker({}) healthy({}) code:{}, retrying", req->workerip(), req->healthy(),
                       statusFut.GetErrorCode());
            return litebus::Async(aid, &LocalSchedSrvActor::AsyncNotifyWorkerStatus, req, true);
        }
        return Status::OK();
    });
}

void LocalSchedSrvActor::ResponseNotifyWorkerStatus(const litebus::AID &from, std::string &&, std::string &&msg)
{
    YRLOG_INFO("received Notify worker status response from {}, {}", std::string(from), msg);
    messages::NotifyWorkerStatusResponse rsp;
    if (!rsp.ParseFromString(msg)) {
        YRLOG_ERROR("invalid Notify response {}. ignored", msg);
        return;
    }
    notifyWorkerStatusSync_.Synchronized(rsp.workerip() + "_" + std::to_string(rsp.healthy()), Status::OK());
}

void LocalSchedSrvActor::ForwardScheduleWithRetry(
    const std::shared_ptr<messages::ScheduleRequest> &req,
    const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &promise, const uint32_t retryTimes)
{
    auto count = retryTimes <= 0 ? 0 : retryTimes - 1;
    auto confTimeout = req->instance().scheduleoption().initcalltimeout() * 1000;
    bool isTimeout =
        confTimeout > 0 ? (count * forwardRequestTimeOutMs_ >= confTimeout) : (retryTimes > FORWARD_SCHEDULE_MAX_RETRY);
    if (isTimeout) {
        YRLOG_ERROR(
            "{}|forward to domain scheduler get response timeout, after max retry times({}) or reach max timeout({}ms)",
            req->requestid(), FORWARD_SCHEDULE_MAX_RETRY, confTimeout);
        GenErrorForwardResponseClearPromise(req, promise, "forward to domain scheduler timeout",
                                            static_cast<int32_t>(StatusCode::LS_FORWARD_DOMAIN_TIMEOUT));
        return;
    }
    if (domainSchedRegisterInfo_.aid.Name().empty()) {
        YRLOG_ERROR("domain scheduler AID is empty, failed to forward schedule to global scheduler");
        GenErrorForwardResponseClearPromise(req, promise, "domain scheduler AID is empty",
                                            static_cast<int32_t>(StatusCode::LS_DOMAIN_SCHEDULER_AID_EMPTY));
        return;
    }
    auto aid = GetAID();
    RETURN_IF_NULL(resourceViewMgr_);
    (void)resourceViewMgr_->GetChanges().Then(
        [aid, req](const std::unordered_map<ResourceType, std::shared_ptr<ResourceUnitChanges>> &changes) {
            for (const auto &[type, change] : changes) {
                (*req->mutable_updateresources())[static_cast<int32_t>(type)] = std::move(*change);
            }
            litebus::Async(aid, &LocalSchedSrvActor::SendForwardToDomain, req);
            return true;
        });
    (void)promise->GetFuture().After(
        forwardRequestTimeOutMs_,
        [aid, req, promise, retryTimes](const litebus::Future<messages::ScheduleResponse> &future) {
            YRLOG_WARN("{}|forward to domain scheduler get response timeout, begin to retry, times({})",
                       req->requestid(), retryTimes);
            litebus::Async(aid, &LocalSchedSrvActor::ForwardScheduleWithRetry, req, promise, retryTimes + 1);
            return messages::ScheduleResponse{};
        });
}

void LocalSchedSrvActor::SendForwardToDomain(const std::shared_ptr<messages::ScheduleRequest> &req)
{
    YRLOG_INFO("{}|{}|forward schedule request to domain scheduler {}, instance({})", req->traceid(), req->requestid(),
               std::string(domainSchedRegisterInfo_.aid), req->instance().instanceid());
    (void)Send(domainSchedRegisterInfo_.aid, "ForwardSchedule", std::move(req->SerializeAsString()));
}

void LocalSchedSrvActor::GenErrorForwardResponseClearPromise(
    const std::shared_ptr<messages::ScheduleRequest> &req,
    const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &promise, const std::string &errorMsg,
    const int32_t code)
{
    auto errRsp = GenScheduleResponse(code, errorMsg, req->traceid(), req->requestid());
    promise->SetValue(std::move(errRsp));
    if (auto iter(forwardSchedulePromise_.find(req->requestid())); iter != forwardSchedulePromise_.end()) {
        (void)forwardSchedulePromise_.erase(iter);
    }
}

void LocalSchedSrvActor::ResponseForwardSchedule(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::ScheduleResponse scheduleRsp;
    (void)scheduleRsp.ParseFromString(msg);
    if (from != domainSchedRegisterInfo_.aid) {
        YRLOG_WARN("froward schedule response ({}) from unexpected domain({}), expect ({}).",
                   scheduleRsp.requestid(), std::string(from), std::string(domainSchedRegisterInfo_.aid));
        return;
    }
    YRLOG_INFO("{}|received forward schedule response from domain scheduler: {}, code: {}, message: {}",
               scheduleRsp.requestid(), std::string(from), scheduleRsp.code(), scheduleRsp.message());
    if (auto iter(forwardSchedulePromise_.find(scheduleRsp.requestid())); iter != forwardSchedulePromise_.end()) {
        iter->second->SetValue(scheduleRsp);
        (void)forwardSchedulePromise_.erase(iter);
        return;
    }

    YRLOG_WARN("{}|failed to get forward schedule promise", scheduleRsp.requestid());
}

litebus::Future<messages::ForwardKillResponse> LocalSchedSrvActor::ForwardKillToInstanceManager(
    const std::shared_ptr<messages::ForwardKillRequest> &req)
{
    auto forwardKillPromise = std::make_shared<litebus::Promise<messages::ForwardKillResponse>>();
    forwardKillPromise_[req->requestid()] = forwardKillPromise;
    ForwardKillWithRetry(req, 1);
    return forwardKillPromise->GetFuture();
}

void LocalSchedSrvActor::ForwardKillWithRetry(const std::shared_ptr<messages::ForwardKillRequest> &req,
                                              const uint32_t retryTimes)
{
    if (forwardKillPromise_.find(req->requestid()) == forwardKillPromise_.end()) {
        return;
    }

    if (retryTimes > FORWARD_KILL_MAX_RETRY) {
        YRLOG_ERROR("{}|forward kill to InstanceManager get response timeout, after max retry times({})",
                    req->requestid(), FORWARD_KILL_MAX_RETRY);
        GenErrorForwardKillClearPromise(req, "forward to InstanceManager timeout",
                                        static_cast<int32_t>(StatusCode::LS_FORWARD_INSTANCE_MANAGER_TIMEOUT));
        return;
    }

    litebus::AID instanceManagerAID(INSTANCE_MANAGER_ACTOR_NAME, globalSchedRegisterInfo_.aid.Url());
    YRLOG_INFO("{}|forward kill request to InstanceManager {}, instance({}), retry times({})", req->requestid(),
               std::string(instanceManagerAID), req->instance().instanceid(), retryTimes);
    (void)Send(instanceManagerAID, "ForwardKill", std::move(req->SerializeAsString()));
    litebus::AsyncAfter(FORWARD_KILL_TIMEOUT, GetAID(), &LocalSchedSrvActor::ForwardKillWithRetry, req, retryTimes + 1);
}

void LocalSchedSrvActor::ResponseForwardKill(const litebus::AID &from, std::string &&, std::string &&msg)
{
    messages::ForwardKillResponse rsp;
    if (!rsp.ParseFromString(msg)) {
        YRLOG_ERROR("failed to parse ForwardKillResponse");
        return;
    }

    YRLOG_INFO("{}|received forward kill response from InstanceManager: {}, code: {}, message: {}", std::string(from),
               rsp.requestid(), rsp.code(), rsp.message());
    if (auto iter(forwardKillPromise_.find(rsp.requestid())); iter != forwardKillPromise_.end()) {
        iter->second->SetValue(rsp);
        (void)forwardKillPromise_.erase(iter);
    }
}

void LocalSchedSrvActor::GenErrorForwardKillClearPromise(const std::shared_ptr<messages::ForwardKillRequest> &req,
                                                         const std::string &errorMsg, const int32_t code)
{
    auto iter = forwardKillPromise_.find(req->requestid());
    if (iter == forwardKillPromise_.end()) {
        YRLOG_ERROR("{}|failed to find kill promise for {}", req->requestid(), req->instance().instanceid());
        return;
    }
    messages::ForwardKillResponse rsp;
    rsp.set_requestid(req->requestid());
    rsp.set_code(code);
    rsp.set_message(errorMsg);
    iter->second->SetValue(std::move(rsp));
    (void)forwardKillPromise_.erase(iter);
}

void LocalSchedSrvActor::UpdateSchedTopoView(const litebus::AID &from, std::string &&, std::string &&msg)
{
    if (from.HashString() != globalSchedRegisterInfo_.aid.HashString()) {
        YRLOG_WARN("message dropped. UpdateSchedTopoView from non-registered global, global: {}, from {}",
                   globalSchedRegisterInfo_.aid.HashString(), from.HashString());
        return;
    }
    messages::ScheduleTopology topology;
    (void)topology.ParseFromString(msg);
    YRLOG_INFO("update domain scheduler info, name: {}, address: {}", topology.leader().name(),
               topology.leader().address());
    domainSchedRegisterInfo_.aid.SetName(topology.leader().name() + DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX);
    domainSchedRegisterInfo_.aid.SetUrl(topology.leader().address());
    domainSchedRegisterInfo_.name = topology.leader().name();
    (void)DoRegistry(domainSchedRegisterInfo_, true);
    ASSERT_IF_NULL(resourceViewMgr_);
    resourceViewMgr_->UpdateDomainUrlForLocal(domainSchedRegisterInfo_.aid.Url());
    if (pingPongDriver_ == nullptr) {
        pingPongDriver_ = std::make_shared<HeartbeatClientDriver>(
            litebus::os::Join(componentName_, nodeID_, '-'),
            [aid(GetAID())](const litebus::AID &) { litebus::Async(aid, &LocalSchedSrvActor::TimeOutEvent); });
    }
    litebus::AID heartBeatAID(HEARTBEAT_OBSERVER_BASENAME + DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME,
                              domainSchedRegisterInfo_.aid.Url());
    YRLOG_DEBUG("UpdateSchedTopoView, StartPingPong nodeID: {}, AID: {}", nodeID_, std::string(heartBeatAID));
    pingPongDriver_->Start(heartBeatAID);
}

litebus::Future<Status> LocalSchedSrvActor::Register()
{
    return RegisterToGlobalScheduler()
           .Then(litebus::Defer(GetAID(), &LocalSchedSrvActor::RegisterToDomainScheduler, std::placeholders::_1))
           .Then([](const messages::Registered &registered) {
               return Status(StatusCode(registered.code()), registered.message());
           })
           .OnComplete(litebus::Defer(GetAID(), &LocalSchedSrvActor::EnableLocalSrv, std::placeholders::_1));
}

litebus::Future<messages::Registered> LocalSchedSrvActor::RegisterToGlobalScheduler()
{
    YRLOG_INFO("start to register to global scheduler, from: {}, to: {}", std::string(GetAID()),
               std::string(globalSchedRegisterInfo_.aid));
    if (globalSchedRegisterInfo_.aid.Name().empty()) {
        YRLOG_ERROR("failed to register to global scheduler, global scheduler AID is empty");
        messages::Registered registered;
        registered.set_code(static_cast<int32_t>(StatusCode::LS_GLOBAL_SCHEDULER_AID_EMPTY));
        registered.set_message("global scheduler AID is empty");
        return registered;
    }

    return DoRegistry(globalSchedRegisterInfo_);
}

litebus::Future<messages::Registered> LocalSchedSrvActor::RegisterToDomainScheduler(
    const messages::Registered &registered)
{
    if (registered.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
        YRLOG_ERROR("failed to register to global scheduler, errCode: {}, errMsg: {}", registered.code(),
                    registered.message());
        return registered;
    }
    YRLOG_INFO("start to register to domain scheduler, aid: {}", std::string(domainSchedRegisterInfo_.aid));
    if (std::string(domainSchedRegisterInfo_.aid.Name()).empty()) {
        YRLOG_ERROR("failed to register to domain scheduler, domain scheduler AID is empty");
        return GenRegistered(static_cast<int32_t>(StatusCode::LS_DOMAIN_SCHEDULER_AID_EMPTY),
                             "domain scheduler AID is empty");
    }

    return DoRegistry(domainSchedRegisterInfo_);
}

litebus::Future<messages::Registered> LocalSchedSrvActor::DoRegistry(RegisterInfo &info, bool isRetry)
{
    auto reg = std::make_shared<messages::Register>();
    reg->set_name(nodeID_);
    reg->set_address(GetAID().UnfixUrl());

    ASSERT_IF_NULL(resourceViewMgr_);
    resourceViewMgr_->GetResources().Then(
        litebus::Defer(GetAID(), &LocalSchedSrvActor::SendRegisterWithRes, info.aid, reg, std::placeholders::_1));
    (void)litebus::TimerTools::Cancel(info.reRegisterTimer);
    info.reRegisterTimer = litebus::AsyncAfter(litebus::Duration(registerCycleMs_), GetAID(),
                                               &LocalSchedSrvActor::RetryRegistry, info.aid);
    // if request is retry register, no need to create a new promise because function of Register uses original future
    if (!isRetry && !info.registeredPromise.GetFuture().IsInit()) {
        info.registeredPromise = litebus::Promise<messages::Registered>();
    }
    return info.registeredPromise.GetFuture();
}

Status LocalSchedSrvActor::SendRegisterWithRes(
    const litebus::AID aid, const std::shared_ptr<messages::Register> &req,
    const std::unordered_map<ResourceType, std::shared_ptr<ResourceUnit>> &resources)
{
    ASSERT_IF_NULL(req);
    for (auto &[type, resource] : resources) {
        (*req->mutable_resources())[static_cast<int32_t>(type)] = std::move(*resource);
    }
    (void)Send(aid, "Register", req->SerializeAsString());
    return Status::OK();
}

void LocalSchedSrvActor::RetryRegistry(const litebus::AID &aid)
{
    YRLOG_INFO("retry registry to {}", std::string(aid));
    if (aid.Name() == globalSchedRegisterInfo_.aid.Name()) {
        (void)DoRegistry(globalSchedRegisterInfo_, true);
    } else if (aid.Name().find(DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX) != std::string::npos) {
        (void)DoRegistry(domainSchedRegisterInfo_, true);
    } else {
        YRLOG_WARN("invalid actor name: {}", std::string(aid));
    }
}

void LocalSchedSrvActor::Registered(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    YRLOG_INFO("receive message({}) from {}", name, std::string(from));
    messages::Registered registered;
    (void)registered.ParseFromString(msg);

    // registered message from global scheduler
    if (from.Name() == globalSchedRegisterInfo_.aid.Name()) {
        if (from.Url() != globalSchedRegisterInfo_.aid.Url()) {
            YRLOG_WARN("global scheduler address is changed, expect {}, actual {}", globalSchedRegisterInfo_.aid.Url(),
                       from.Url());
            return;
        }
        if (registered.code() != int32_t(StatusCode::SUCCESS)) {
            YRLOG_ERROR("failed to register to global scheduler, errCode: {}, errMsg: {}", registered.code(),
                        registered.message());
        } else {
            (void)litebus::TimerTools::Cancel(globalSchedRegisterInfo_.reRegisterTimer);
            auto leader = registered.topo().leader();
            domainSchedRegisterInfo_.aid.SetName(leader.name() + DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME_POSTFIX);
            domainSchedRegisterInfo_.aid.SetUrl(leader.address());
            domainSchedRegisterInfo_.name = leader.name();
            ASSERT_IF_NULL(resourceViewMgr_);
            resourceViewMgr_->UpdateDomainUrlForLocal(domainSchedRegisterInfo_.aid.Url());
            YRLOG_INFO("succeed to register to global scheduler, domain scheduler name: {}, address: {}", leader.name(),
                       leader.address());
            globalSchedRegisterInfo_.registeredPromise.SetValue(registered);
        }
    } else if (from.Name() == domainSchedRegisterInfo_.aid.Name()) {  // registered message from domain scheduler
        if (from.Url() != domainSchedRegisterInfo_.aid.Url()) {
            YRLOG_WARN("domain scheduler address is changed, expect {}, actual {}", domainSchedRegisterInfo_.aid.Url(),
                       from.Url());
            return;
        }
        if (registered.code() != int32_t(StatusCode::SUCCESS)) {
            YRLOG_ERROR("failed to register to domain scheduler, errCode: {}, errMsg: {}", registered.code(),
                        registered.message());
        } else {
            (void)litebus::TimerTools::Cancel(domainSchedRegisterInfo_.reRegisterTimer);
            YRLOG_INFO("succeed to register to domain scheduler");
            domainSchedRegisterInfo_.registeredPromise.SetValue(registered);
        }
    } else {
        YRLOG_WARN("get unexpected name of: {}", from.Name());
    }
}

litebus::Future<Status> LocalSchedSrvActor::EnableLocalSrv(const litebus::Future<Status> &future)
{
    litebus::Promise<Status> promiseRet;
    if (future.IsError()) {
        YRLOG_ERROR("failed to enable local service");
        promiseRet.SetFailed(static_cast<int32_t>(StatusCode::LS_REGISTRY_TIMEOUT));
        return promiseRet.GetFuture();
    }

    auto status = future.Get();
    if (status.StatusCode() != StatusCode::SUCCESS) {
        YRLOG_ERROR("failed to enable local service, code: {}", fmt::underlying(status.StatusCode()));
        return status;
    }

    if (pingPongDriver_ == nullptr) {
        pingPongDriver_ = std::make_shared<HeartbeatClientDriver>(
            litebus::os::Join(componentName_, nodeID_, '-'),
            [aid(GetAID()), nodeID = nodeID_](const litebus::AID &dst) {
                YRLOG_WARN("pingpong {} timeout, from proxy to domain. dst: {}", nodeID, std::string(dst));
                litebus::Async(aid, &LocalSchedSrvActor::TimeOutEvent);
            });
    }
    litebus::AID heartBeatAID(HEARTBEAT_OBSERVER_BASENAME + DOMAIN_UNDERLAYER_SCHED_MGR_ACTOR_NAME,
                              domainSchedRegisterInfo_.aid.Url());
    YRLOG_DEBUG("EnableLocalSrv, StartPingPong nodeID: {}, AID: {}", nodeID_, std::string(heartBeatAID));
    (void)pingPongDriver_->Start(heartBeatAID);

    // report the resource periodly is removed, implement code should be removed in the future.
    enableService_ = true;
    return status;
}

void LocalSchedSrvActor::TimeOutEvent()
{
    if (exiting_) {
        YRLOG_INFO("local is exiting, no need to register.");
        return;
    }
    YRLOG_ERROR("the heartbeat between local scheduler and domain scheduler times out");
    enableService_ = false;
    (void)litebus::Async(GetAID(), &LocalSchedSrvActor::Register);
}


void LocalSchedSrvActor::DeletePodResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::DeletePodResponse rsp;
    if (!rsp.ParseFromString(msg)) {
        YRLOG_ERROR("invalid delete pod response {}. ignored", msg);
        return;
    }
    YRLOG_DEBUG("{}|receive delete pod response from {}, code is {}", rsp.requestid(), from.HashString(), rsp.code());
    if (rsp.code() == 0) {
        deletePodMatch_.Synchronized(rsp.requestid(), Status::OK());
    } else {
        deletePodMatch_.Synchronized(rsp.requestid(), Status(StatusCode::FAILED));
    }
}

void LocalSchedSrvActor::DeletePod(const std::string &agentID, const std::string &reqID, const std::string &msg)
{
    if (!isK8sEnabled_) {
        return;
    }
    auto deletePodRequest = std::make_shared<messages::DeletePodRequest>();
    deletePodRequest->set_requestid(reqID);
    deletePodRequest->set_functionagentid(agentID);
    deletePodRequest->set_message(msg);
    YRLOG_INFO("{}|send deletePod request to domain, agent: {}, msg: {}", deletePodRequest->requestid(),
               deletePodRequest->functionagentid(), deletePodRequest->message());
    auto future = deletePodMatch_.AddSynchronizer(reqID);
    Send(domainSchedRegisterInfo_.aid, "DeletePod", deletePodRequest->SerializeAsString());
    future.OnComplete([aid(GetAID()), deletePodRequest](const litebus::Future<Status> &status) {
        if (status.IsError() || status.Get().IsError()) {
            YRLOG_WARN("{}|failed to delete pod({}), start to retry", deletePodRequest->requestid(),
                       deletePodRequest->functionagentid());
            litebus::Async(aid, &LocalSchedSrvActor::DeletePod, deletePodRequest->functionagentid(),
                           deletePodRequest->requestid(), deletePodRequest->message());
        }
    });
}

void LocalSchedSrvActor::Init()
{
    (void)Explorer::GetInstance().AddLeaderChangedCallback(
        "LocalSchedSrv", [aid(GetAID())](const LeaderInfo &leaderInfo) {
            litebus::Async(aid, &LocalSchedSrvActor::UpdateMasterInfo, leaderInfo);
        });
    Receive("Schedule", &LocalSchedSrvActor::Schedule);
    Receive("UpdateSchedTopoView", &LocalSchedSrvActor::UpdateSchedTopoView);
    Receive("Registered", &LocalSchedSrvActor::Registered);
    Receive("UnRegistered", &LocalSchedSrvActor::UnRegistered);
    Receive("ResponseForwardSchedule", &LocalSchedSrvActor::ResponseForwardSchedule);
    Receive("ResponseForwardKill", &LocalSchedSrvActor::ResponseForwardKill);
    Receive("ResponseNotifyWorkerStatus", &LocalSchedSrvActor::ResponseNotifyWorkerStatus);
    Receive("EvictAgent", &LocalSchedSrvActor::EvictAgent);
    Receive("NotifyEvictResultAck", &LocalSchedSrvActor::NotifyEvictResultAck);
    Receive("OnForwardGroupSchedule", &LocalSchedSrvActor::OnForwardGroupSchedule);
    Receive("OnKillGroup", &LocalSchedSrvActor::OnKillGroup);
    Receive("DeletePodResponse", &LocalSchedSrvActor::DeletePodResponse);
    Receive("PreemptInstances", &LocalSchedSrvActor::PreemptInstances);
    Receive("TryCancelResponse", &LocalSchedSrvActor::TryCancelResponse);
}

void LocalSchedSrvActor::Finalize()
{
    (void)litebus::TimerTools::Cancel(globalSchedRegisterInfo_.reRegisterTimer);
    (void)litebus::TimerTools::Cancel(domainSchedRegisterInfo_.reRegisterTimer);
}

void LocalSchedSrvActor::UpdateMasterInfo(const explorer::LeaderInfo &leaderInfo)
{
    masterAid_ = litebus::AID(LOCAL_SCHED_MGR_ACTOR_NAME, leaderInfo.address);
    masterAid_.SetProtocol(litebus::BUS_TCP);
    YRLOG_INFO("begin update master info, cur master aid: {}, new master aid: {}",
               std::string(globalSchedRegisterInfo_.aid), std::string(masterAid_));
    if (subscriptionMgr_ != nullptr) {
        (void)subscriptionMgr_->NotifyMasterIPToSubscribers(leaderInfo.address);
    }
    if (globalSchedRegisterInfo_.aid.Url() != masterAid_.Url()) {
        globalSchedRegisterInfo_.aid = masterAid_;
        (void)litebus::TimerTools::Cancel(globalSchedRegisterInfo_.reRegisterTimer);
        (void)litebus::TimerTools::Cancel(domainSchedRegisterInfo_.reRegisterTimer);
        if (!IsReady()) {
            YRLOG_WARN("local sched is not ready, register should be delay.");
            return;
        }
        (void)Register();
    }
}

litebus::Future<std::string> LocalSchedSrvActor::QueryMasterIP()
{
    if (masterAid_.GetIp().empty()) {
        return "";
    }
    return masterAid_.Url();
}

void LocalSchedSrvActor::EvictAgent(const litebus::AID &from, std::string && /* name */, std::string &&msg)
{
    auto req = std::make_shared<messages::EvictAgentRequest>();
    if (msg.empty() || !req->ParseFromString(msg)) {
        YRLOG_ERROR("invalid evict request.");
        auto status = Status(StatusCode::PARAMETER_ERROR, "invalid message");
        req->set_agentid("invalid");
        SendEvictAck(status, req, from);
        return;
    }
    YRLOG_INFO("received evict agent request. agent({}) to be evicted", req->agentid());
    ASSERT_IF_NULL(functionAgentMgr_);
    (void)functionAgentMgr_->EvictAgent(req).OnComplete(
        litebus::Defer(GetAID(), &LocalSchedSrvActor::SendEvictAck, std::placeholders::_1, req, from));
}

void LocalSchedSrvActor::PreemptInstances(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto req = std::make_shared<messages::EvictAgentRequest>();
    if (msg.empty() || !req->ParseFromString(msg)) {
        YRLOG_ERROR("invalid preempt instance request.");
        return;
    }
    messages::EvictAgentAck ack;
    ack.set_requestid(req->requestid());
    ack.set_code(StatusCode::SUCCESS);
    auto instanceCtrl = instanceCtrl_.lock();
    if (instanceCtrl == nullptr) {
        YRLOG_WARN("{}|instance control is null.", req->requestid());
        ack.set_code(StatusCode::FAILED);
        ack.set_message("failed to preempt, instance control is null");
        Send(from, "PreemptInstancesResponse", ack.SerializeAsString());
        return;
    }
    std::unordered_set<std::string> instanceSet;
    for (auto &instanceID : req->instances()) {
        instanceSet.insert(instanceID);
    }
    YRLOG_INFO("{}|received preempt request from: {}", req->requestid(), std::string(from));
    (void)instanceCtrl->EvictInstances(instanceSet, req, true);
    Send(from, "PreemptInstancesResponse", ack.SerializeAsString());
}

void LocalSchedSrvActor::SendEvictAck(const litebus::Future<Status> &status,
                                      const std::shared_ptr<messages::EvictAgentRequest> &req, const litebus::AID &to)
{
    messages::EvictAgentAck ack;
    if (status.IsError()) {
        ack.set_code(status.GetErrorCode());
        ack.set_message("failed to evict agent");
    } else {
        ack.set_code(status.Get().StatusCode());
        ack.set_message(status.Get().ToString());
    }
    ack.set_agentid(req->agentid());
    ack.set_requestid(req->requestid());
    YRLOG_INFO("{}|notify evict agent({}) request accepted", req->requestid(), req->agentid());
    Send(to, "EvictAck", ack.SerializeAsString());
}

void LocalSchedSrvActor::NotifyEvictResultAck(const litebus::AID &, std::string &&, std::string &&msg)
{
    auto ack = messages::EvictAgentResultAck();
    if (msg.empty() || !ack.ParseFromString(msg)) {
        YRLOG_WARN("invalid EvictAgentResultAck: {}", msg);
        return;
    }
    notifyEvictResultSync_.Synchronized(ack.agentid(), Status::OK());
}

void LocalSchedSrvActor::NotifyEvictResult(
    const std::shared_ptr<messages::EvictAgentResult> &req)
{
    YRLOG_INFO("notify {} evict agent({}) result", masterAid_.HashString(), req->agentid());
    auto future = notifyEvictResultSync_.AddSynchronizer(req->agentid());
    Send(masterAid_, "NotifyEvictResult", req->SerializeAsString());
    future.OnComplete([aid(GetAID()), req](const litebus::Future<Status> &future) {
        if (future.IsOK()) {
            return;
        }
        YRLOG_WARN("notify evict agent({}) result timeout, retry to send", req->agentid());
        // while notify timeout, retry it with unlimited to make sure result received by master
        litebus::Async(aid, &LocalSchedSrvActor::NotifyEvictResult, req);
    });
}

void LocalSchedSrvActor::BindFunctionAgentMgr(const std::shared_ptr<FunctionAgentMgr> &functionAgentMgr)
{
    functionAgentMgr_ = functionAgentMgr;
}

void LocalSchedSrvActor::BindSubscriptionMgr(const std::shared_ptr<SubscriptionMgr> &subscriptionMgr)
{
    ASSERT_IF_NULL(subscriptionMgr);
    subscriptionMgr_ = subscriptionMgr;
}

litebus::Future<messages::GroupResponse> LocalSchedSrvActor::ForwardGroupSchedule(
    const std::shared_ptr<messages::GroupInfo> &groupInfo)
{
    auto promise = std::make_shared<litebus::Promise<messages::GroupResponse>>();
    // requestUnBindMatch_ would not be called concurrency
    DoForwardGroupSchedule(promise, std::chrono::high_resolution_clock::now(), groupInfo);
    return promise->GetFuture();
}

void LocalSchedSrvActor::DoForwardGroupSchedule(
    const std::shared_ptr<litebus::Promise<messages::GroupResponse>> &promise,
    std::chrono::time_point<std::chrono::high_resolution_clock> beginTime,
    const std::shared_ptr<messages::GroupInfo> &groupInfo)
{
    if (!enableService_) {
        YRLOG_ERROR("local service is disabled. defer to forward");
        litebus::AsyncAfter(groupTimeout_, GetAID(), &LocalSchedSrvActor::DoForwardGroupSchedule, promise, beginTime,
                            groupInfo);
        return;
    }
    litebus::AID domainGroupCtrl(DOMAIN_GROUP_CTRL_ACTOR_NAME, domainSchedRegisterInfo_.aid.Url());
    YRLOG_INFO("{}|{}|forward gang or range({}) group({}) schedule request to {}.",
               groupInfo->traceid(), groupInfo->requestid(), groupInfo->insrangescheduler(),
               groupInfo->groupid(), std::string(domainGroupCtrl));
    auto future = requestGroupScheduleMatch_.AddSynchronizer(groupInfo->requestid());
    Send(domainGroupCtrl, "ForwardGroupSchedule", groupInfo->SerializeAsString());
    future.OnComplete([promise, beginTime, groupInfo,
            aid(GetAID())](const litebus::Future<messages::GroupResponse> &future) {
            if (future.IsError()) {
                YRLOG_WARN("{}|{}|forward group({}) schedule request timeout.", groupInfo->traceid(),
                           groupInfo->requestid(), groupInfo->groupid());
                litebus::Async(aid, &LocalSchedSrvActor::DoForwardGroupSchedule, promise, beginTime, groupInfo);
                return;
            }
            promise->SetValue(future.Get());
        });
}

void LocalSchedSrvActor::OnForwardGroupSchedule(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::GroupResponse rsp;
    if (!rsp.ParseFromString(msg)) {
        YRLOG_WARN("invalid {} response from {} msg {}, ignored", std::string(from), name, msg);
        return;
    }
    auto status = requestGroupScheduleMatch_.Synchronized(rsp.requestid(), rsp);
    if (status.IsError()) {
        YRLOG_WARN("{}|{}|received {} from {}. code {} msg {}. no found request ignore it", rsp.traceid(),
                   rsp.requestid(), name, rsp.code(), rsp.message(), from.HashString());
        return;
    }
    YRLOG_INFO("{}|{}|received {} response. code {} message {}. from {}", rsp.traceid(), rsp.requestid(), name,
               rsp.code(), rsp.message(), from.HashString());
}

litebus::Future<Status> LocalSchedSrvActor::KillGroup(const std::shared_ptr<messages::KillGroup> &killReq)
{
    auto promise = std::make_shared<litebus::Promise<Status>>();
    DoKillGroup(promise, killReq);
    return promise->GetFuture();
}

void LocalSchedSrvActor::DoKillGroup(
    const std::shared_ptr<litebus::Promise<Status>> &promise,
    const std::shared_ptr<messages::KillGroup> &killReq)
{
    if (!enableService_) {
        YRLOG_ERROR("local service is disabled. cannot forward kill group.");
        promise->SetValue(
            Status(StatusCode::ERR_INNER_COMMUNICATION, "the connection between local & domain may be lost."));
        return;
    }

    litebus::AID groupMgr(GROUP_MANAGER_ACTOR_NAME, globalSchedRegisterInfo_.aid.Url());
    YRLOG_INFO("forward kill group({}) schedule request to {}.", killReq->groupid(), std::string(groupMgr));
    auto future = requestGroupKillMatch_.AddSynchronizer(killReq->groupid());
    Send(groupMgr, "KillGroup", killReq->SerializeAsString());
    future.OnComplete([promise, killReq,
            aid(GetAID())](const litebus::Future<Status> &future) {
            if (future.IsError()) {
                YRLOG_WARN("{}|{}|forward kill group({}) request timeout.", killReq->groupid());
                litebus::Async(aid, &LocalSchedSrvActor::DoKillGroup, promise, killReq);
                return;
            }
            promise->SetValue(future.Get());
        });
}

void LocalSchedSrvActor::OnKillGroup(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::KillGroupResponse rsp;
    if (!rsp.ParseFromString(msg)) {
        YRLOG_WARN("invalid {} response from {} msg {}, ignored", std::string(from), name, msg);
        return;
    }
    if (auto status = requestGroupKillMatch_.Synchronized(rsp.groupid(),
                                                          Status(static_cast<StatusCode>(rsp.code()), rsp.message()));
        status.IsError()) {
        YRLOG_WARN("received {} from {}. code {} msg {}. no found request({}) ignore it", name, from.HashString(),
                   rsp.groupid(), rsp.code(), rsp.message());
        return;
    }
    YRLOG_INFO("received {} id({}) response. code {} message {}. from {}", name, rsp.groupid(), rsp.code(),
               rsp.message(), from.HashString());
}

void LocalSchedSrvActor::ToReady()
{
    BasisActor::ToReady();
    if (!globalSchedRegisterInfo_.aid.OK()) {
        YRLOG_WARN("global is not explored, unable to register");
        return;
    }
    YRLOG_INFO("localsrv is ready, trigger to register global {}", globalSchedRegisterInfo_.aid.HashString());
    Register();
}

litebus::Future<Status> LocalSchedSrvActor::TryCancelSchedule(
    const std::shared_ptr<messages::CancelSchedule> &cancelRequest)
{
    auto promise = std::make_shared<litebus::Promise<Status>>();
    cancelPromise_[cancelRequest->msgid()] = promise;
    litebus::Async(GetAID(), &LocalSchedSrvActor::DoTryCancel, cancelRequest, promise);
    return promise->GetFuture();
}

void LocalSchedSrvActor::DoTryCancel(const std::shared_ptr<messages::CancelSchedule> &cancelRequest,
                                     const std::shared_ptr<litebus::Promise<Status>> &promise)
{
    if (domainSchedRegisterInfo_.aid.Name().empty()) {
        YRLOG_ERROR("domain scheduler AID is empty, failed to forward cancel to domain scheduler");
        (void)cancelPromise_.erase(cancelRequest->msgid());
        return;
    }
    auto domainAid = litebus::AID(domainSchedRegisterInfo_.name + DOMAIN_SCHEDULER_SRV_ACTOR_NAME_POSTFIX,
                                  domainSchedRegisterInfo_.aid.Url());
    YRLOG_WARN("send cancel schedule request to domain(name: {}, addr: {}), cancel({}) type({}) reason({}) msgId({})",
               domainAid.Name(), domainAid.Url(), cancelRequest->id(), fmt::underlying(cancelRequest->type()),
               cancelRequest->reason(), cancelRequest->msgid());
    Send(domainAid, "TryCancelSchedule", cancelRequest->SerializeAsString());
    (void)promise->GetFuture().After(TRY_CANCEL_TIMEOUT,
                                     [aid(GetAID()), cancelRequest, promise](const litebus::Future<Status> &) {
                                         litebus::Async(aid, &LocalSchedSrvActor::DoTryCancel, cancelRequest, promise);
                                         return Status::OK();
                                     });
}

void LocalSchedSrvActor::TryCancelResponse(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    auto rsp = messages::CancelScheduleResponse();
    if (!rsp.ParseFromString(msg)) {
        YRLOG_WARN("received try cancel response from {}, invalid msg {} ignore", std::string(from), msg);
        return;
    }
    if (cancelPromise_.find(rsp.msgid()) == cancelPromise_.end()) {
        YRLOG_WARN("received try cancel response from {}, invalid msgid {} ignore", std::string(from), rsp.msgid());
        return;
    }
    cancelPromise_[rsp.msgid()]->SetValue(Status(static_cast<StatusCode>(rsp.status().code()), rsp.status().message()));
    (void)cancelPromise_.erase(rsp.msgid());
}

litebus::Future<Status> LocalSchedSrvActor::GracefulShutdown()
{
    enableService_ = false;
    exiting_ = true;
    RETURN_STATUS_IF_NULL(functionAgentMgr_, StatusCode::FAILED, "nullptr of functionAgentMgr, may not be initialized");
    auto instanceCtrl = instanceCtrl_.lock();
    RETURN_STATUS_IF_NULL(instanceCtrl, StatusCode::FAILED, "nullptr of instanceCtrl, may not be initialized");
    return instanceCtrl->GracefulShutdown()
        .Then([functionAgentMgr(functionAgentMgr_)](const Status &) { return functionAgentMgr->GracefulShutdown(); })
        .Then([instanceCtrl](const Status &status) {
            instanceCtrl->SetAbnormal();
            return status;
        })
        .Then(litebus::Defer(GetAID(), &LocalSchedSrvActor::UnRegister));
}

void LocalSchedSrvActor::UnRegistered(const litebus::AID &from, std::string &&name, std::string &&msg)
{
    YRLOG_INFO("receive message({}) from {}", name, std::string(from));
    messages::Registered unregistered;
    (void)unregistered.ParseFromString(msg);
    if (unregistered.code() != static_cast<int32_t>(StatusCode::SUCCESS)) {
        YRLOG_INFO("UnRegister failed(code:{} msg:{}), defer to retry", unregistered.code(), unregistered.message());
        return;
    }
    if (!unRegistered_.GetFuture().IsInit()) {
        return;
    }
    unRegistered_.SetValue(Status::OK());
}

litebus::Future<Status> LocalSchedSrvActor::UnRegister()
{
    if (!unRegistered_.GetFuture().IsInit()) {
        YRLOG_WARN("local is already unregistered");
        return unRegistered_.GetFuture();
    }
    if (globalSchedRegisterInfo_.aid.Name().empty()) {
        return Status(StatusCode::LS_GLOBAL_SCHEDULER_AID_EMPTY);
    }
    auto reg = std::make_shared<messages::Register>();
    reg->set_name(nodeID_);
    reg->set_address(GetAID().UnfixUrl());
    auto aid = globalSchedRegisterInfo_.aid;
    Send(aid, "UnRegister", reg->SerializeAsString());
    return unRegistered_.GetFuture().After(UNREGISTER_TIMEOUT, [aid(GetAID())](const litebus::Future<Status> &future) {
        return litebus::Async(aid, &LocalSchedSrvActor::UnRegister);
    });
}

litebus::Future<Status> LocalSchedSrvActor::IsRegisteredToGlobal()
{
    // never set failed
    return globalSchedRegisterInfo_.registeredPromise.GetFuture().Then(
        [](const messages::Registered &) -> litebus::Future<Status> { return Status::OK(); });
}
} // namespace functionsystem::local_scheduler