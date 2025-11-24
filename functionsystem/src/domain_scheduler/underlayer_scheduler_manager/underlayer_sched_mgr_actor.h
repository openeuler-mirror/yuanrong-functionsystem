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

#ifndef DOMAIN_SCHED_UNDERLAYER_SCHED_MGR_H
#define DOMAIN_SCHED_UNDERLAYER_SCHED_MGR_H

#include <async/future.hpp>
#include <litebus.hpp>
#include <unordered_map>
#include <utility>

#include "common/heartbeat/heartbeat_observer.h"
#include "common/proto/pb/message_pb.h"
#include "common/resource_view/resource_view_mgr.h"
#include "common/schedule_decision/preemption_controller/preemption_controller.h"
#include "common/utils/request_sync_helper.h"
#include "domain_scheduler/instance_control/instance_ctrl.h"
#include "domain_scheduler/domain_scheduler_service/domain_sched_srv.h"

namespace functionsystem::domain_scheduler {
class UnderlayerScheduler {
public:
    UnderlayerScheduler(const std::string &name, const std::string &address, const uint32_t &heartbeatTimes,
                        const uint32_t heartbeatInterval)
        : name_(name), address_(address), heartbeatMaxTimes_(heartbeatTimes), heartbeatIntervalMs_(heartbeatInterval)
    {
    }
    ~UnderlayerScheduler();

    void AddRegisterTimer(const litebus::AID &aid, uint64_t timeOutMs);

    void Registered(const litebus::AID &aid);

    void RegisterResourceClearCallback(std::function<void(std::string &)> resourceClearCallBack)
    {
        resourceClearCallBack_ = std::move(resourceClearCallBack);
    }

    litebus::Future<bool> IsRegistered()
    {
        return registered_.GetFuture();
    }

    const litebus::AID &GetAID() const
    {
        return aid_;
    }

    const std::string GetAddress() const
    {
        return address_;
    }

    const std::string GetName() const
    {
        return name_;
    }

private:
    std::string name_;
    std::string address_;
    uint32_t heartbeatMaxTimes_;
    uint32_t heartbeatIntervalMs_;
    litebus::AID aid_;
    litebus::Timer registerTimeOut_;
    litebus::Promise<bool> registered_;
    std::function<void(std::string &)> resourceClearCallBack_;
};

class UnderlayerSchedMgrActor : public litebus::ActorBase {
public:
    explicit UnderlayerSchedMgrActor(const std::string &name);
    UnderlayerSchedMgrActor(const std::string &name, const uint32_t &heartbeatTimes, const uint32_t heartbeatInterval,
                            const uint32_t &groupTimeout = 0);
    ~UnderlayerSchedMgrActor() override = default;

    /* *
     * Receive register request from underlayer scheduler(local/domain)
     * @param msg: Serialized RegisterRequest
     */
    void Register(const litebus::AID &from, std::string &&name, std::string &&msg);

    /* *
     * Receive schedule request from underlayer
     * @param msg: Serialized ScheduleRequest
     */
    void ForwardSchedule(const litebus::AID &from, std::string &&name, std::string &&msg);

    /* *
     * Receive schedule result from underlayer
     * @param msg: Serialized ScheduleResponse
     */
    void ResponseSchedule(const litebus::AID &from, std::string &&name, std::string &&msg);

    /* *
     * Receive abnormal scheduler notification from underlayer
     * @param msg: Serialized ScheduleResponse
     */
    void NotifySchedAbnormal(const litebus::AID &from, std::string &&name, std::string &&msg);

    /**
     * Receive abnormal worker notification from underlayer
     * @param msg Serialized ReportNodeFaultRequest
     */
    void NotifyWorkerStatus(const litebus::AID &from, std::string &&, std::string &&msg);

    /**
     * Received batch resource reservation return value
     * @param msg Serialized ReserveResponse
     */
    void OnReserves(const litebus::AID &from, std::string &&name, std::string &&msg);

    /**
     * Returned result of accepting instance specialization binding
     * @param msg Serialized BindResponse
     */
    void OnBind(const litebus::AID &from, std::string &&name, std::string &&msg);

    /**
     * Received revert resource reservation return value
     * @param msg Serialized ReserveResponse
     */
    void OnUnReserve(const litebus::AID &from, std::string &&name, std::string &&msg);

    /**
     * Returned result of unbind instance
     * @param msg Serialized BindResponse
     */
    void OnUnBind(const litebus::AID &from, std::string &&name, std::string &&msg);

    void DeletePod(const litebus::AID &from, std::string &&name, std::string &&msg);

    void DeletePodResponse(const litebus::AID &from, std::string &&name, std::string &&msg);

    void ResponsePreemptInstance(const litebus::AID &from, std::string &&, std::string &&msg);

    /* *
     * Batch Reserve request resource to underlayer
     */
    litebus::Future<std::shared_ptr<messages::OnReserves>> Reserves(
        const std::string &selectedName, const std::shared_ptr<messages::Reserves> &req);

    litebus::Future<Status> UnReserve(const std::string &selectedName,
                                      const std::shared_ptr<messages::ScheduleRequest> &req);

    void SetScalerAddress(const std::string &address);

    /* *
     * instance specialization binding to underlayer
     */
    litebus::Future<Status> Bind(const std::string &selectedName,
                                 const std::shared_ptr<messages::ScheduleRequest> &req);

    litebus::Future<Status> UnBind(const std::string &selectedName,
                                   const std::shared_ptr<messages::ScheduleRequest> &req);
    /* *
     * Dispatch schedule request to underlayer
     */
    litebus::Future<std::shared_ptr<messages::ScheduleResponse>> DispatchSchedule(
        const std::string &selectedName, const std::shared_ptr<messages::ScheduleRequest> &req);

    void UpdateUnderlayerTopo(const messages::ScheduleTopology &req);

    void NotifyAbnormal(const messages::NotifySchedAbnormalRequest &req);

    void DeleteUnfinishedScheduleReqeust(const std::string &to, const std::string &requestID);
    void InsertUnfinishedScheduleRequest(const std::string &to, const std::string &requestID);
    void ClearAbnormalUnfinishedCache(const std::string &schedName);

    void PreemptInstance(const std::vector<schedule_decision::PreemptResult> &preemptResults);

    litebus::Future<Status> AsyncPreemptInstance(const litebus::AID &proxyID,
                                                 const std::shared_ptr<messages::EvictAgentRequest> &req);

    litebus::Future<bool> IsRegistered(const std::string &name);

    void SetDomainLevel(bool isHeader)
    {
        isHeader_ = isHeader;
    }

    // must be called before actor spawn
    void BindDomainService(const std::shared_ptr<DomainSchedSrv> &domainSrv)
    {
        ASSERT_IF_NULL(domainSrv);
        domainSrv_ = domainSrv;
    }
    // must be called before actor spawn
    void BindResourceView(const std::shared_ptr<resource_view::ResourceViewMgr> &resourceViewMgr)
    {
        ASSERT_IF_NULL(resourceViewMgr);
        resourceViewMgr_ = resourceViewMgr;
    }
    // must be called before actor spawn
    void BindInstanceCtrl(const std::shared_ptr<InstanceCtrl> &instanceCtrl)
    {
        ASSERT_IF_NULL(instanceCtrl);
        instanceCtrl_ = instanceCtrl;
    }

protected:
    void Init() override;

private:
    void HeartbeatLost(const std::string &name, const std::string &address);
    litebus::Future<Status> AsyncNotifyAbnormal(const messages::NotifySchedAbnormalRequest &req);
    void NotifySchedAbnormalCallBack(const litebus::AID &to, const messages::NotifySchedAbnormalRequest &req);
    void NotifyWorkerStatusCallBack(const litebus::Future<Status> &status, const litebus::AID &to,
                                    const messages::NotifyWorkerStatusRequest &req);
    void ForwardScheduleCallback(const litebus::AID &to, const std::shared_ptr<messages::ScheduleRequest> &req,
                                 const litebus::Future<std::shared_ptr<messages::ScheduleResponse>> &rspFuture);
    litebus::Future<std::shared_ptr<messages::ScheduleResponse>> CheckForwardUplayer(
        const std::shared_ptr<messages::ScheduleRequest> &req,
        const litebus::Future<std::shared_ptr<messages::ScheduleResponse>> &rspFuture);
    void OnDeletePodComplete(const litebus::Future<std::shared_ptr<messages::DeletePodResponse>> &rsp,
                             const litebus::AID &from);

    bool isHeader_{ false };
    bool isScalerEnabled_{ false };
    litebus::AID scaler_;
    std::shared_ptr<InstanceCtrl> instanceCtrl_{ nullptr };
    std::shared_ptr<resource_view::ResourceViewMgr> resourceViewMgr_{ nullptr };
    std::shared_ptr<DomainSchedSrv> domainSrv_{ nullptr };
    std::unordered_map<std::string, std::shared_ptr<UnderlayerScheduler>> underlayers_;
    std::unordered_map<std::string, std::unordered_set<std::string>> unfinishedScheduleReqs_;
    std::unordered_set<std::string> recivedSchedulingReq_;

    const uint32_t heartbeatMaxTimes_ = 180;
    const uint32_t heartbeatInterval_ = 1000;

    const uint32_t scheduleTimeout_ = 20000;
    REQUEST_SYNC_HELPER(UnderlayerSchedMgrActor, std::shared_ptr<messages::ScheduleResponse>, scheduleTimeout_,
                        requestMatch_);

    const uint32_t groupTimeout_ = 5000;
    void DoReserves(const std::shared_ptr<litebus::Promise<std::shared_ptr<messages::OnReserves>>> &promise,
                   const std::string &selectedName, const std::shared_ptr<messages::Reserves> &req);
    void SendMethodWithRetry(const std::shared_ptr<litebus::Promise<Status>> &promise, const std::string &method,
                             RequestSyncHelper<UnderlayerSchedMgrActor, Status> *syncHelper,
                             const std::string &selectedName, const std::shared_ptr<messages::ScheduleRequest> &req);
    void ReceiveGroupMethod(RequestSyncHelper<UnderlayerSchedMgrActor, Status> *syncHelper, const litebus::AID &from,
                            std::string &&name, std::string &&msg);

    REQUEST_SYNC_HELPER(UnderlayerSchedMgrActor, std::shared_ptr<messages::OnReserves>, groupTimeout_,
                        requestReservesMatch_);
    REQUEST_SYNC_HELPER(UnderlayerSchedMgrActor, Status, groupTimeout_, requestUnReserveMatch_);
    REQUEST_SYNC_HELPER(UnderlayerSchedMgrActor, Status, groupTimeout_, requestBindMatch_);
    REQUEST_SYNC_HELPER(UnderlayerSchedMgrActor, Status, groupTimeout_, requestUnBindMatch_);
    const uint32_t deletePodTimeout_ = 5000;
    REQUEST_SYNC_HELPER(UnderlayerSchedMgrActor, std::shared_ptr<messages::DeletePodResponse>, deletePodTimeout_,
                        deletePodMatch_);
    const uint32_t preemptInstanceTimeout_ = 5000;
    REQUEST_SYNC_HELPER(UnderlayerSchedMgrActor, Status, preemptInstanceTimeout_, preemptInstanceSync_);
    int AddNewHeartbeatNode(
        const HeartbeatObserver::TimeOutHandler &handler, std::shared_ptr<UnderlayerScheduler> underlayer);
    std::shared_ptr<HeartbeatObserveDriver> heartbeatObserver_ = nullptr;
};
}  // namespace functionsystem::domain_scheduler

#endif  // DOMAIN_SCHED_UNDERLAYER_SCHED_MGR_H
