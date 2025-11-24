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
#ifndef LOCAL_SCHEDULER_BUNDLE_MANAGER_ACTOR_H
#define LOCAL_SCHEDULER_BUNDLE_MANAGER_ACTOR_H

#include "actor/actor.hpp"
#include "async/future.hpp"
#include "common/explorer/explorer.h"
#include "meta_store_client/meta_store_client.h"
#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix_pb.h"
#include "common/resource_view/resource_type.h"
#include "common/resource_view/resource_view_mgr.h"
#include "common/schedule_decision/scheduler.h"
#include "common/utils/actor_driver.h"
#include "function_proxy/local_scheduler/instance_control/instance_ctrl.h"

namespace functionsystem::local_scheduler {

const int32_t BUNDLE_RESERVE_TIMEOUT_MS = 120000;

struct BundleManagerActorParam {
    std::string actorName;
    std::string nodeId;
    std::shared_ptr<MetaStoreClient> metaStoreClient;
    int32_t reservedTimeout = BUNDLE_RESERVE_TIMEOUT_MS;
};

class BundleMgrActor : public BasisActor {
public:
    explicit BundleMgrActor(const BundleManagerActorParam &bundleManagerActorParam);
    virtual ~BundleMgrActor() = default;

    litebus::Future<Status> Sync() override;
    litebus::Future<Status> Recover() override;

    /**
     * batch reserve resource: 1.pre-deduction from resource view, 2.create bundle
     * @param msg is serialized ScheduleRequest
     */
    virtual void Reserves(const litebus::AID &from, std::string &&name, std::string &&msg);

    litebus::Future<std::shared_ptr<messages::ScheduleResponse>> DoReserve(
        std::shared_ptr<messages::ScheduleRequest> &req);

    /**
     * rollback reserve operation
     * @param msg is serialized ScheduleRequest
     */
    virtual void UnReserve(const litebus::AID &from, std::string &&name, std::string &&msg);

    /**
     * bind bundle: 1.write bundle into etcd, 2.add unit(bundle) in virtual resource view
     * @param msg is serialized ScheduleRequest
     */
    virtual void Bind(const litebus::AID &from, std::string &&name, std::string &&msg);

    /**
     * rollback bind operation
     * @param msg is serialized ScheduleRequest
     */
    virtual void UnBind(const litebus::AID &from, std::string &&name, std::string &&msg);

    virtual void ForwardReportAgentAbnormalResponse(const litebus::AID &from, std::string &&name, std::string &&msg);

    /**
     * remove bundle req from ResourceGroupManager
     *
     * @param from ResourceGroupManager
     * @param name RemoveBundle
     * @param msg is serialized RemoveBundleRequest
     */
    virtual void RemoveBundle(const litebus::AID &from, std::string &&name, std::string &&msg);

    void OnHealthyStatus(const Status &status);

    litebus::Future<Status> SyncBundles(const std::string &agentID);

    litebus::Future<Status> NotifyFailedAgent(const std::string &failedAgentID);

    litebus::Future<Status> NotifyFailedBundles(const std::set<std::string> &bundleIDs);

    litebus::Future<Status> OnReportAgentAbnormal(const std::shared_ptr<messages::ReportAgentAbnormalResponse> &resp,
                                                  const messages::ReportAgentAbnormalRequest &req);

    litebus::Future<Status> OnNotifyFailedAgent(const Status &status, const std::string &failedAgentID);

    litebus::Future<Status> SyncFailedBundles(
        const std::unordered_map<std::string, messages::FuncAgentRegisInfo> &agentMap);

    void UpdateBundlesStatus(const std::string &agentID, const resource_view::UnitStatus &status);

    class BundleOperator {
    public:
        BundleOperator(const std::string &nodeId, const std::shared_ptr<MetaStoreClient> &metaStoreClient)
            : nodeID_(nodeId), metaStoreClient_(metaStoreClient)
        {
        }
        ~BundleOperator() = default;

        litebus::Future<Status> UpdateBundles(const std::unordered_map<std::string, messages::BundleInfo> &bundles);
        litebus::Future<std::unordered_map<std::string, messages::BundleInfo>> GetBundles();

    private:
        std::string nodeID_;
        std::shared_ptr<MetaStoreClient> metaStoreClient_;
    };

    void BindScheduler(const std::shared_ptr<schedule_decision::Scheduler> &scheduler)
    {
        scheduler_ = scheduler;
    }

    void BindResourceViewMgr(const std::shared_ptr<resource_view::ResourceViewMgr> &resourceViewMgr)
    {
        resourceViewMgr_ = resourceViewMgr;
    }

    void BindInstanceCtrl(const std::shared_ptr<InstanceCtrl> &instanceCtrl)
    {
        instanceCtrl_ = instanceCtrl;
    }

    void BindLocalSchedSrv(const std::shared_ptr<LocalSchedSrv> &localSchedSrv)
    {
        localSchedSrv_ = localSchedSrv;
    }

    [[maybe_unused]] std::unordered_map<std::string, messages::BundleInfo> GetBundles()
    {
        return bundles_;
    }

protected:
    void Init() override;
    void Finalize() override;

private:
    struct ReservedContext {
        schedule_decision::ScheduleResult result;
        litebus::Timer reserveTimer;
        messages::BundleInfo bundleInfo;
    };

    std::string nodeID_;
    std::shared_ptr<schedule_decision::Scheduler> scheduler_;
    std::shared_ptr<BundleOperator> bundleOperator_;
    std::shared_ptr<InstanceCtrl> instanceCtrl_;
    std::shared_ptr<LocalSchedSrv> localSchedSrv_;
    std::shared_ptr<resource_view::ResourceViewMgr> resourceViewMgr_;
    int32_t reserveToBindTimeoutMs_;
    std::unordered_map<std::string, ReservedContext> reserveResult_;
    std::unordered_map<std::string, messages::BundleInfo> bundles_;
    std::unordered_map<std::string, std::set<std::string>> agentBundles_;  // key: agentID, value: set of bundleIDs
    std::shared_ptr<litebus::Promise<Status>> waitToPersistBundles_ { nullptr };
    std::shared_ptr<litebus::Promise<Status>> persistingBundles_ { nullptr };
    std::shared_ptr<litebus::AID> resourceGroupManagerAID_;

    void SendMsg(const litebus::AID &to, const std::string &name, const std::string &msg);
    bool IsPreCheckPassed(const litebus::AID &from, std::string &&name, std::string &&msg,
                          std::shared_ptr<messages::ScheduleRequest> &req);
    std::shared_ptr<ResourceView> GetResourceView(const std::string &rGroup);
    void TimeoutToBind(const std::shared_ptr<messages::ScheduleRequest> &req);
    litebus::Future<std::shared_ptr<messages::ScheduleResponse>> OnReserve(
        const litebus::Future<schedule_decision::ScheduleResult> &future,
        const std::shared_ptr<messages::ScheduleRequest> &req,
        const std::shared_ptr<messages::ScheduleResponse> &resp);
    litebus::Future<std::shared_ptr<messages::ScheduleResponse>> OnSuccessfulReserve(
        const schedule_decision::ScheduleResult &result,
        const std::shared_ptr<messages::ScheduleRequest> &req,
        const std::shared_ptr<messages::ScheduleResponse> &resp);
    void OnBind(const litebus::AID &to, const litebus::Future<Status> &future,
                const std::shared_ptr<messages::ScheduleRequest> &req,
                const std::shared_ptr<messages::GroupResponse> &resp);
    void OnBindFailed(const litebus::AID &to, const Status &status,
                      const std::shared_ptr<messages::ScheduleRequest> &req,
                      const std::shared_ptr<messages::GroupResponse> &resp);
    void OnUnBind(const litebus::AID &to, const litebus::Future<Status> &future,
                  const std::shared_ptr<messages::ScheduleRequest> &req);
    void HandleRemove(const std::string &rGroupName, const std::string &tenantId);
    void RemoveBundleById(const std::string &bundleId);
    void DoRemoveBundle(const litebus::Future<litebus::Option<ResourceUnit>> &future,
                        const messages::BundleInfo bundleInfo);
    void OnRemoveBundle(const litebus::AID &to, const litebus::Future<Status> &future,
                        const std::shared_ptr<messages::RemoveBundleRequest> &req);
    litebus::Future<Status> PersistBundles();
    void OnPutBundlesInMetaStore(const litebus::Future<Status> &status);

    litebus::Future<Status> CollectResourceChangesForGroupResp(const std::shared_ptr<messages::GroupResponse> &resp);
    litebus::Future<Status> CollectResourceChangesForOnReserves(
        const std::shared_ptr<messages::OnReserves> &resp);
    messages::BundleInfo GenBundle(const std::shared_ptr<messages::ScheduleRequest> &req,
                                   const schedule_decision::ScheduleResult &result);
    resources::InstanceInfo GenInstanceInfo(const messages::BundleInfo &bundleInfo);
    ResourceUnit GenResourceUnit(const messages::BundleInfo &bundleInfo);

    litebus::Future<Status> OnSyncBundle(
        const litebus::Future<std::unordered_map<std::string, messages::BundleInfo>> &future);
    void CompareSynced(const litebus::Future<std::unordered_map<std::string, messages::BundleInfo>> &future);

    void UpdateMasterInfo(const explorer::LeaderInfo &leaderInfo);
    void AddBundle(const messages::BundleInfo &bundle);
    void DeleteBundle(const std::string &bundleID);

    BACK_OFF_RETRY_HELPER(BundleMgrActor, std::shared_ptr<messages::ReportAgentAbnormalResponse>,
                          reportAgentAbnormalHelper_);
};
}  // namespace functionsystem::local_scheduler
#endif  // LOCAL_SCHEDULER_BUNDLE_MANAGER_ACTOR_H
