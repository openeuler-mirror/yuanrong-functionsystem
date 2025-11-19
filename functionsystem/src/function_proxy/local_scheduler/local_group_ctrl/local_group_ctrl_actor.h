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
#ifndef LOCAL_SCHEDULER_GROUP_CTRL_ACTOR_H
#define LOCAL_SCHEDULER_GROUP_CTRL_ACTOR_H
#include "actor/actor.hpp"
#include "async/future.hpp"
#include "common/utils/actor_driver.h"
#include "meta_store_client/meta_store_client.h"
#include "common/proto/pb/posix_pb.h"
#include "common/schedule_decision/scheduler.h"
#include "common/resource_view/resource_type.h"
#include "common/resource_view/resource_view_mgr.h"
#include "function_proxy/common/posix_client/control_plane_client/control_interface_client_manager_proxy.h"
#include "function_proxy/local_scheduler/instance_control/instance_ctrl.h"

namespace functionsystem::local_scheduler {
using GroupInfos = std::vector<std::shared_ptr<messages::GroupInfo>>;
struct GroupContext {
    std::shared_ptr<messages::GroupInfo> groupInfo;
    std::vector<std::shared_ptr<messages::ScheduleRequest>> requests;
    litebus::Promise<std::shared_ptr<CreateResponses>> persistingPromise;
    std::vector<std::shared_ptr<litebus::Promise<Status>>> groupInsPromise;
    bool insRangeScheduler;
    std::shared_ptr<messages::ScheduleRequest> insRangeRequest;
    void UpdateInfo()
    {
        if (insRangeScheduler) {
            ASSERT_FS(groupInfo->requests_size() <= static_cast<int>(requests.size()));
            auto infoRequestSize = groupInfo->requests_size();
            for (size_t i = 0; i < static_cast<std::uint32_t>(infoRequestSize) - 1; i++) {
                groupInfo->mutable_requests(i)->CopyFrom(*requests[i]);
            }
            auto rangeInstanceID = insRangeRequest->instance().instanceid();
            groupInfo->mutable_requests(infoRequestSize - 1)->CopyFrom(*insRangeRequest);
            groupInfo->mutable_requests(infoRequestSize - 1)
                ->mutable_instance()->CopyFrom(requests[infoRequestSize - 1]->instance());
            groupInfo->mutable_requests(infoRequestSize - 1)
                ->mutable_instance()->set_instanceid(rangeInstanceID);
            groupInfo->clear_rangerequests();
            for (auto i = infoRequestSize - 1; i < static_cast<int>(requests.size()); i++) {
                auto rangeReq = groupInfo->add_rangerequests();
                rangeReq->CopyFrom(*requests[i]);
            }
        } else {
            ASSERT_FS(groupInfo->requests_size() == static_cast<int>(requests.size()));
            for (size_t i = 0; i < requests.size(); i++) {
                groupInfo->mutable_requests(i)->CopyFrom(*requests[i]);
            }
        }
    }
};
class LocalGroupCtrlActor : public BasisActor {
public:
    LocalGroupCtrlActor(const std::string &name, const std::string &nodeID,
                        const std::shared_ptr<MetaStoreClient> &metaStoreClient);
    LocalGroupCtrlActor(const std::string &name, const std::string &nodeID,
                        const std::shared_ptr<MetaStoreClient> &metaStoreClient, int32_t reservedTimeout);
    ~LocalGroupCtrlActor() override = default;
    /**
     * receive gang schedule instance request from client
     * @param group reqs
     * @return group response
     */
    litebus::Future<std::shared_ptr<CreateResponses>> GroupSchedule(const std::string &from,
                                                                    const std::shared_ptr<CreateRequests> &req);

    litebus::Future<Status> Sync() override;
    litebus::Future<Status> Recover() override;

    /**
     * receives resource pre-deduction from the domain.
     * @param msg is serilized ReserveRequest
     */
    virtual void Reserve(const litebus::AID &from, std::string &&name, std::string &&msg);

    /**
     * receives rollback resource pre-deduction
     * @param msg is serilized UnReserveRequest
     */
    virtual void UnReserve(const litebus::AID &from, std::string &&name, std::string &&msg);

    /**
     * receives instance specialization binding
     * @param msg is serilized BindRequest
     */
    virtual void Bind(const litebus::AID &from, std::string &&name, std::string &&msg);

    /**
     * Receives rollback instance specialization binding and resource pre-deduction.
     * @param msg is serilized UnBindRequest
     */
    virtual void UnBind(const litebus::AID &from, std::string &&name, std::string &&msg);

    /**
     * Receive clear group msg from GroupManagerActor
     *
     * @param from GroupManagerActor
     * @param name ClearGroup
     * @param msg is serialized
     */
    virtual void ClearGroup(const litebus::AID &from, std::string &&name, std::string &&msg);

    void OnHealthyStatus(const Status &status);

    class GroupOperator {
    public:
        explicit GroupOperator(const std::shared_ptr<MetaStoreClient> &metaStoreClient)
            : metaStoreClient_(metaStoreClient)
        {
        }
        ~GroupOperator() = default;
        litebus::Future<Status> TxnGroupInstances(const std::shared_ptr<messages::GroupInfo> &req);
        litebus::Future<GroupInfos> SyncGroupInstances();
        litebus::Future<Status> DeleteGroupInstances(const std::shared_ptr<messages::GroupInfo> &req);

    private:
        std::shared_ptr<MetaStoreClient> metaStoreClient_;
    };

    void BindScheduler(const std::shared_ptr<schedule_decision::Scheduler> &scheduler)
    {
        scheduler_ = scheduler;
    }

    void BindResourceView(const std::shared_ptr<resource_view::ResourceViewMgr> &resourceViewMgr)
    {
        resourceViewMgr_ = resourceViewMgr;
    }

    void BindControlInterfaceClientManager(const std::shared_ptr<ControlInterfaceClientManagerProxy> &mgr)
    {
        clientManager_ = mgr;
    }

    void BindInstanceCtrl(const std::shared_ptr<InstanceCtrl> &instanceCtrl)
    {
        instanceCtrl_ = instanceCtrl;
    }

    void BindLocalSchedSrv(const std::shared_ptr<LocalSchedSrv> &localSchedSrv)
    {
        localSchedSrv_ = localSchedSrv;
    }

protected:
    void Init() override;
    void Finalize() override;

private:
    bool CheckIsReady(const std::string &name);
    litebus::Future<Status> OnSyncGroup(const std::vector<std::shared_ptr<messages::GroupInfo>> &groupInfos);
    void CompareSynced(const litebus::Future<GroupInfos> &future);
    litebus::Future<Status> ToGroupInstanceScheduling(const std::shared_ptr<GroupContext> &groupCtx);
    litebus::Future<std::shared_ptr<CreateResponses>> OnGroupCreateFailed(
        const Status &status, const std::shared_ptr<GroupContext> &groupCtx);
    litebus::Future<std::shared_ptr<CreateResponses>> OnLocalGroupSchedule(
        const litebus::Future<schedule_decision::GroupScheduleResult> &future,
        const std::shared_ptr<GroupContext> &groupCtx, std::shared_ptr<CreateResponses> rsp);
    litebus::Future<std::shared_ptr<CreateResponses>> HandleLocalGroupScheduleError(
        const std::shared_ptr<GroupContext> &groupCtx, std::shared_ptr<CreateResponses> rsp);
    void HandleAllocateInsComplete(const litebus::Future<Status> future, const std::shared_ptr<GroupContext> &groupCtx,
                                   const schedule_decision::GroupScheduleResult result,
                                   std::shared_ptr<CreateResponses> rsp);
    void HandleAllocateInsSuccess(const std::shared_ptr<GroupContext> &groupCtx,
                                  const schedule_decision::GroupScheduleResult &result,
                                  std::shared_ptr<CreateResponses> rsp);
    void HandleAllocateInsError(const std::shared_ptr<GroupContext> &groupCtx,
                                std::shared_ptr<CreateResponses> rsp);
    litebus::Future<std::shared_ptr<CreateResponses>> ForwardGroupSchedule(
        const std::shared_ptr<GroupContext> &groupCtx, std::shared_ptr<CreateResponses> rsp);
    litebus::Future<std::shared_ptr<CreateResponses>> ForwardGroupScheduleDone(
        const messages::GroupResponse &groupRsp, const std::shared_ptr<GroupContext> &groupCtx,
        std::shared_ptr<CreateResponses> rsp);
    void CollectInstancesReady(const std::shared_ptr<GroupContext> &groupCtx);
    void NotifyGroupResult(const Status &status, const std::string &to,
                           const std::shared_ptr<GroupContext> &groupCtx);
    void OnGroupFailed(const Status &status, const std::shared_ptr<GroupContext> &groupCtx);
    void OnGroupSuccessful(const std::shared_ptr<GroupContext> &groupCtx);

    bool IsDuplicateGroup(const std::string &from, const std::shared_ptr<CreateRequests> &req);
    [[maybe_unused]] inline std::shared_ptr<GroupContext> NewGroupCtx(
        const std::shared_ptr<messages::GroupInfo> &groupInfo);
    inline void DeleteGroupCtx(const std::string &requestID);
    std::shared_ptr<GroupContext> GetGroupCtx(const std::string &requestID);

    void OnReserve(const litebus::AID &to, const litebus::Future<schedule_decision::ScheduleResult> &future,
                   const std::shared_ptr<messages::ScheduleRequest> &req,
                   const std::shared_ptr<messages::ScheduleResponse> &resp);

    void OnBind(const litebus::AID &to, const litebus::Future<Status> &future,
                const std::shared_ptr<messages::ScheduleRequest> &req,
                const std::shared_ptr<messages::GroupResponse> &resp);

    void SendMsg(const litebus::AID &to, const std::string &name, const std::string &msg);

    void TimeoutToBind(const std::shared_ptr<messages::ScheduleRequest> &req);

    void OnSuccessfulReserve(const litebus::AID &to, const schedule_decision::ScheduleResult &result,
                             const std::shared_ptr<messages::ScheduleRequest> &req,
                             const std::shared_ptr<messages::ScheduleResponse> &resp);

    void CollectResourceOnReserve(const litebus::AID &to, const std::shared_ptr<messages::ScheduleResponse> &resp);

    litebus::Future<std::shared_ptr<CreateResponses>> DoLocalGroupSchedule(
        const Status &status, std::shared_ptr<schedule_decision::Scheduler> scheduler,
        std::shared_ptr<GroupContext> groupCtx, std::shared_ptr<CreateResponses> resp);

    void OnBindFailed(const litebus::AID &to, const Status &status,
                      const std::shared_ptr<messages::ScheduleRequest> &req,
                      const std::shared_ptr<messages::GroupResponse> &resp);

    void OnUnBind(const litebus::AID &to, const std::shared_ptr<messages::ScheduleRequest> &req);

    void SetDeviceInfoError(const litebus::AID &to, const std::shared_ptr<messages::ScheduleRequest> &req,
                            const std::shared_ptr<messages::ScheduleResponse> &resp);

    litebus::Future<Status> SetDeviceInfoToHeteroScheduleResp(const schedule_decision::ScheduleResult &result,
        const std::shared_ptr<messages::ScheduleRequest> &req, const std::shared_ptr<messages::ScheduleResponse> &resp);

    void OnClearGroup(const litebus::AID &to, const std::string &groupID);

    void ClearLocalGroupInstanceInfo(const InstanceInfo &info);

    void OnCheckInstanceRunning(const litebus::Future<bool> &isRunning, const litebus::AID &from,
                                const std::shared_ptr<messages::ScheduleRequest> &req,
                                const std::shared_ptr<messages::GroupResponse> &resp);

private:
    struct ReservedContext {
        schedule_decision::ScheduleResult result;
        litebus::Timer reserveTimeout;
    };
    bool isStarted {false};
    std::string nodeID_;
    std::shared_ptr<ControlInterfaceClientManagerProxy> clientManager_;
    std::shared_ptr<schedule_decision::Scheduler> scheduler_;
    std::shared_ptr<GroupOperator> groupOperator_;
    std::shared_ptr<InstanceCtrl> instanceCtrl_;
    std::shared_ptr<LocalSchedSrv> localSchedSrv_;
    std::shared_ptr<resource_view::ResourceViewMgr> resourceViewMgr_;
    int32_t reserveToBindTimeoutMs_;
    std::unordered_map<std::string, ReservedContext> reserveResult_;
    std::unordered_set<std::string> bindingReqs_;
    std::unordered_map<std::string, std::shared_ptr<GroupContext>> groupCtxs_;
};
}  // namespace functionsystem::local_scheduler
#endif  // LOCAL_SCHEDULER_GROUP_CTRL_ACTOR_H
