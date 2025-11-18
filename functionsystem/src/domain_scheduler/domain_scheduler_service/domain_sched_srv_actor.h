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

#ifndef DOMAIN_SCHEDULER_SRV_H
#define DOMAIN_SCHEDULER_SRV_H
#include <actor/actor.hpp>
#include <async/future.hpp>
#include <string>

#include "common/explorer/explorer.h"
#include "common/heartbeat/heartbeat_client.h"
#include "common/proto/pb/message_pb.h"
#include "common/resource_view/resource_view_mgr.h"
#include "common/status/status.h"
#include "common/utils/request_sync_helper.h"
#include "domain_scheduler/domain_group_control/domain_group_ctrl.h"
#include "domain_scheduler/instance_control/instance_ctrl.h"
#include "include/structure.h"
#include "underlayer_scheduler_manager/underlayer_sched_mgr.h"

namespace functionsystem::domain_scheduler {
class DomainSchedSrvActor : public litebus::ActorBase {
public:
    DomainSchedSrvActor(const std::string &name, const std::shared_ptr<MetaStoreClient> &metaStoreClient,
                        uint32_t receivedPingTimeout = 0, uint32_t maxRegisterTimes = 0,
                        uint32_t registerIntervalMs = 0, uint32_t putReadyResCycleMs = 0);
    ~DomainSchedSrvActor() override = default;

    /* *
     * before srv starting, RegisterToGlobal should be called
     * @return
     */
    virtual litebus::Future<Status> RegisterToGlobal();

    /* *
     * set component name
     * @return
     */
    void SetComponentName(const std::string &componentName);

    /* *
     * Update scheduler topology
     * @param from: Caller AID
     * @param name: Interface name
     * @param msg: Serialized ScheduleTopology
     */
    void UpdateSchedTopoView(const litebus::AID &from, std::string &&name, std::string &&msg);

    /* *
     * Receive response of register from global
     * @param from: Global AID
     * @param name: Interface name
     * @param msg: Serialized Registered
     */
    void Registered(const litebus::AID &from, std::string &&name, std::string &&msg);

    /* *
     * For subscriber to get Resources
     * @param from: Subscriber AID
     * @param name: Interface name
     * @param msg: null
     */
    void PullResources(const litebus::AID &from, std::string &&name, std::string &&msg);

    /* *
     * ResponseForwardSchedule
     * @param from: Uplayer AID
     * @param name: Interface name
     * @param msg: Serialized ScheduleResponse
     */
    void ResponseForwardSchedule(const litebus::AID &from, std::string &&name, std::string &&msg);

    /* *
     * Schedule interface for uplayer scheduler or function-accessor
     * @param from: Uplayer AID
     * @param name: Interface name
     * @param msg: Serialized ScheduleResquest
     */
    void Schedule(const litebus::AID &from, std::string &&name, std::string &&msg);

    /* *
     * Schedule interface for uplayer scheduler or function-accessor
     * @param from: Uplayer AID
     * @param name: Interface name
     * @param msg: Serialized ScheduleResquest
     */
    void ResponseNotifySchedAbnormal(const litebus::AID &from, std::string &&name, std::string &&msg);

    /* *
     * Schedule interface for uplayer scheduler
     * @param from: Uplayer AID
     * @param name: Interface name
     * @param msg: Serialized NotifyWorkerStatusResponse
     */
    void ResponseNotifyWorkerStatus(const litebus::AID &from, std::string &&, std::string &&msg);

    /* *
     * query interface for global
     * @param from: global AID
     * @param name: Interface name
     * @param msg: Serialized QueryAgentInfoRequest
     */
    void QueryAgentInfo(const litebus::AID &from, std::string &&name, std::string &&msg);

    void GetSchedulingQueue(const litebus::AID &from, std::string &&, std::string &&msg);
    void GetSchedulingQueueCallBack(
        const litebus::AID &to, const std::string &requestID,
        const litebus::Future<std::vector<std::shared_ptr<messages::ScheduleRequest>>> &future);

    litebus::Future<std::vector<std::shared_ptr<messages::ScheduleRequest>>> CombineInstanceAndGroup(
        const std::vector<std::shared_ptr<messages::ScheduleRequest>> &instanceQueue);

    /* *
     * query resource information
     * @param from: global AID
     * @param name: Interface name
     * @param msg: Serialized QueryResourcesInfoRequest
     */
    void QueryResourcesInfo(const litebus::AID &from, std::string &&, std::string &&msg);

    void TryCancelSchedule(const litebus::AID &from, std::string &&name, std::string &&msg);

    /* *
     * Report the managed scheduler exception.
     * @param req: abnormal infomation
     * @return request result
     */
    virtual litebus::Future<Status> NotifySchedAbnormal(const messages::NotifySchedAbnormalRequest &req);

    virtual litebus::Future<Status> NotifyWorkerStatus(const messages::NotifyWorkerStatusRequest &req);

    /* *
     * Submit an instance scheduling request by forward req to upLayer
     * @param req: schedule request body
     * @return request result
     */
    litebus::Future<std::shared_ptr<messages::ScheduleResponse>> ForwardSchedule(
        const std::shared_ptr<messages::ScheduleRequest> &req);

    Status SendRegisterWithRes(const litebus::AID aid, const std::shared_ptr<messages::Register> &req,
                               const std::unordered_map<ResourceType, std::shared_ptr<ResourceUnit>> &resources);

    void BindInstanceCtrl(const std::shared_ptr<domain_scheduler::InstanceCtrl> &instanceCtrl)
    {
        ASSERT_IF_NULL(instanceCtrl);
        instanceCtrl_ = instanceCtrl;
    }
    void BindResourceView(const std::shared_ptr<resource_view::ResourceViewMgr> &resourceViewMgr)
    {
        ASSERT_IF_NULL(resourceViewMgr);
        resourceViewMgr_ = resourceViewMgr;
    }
    void BindUnderlayerMgr(const std::shared_ptr<UnderlayerSchedMgr> &underlayer)
    {
        ASSERT_IF_NULL(underlayer);
        underlayer_ = underlayer;
    }

    void BindDomainGroupCtrl(const std::shared_ptr<domain_scheduler::DomainGroupCtrl> &groupCtrl)
    {
        ASSERT_IF_NULL(groupCtrl);
        groupCtrl_ = groupCtrl;
    }

    virtual void UpdateMasterInfo(const explorer::LeaderInfo &leaderInfo);

    litebus::Future<Status> EnableMetrics(const bool enableMetrics);

protected:
    void Init() override;
    void Finalize() override;

    virtual void RegisterToLeader();

    virtual void PingPongLost(const litebus::AID &lostDst);

    virtual void UpdateLeader(const std::string &name, const std::string &address);

    struct RegisterUp {
        litebus::AID aid;
        litebus::Promise<Status> registered;
        litebus::Timer reRegisterTimer;
        uint32_t timeouts = 0;
    };

    [[maybe_unused]] void SetUplayer(const RegisterUp &uplayer)
    {
        uplayer_ = uplayer;
    }

    [[maybe_unused]] void SetGlobal(const RegisterUp &global)
    {
        global_ = global;
    }

    virtual void Registered(const messages::Registered &message, RegisterUp &registry);

private:
    void RegisterTimeout(const litebus::AID &aid);
    void RegisterTrigger(RegisterUp &registry);
    litebus::Future<std::shared_ptr<messages::ScheduleResponse>> CollectCurrentResource(
        const litebus::Future<std::shared_ptr<messages::ScheduleResponse>> &response);
    void ScheduleCallback(const litebus::AID &to,
                          const litebus::Future<std::shared_ptr<messages::ScheduleResponse>> &response,
                          const std::shared_ptr<messages::ScheduleRequest> &req);
    void UpdateResourceToSubscriber(const litebus::AID &to, const litebus::Future<std::string> &view);
    void UpdateResourceToUpLayer();

    void PutReadyResCycle();
    void PutReadyRes();
    uint32_t CountReadyRes(const std::shared_ptr<resource_view::ResourceUnit> &unit,
                           const std::shared_ptr<std::unordered_set<std::string>> &curReadyAgent);
    uint32_t DoCountReadyRes(const resource_view::ResourceUnit &unit,
                             const std::shared_ptr<std::unordered_set<std::string>> &curReadyAgent);

    Status DoPutReadyRes(uint32_t readyResCnt, const std::shared_ptr<uint32_t> &prevrescnt,
                         const std::shared_ptr<std::unordered_set<std::string>> &curReadyAgent,
                         const std::shared_ptr<std::unordered_set<std::string>> &prevReadyAgent);

    void StartCollectClusterResourceState();
    void CollectClusterResourceState();
    void StopCollectClusterResourceState();
    void ExtractAgentInfo(const resource_view::ResourceUnit &unit,
                          google::protobuf::RepeatedPtrField<resources::AgentInfo> &agentInfos);
    void QueryAgentInfoCallBack(const litebus::AID &to, const std::string &requestID,
                                const litebus::Future<std::shared_ptr<resource_view::ResourceUnit>> &future);
    void QueryResourcesInfoCallBack(const litebus::AID &to, const std::string &requestID,
                                    const litebus::Future<std::shared_ptr<resource_view::ResourceUnit>> &future);

    void StartPingPong(const std::string &address);

    std::string domainName_;
    std::shared_ptr<MetaStoreClient> metaStoreClient_;
    uint32_t maxRegisterTimes_;
    uint32_t registerIntervalMs_;
    uint32_t putReadyResCycleMs_;
    uint32_t receivedPingTimeout_;
    RegisterUp global_;
    RegisterUp uplayer_;
    std::unique_ptr<HeartbeatClientDriver> pingpong_;
    std::shared_ptr<InstanceCtrl> instanceCtrl_;
    std::shared_ptr<ResourceViewMgr> resourceViewMgr_;
    std::shared_ptr<messages::ScheduleRequest> scheduleRequest_;

    std::shared_ptr<UnderlayerSchedMgr> underlayer_;
    std::shared_ptr<DomainGroupCtrl> groupCtrl_;
    litebus::AID masterAid_;
    bool isHeader_{ false };  // this indicates whether the domain is the head node.
    bool enableMetrics_{ false };
    litebus::Timer metricExportTimer_;

    std::shared_ptr<litebus::Timer> putReadyResTimer_ = nullptr;
    std::unordered_set<std::string> recivedSchedulingReq_;

    const uint32_t scheduleTimeout_ = 60000;
    REQUEST_SYNC_HELPER(DomainSchedSrvActor, std::shared_ptr<messages::ScheduleResponse>, scheduleTimeout_,
                        scheduleSync_);
    const uint32_t notifyAbnormalTimeout_ = 5000;
    REQUEST_SYNC_HELPER(DomainSchedSrvActor, Status, notifyAbnormalTimeout_, notifyAbnormalSync_);
    const uint32_t notifyWorkerStatusTimeout_ = 5000;
    REQUEST_SYNC_HELPER(DomainSchedSrvActor, Status, notifyWorkerStatusTimeout_, notifyWorkerStatusSync_);
    std::string componentName_ = "";
};
}  // namespace functionsystem::domain_scheduler
#endif  // DOMAIN_SCHEDULER_SRV_H
