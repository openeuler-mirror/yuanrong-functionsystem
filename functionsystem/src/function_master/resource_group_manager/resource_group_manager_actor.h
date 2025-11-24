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

#ifndef FUNCTION_MASTER_RESOURCE_GROUP_MANAGER_ACTOR_H
#define FUNCTION_MASTER_RESOURCE_GROUP_MANAGER_ACTOR_H

#include <string>
#include <unordered_map>

#include "actor/actor.hpp"
#include "async/future.hpp"
#include "common/constants/actor_name.h"
#include "common/explorer/explorer.h"
#include "common/leader/business_policy.h"
#include "meta_store_client/meta_store_client.h"
#include "common/proto/pb/message_pb.h"
#include "function_master/global_scheduler/global_sched.h"

namespace functionsystem::resource_group_manager {
using GlobalScheduler = functionsystem::global_scheduler::GlobalSched;
using ResourceGroupInfos = std::vector<std::shared_ptr<messages::ResourceGroupInfo>>;
using ResourceGroupInfoMap = std::unordered_map<std::string, std::shared_ptr<messages::ResourceGroupInfo>>;

const int DEFAULT_RESCHEDULE_INTERVAL = 3000;

struct BundleIndex {
    std::string tenantID;
    std::string groupName;
    int32_t index;
};

class ResourceGroupManagerActor : public litebus::ActorBase,
                                   public std::enable_shared_from_this<ResourceGroupManagerActor> {
public:
    ResourceGroupManagerActor(const std::shared_ptr<MetaStoreClient> &metaClient,
                               const std::shared_ptr<GlobalScheduler> &scheduler,
                               const std::string &name = RESOURCE_GROUP_MANAGER)
        : litebus::ActorBase(name), groupOperator_(std::make_shared<ResourceGroupOperator>(metaClient))
    {
        member_ = std::make_shared<Member>();
        member_->globalScheduler = scheduler;
    }

    ~ResourceGroupManagerActor() override = default;

    void UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo);
    void ForwardCreateResourceGroup(const litebus::AID &from, std::string &&name, std::string &&msg);
    void ForwardDeleteResourceGroup(const litebus::AID &from, std::string &&name, std::string &&msg);
    void ForwardReportUnitAbnormal(const litebus::AID &from, std::string &&name, std::string &&msg);
    void OnRemoveBundle(const litebus::AID &from, std::string &&name, std::string &&msg);
    litebus::Future<Status> OnLocalAbnormal(const std::string &abnormalLocal);
    litebus::Future<messages::QueryResourceGroupResponse> QueryResourceGroup(
        const std::shared_ptr<messages::QueryResourceGroupRequest> req);

protected:
    void Init() override;
    litebus::Future<Status> Sync();
    litebus::Future<Status> OnSyncResourceGroups(
        const std::shared_ptr<GetResponse> &getResponse,
        const std::unordered_map<std::string, std::unordered_set<std::string>> &oldMap);

    void AddResourceGroupInfo(const std::shared_ptr<messages::ResourceGroupInfo> &req);
    void DeleteResourceGroupInfo(const std::shared_ptr<messages::ResourceGroupInfo> &req);
    const std::shared_ptr<messages::ResourceGroupInfo> GetResourceGroupInfo(const std::string &name,
                                                                              const std::string &tenantID);
    void AddBundleInfo(const messages::BundleInfo &bundle, const int32_t index);
    void DeleteBundleInfo(const messages::BundleInfo &bundle);
    std::shared_ptr<BundleIndex> GetBundleIndex(const std::string &bundleID);

    litebus::Future<Status> HandleLocalAbnormal(const std::string &abnormalLocal);
    void HandleForwardReportUnitAbnormal(const litebus::AID &from,
                                         const std::shared_ptr<messages::ReportAgentAbnormalRequest> request);
    void HandleForwardCreateResourceGroup(const litebus::AID &from,
                                           const std::shared_ptr<core_service::CreateResourceGroupRequest> request);
    void HandleForwardDeleteResourceGroup(const litebus::AID &from,
                                           const std::shared_ptr<inner_service::ForwardKillRequest> request);
    void ScheduleResourceGroup(
        const std::shared_ptr<litebus::Promise<core_service::CreateResourceGroupResponse>> &promise,
        const std::string &requestID, const std::string &name, const std::string &tenantID,
        const std::shared_ptr<messages::GroupInfo> &groupInfo);
    litebus::Future<messages::GroupResponse> ForwardGroupSchedule(
        const std::shared_ptr<messages::GroupInfo> &groupInfo);
    litebus::Future<Status> ForwardGroupScheduleDone(
        const messages::GroupResponse &groupRsp, const std::string &requestID, const std::string &name,
        const std::string &tenantID,
        const std::shared_ptr<litebus::Promise<core_service::CreateResourceGroupResponse>> &promise);

    void DeleteResourceGroupPreCheck(std::shared_ptr<messages::ResourceGroupInfo> resourceGroupInfo,
        const litebus::AID &from, const std::shared_ptr<inner_service::ForwardKillRequest> request);
    void DoDeleteResourceGroup(std::shared_ptr<messages::ResourceGroupInfo> resourceGroupInfo, const litebus::AID &from,
                                const std::shared_ptr<inner_service::ForwardKillRequest> request);
    litebus::Future<Status> SendDeleteResourceGroupResponse(const messages::ForwardKillResponse &response,
                                                             const litebus::AID &from);
    litebus::Future<Status> RemoveAllBundles(const std::unordered_set<std::string> proxyIDs,
                                             const std::string &tenantID, const std::string &rgName);
    void RemoveBundle(const std::shared_ptr<messages::RemoveBundleRequest> request, const std::string &nodeID);
    litebus::Future<Status> RemoveBundleWithLocal(const litebus::Option<std::string> &localAddressOpt,
                                                  const std::shared_ptr<messages::RemoveBundleRequest> request);
    litebus::Future<Status> OnRemoveAllBundles(const Status &status,
                                               const std::shared_ptr<messages::ResourceGroupInfo> &resourceGroupInfo,
                                               const litebus::AID &from,
                                               const std::shared_ptr<inner_service::ForwardKillRequest> request);
    litebus::Future<Status> OnDeleteResourceGroupFromMetaStore(
        const Status &status, const std::shared_ptr<messages::ResourceGroupInfo> &resourceGroupInfo,
        const litebus::AID &from, const std::shared_ptr<inner_service::ForwardKillRequest> request);

    litebus::Future<Status> SendCreateResourceGroupResponse(const core_service::CreateResourceGroupResponse &response,
                                                             const litebus::AID &from);
    litebus::Future<Status> SendDeleteResourceGroupResponse(const inner_service::ForwardKillResponse &response,
                                                             const litebus::AID &from);
    litebus::Future<Status> PersistenceAllGroups(
        const std::unordered_map<std::string, std::shared_ptr<messages::ResourceGroupInfo>> resourceGroupInfos);
    litebus::Future<Status> OnPersistenceAllGroups(
        const Status &status, const litebus::AID &from,
        const std::unordered_map<std::string, std::shared_ptr<messages::ResourceGroupInfo>> resourceGroupInfos,
        const std::shared_ptr<messages::ReportAgentAbnormalRequest> request);
    void RescheduleResourceGroup(const std::string &tenantID, const std::string &rgName);
    litebus::Future<Status> OnRescheduleResourceGroup(
        const litebus::Future<std::list<messages::GroupResponse>> &future, const std::string &tenantID,
        const std::string &rgName);
    void TransCreateResourceGroupReq(std::shared_ptr<CreateResourceGroupRequest> &req);

private:
    class ResourceGroupOperator {
    public:
        explicit ResourceGroupOperator(const std::shared_ptr<MetaStoreClient> &metaStoreClient)
            : metaStoreClient_(metaStoreClient)
        {
        }
        ~ResourceGroupOperator() = default;
        litebus::Future<std::shared_ptr<GetResponse>> SyncResourceGroups();
        litebus::Future<Status> TxnResourceGroup(const std::shared_ptr<messages::ResourceGroupInfo> &req);
        litebus::Future<Status> DeleteResourceGroup(const std::shared_ptr<messages::ResourceGroupInfo> &req);

    private:
        std::shared_ptr<MetaStoreClient> metaStoreClient_;
    };

    struct Member {
        explorer::LeaderInfo leaderInfo;
        std::shared_ptr<GlobalScheduler> globalScheduler{ nullptr };
        // tenantID --> groupInfoMap
        std::unordered_map<std::string, ResourceGroupInfoMap> resourceGroups;
        // bundleID --> bundleInfo
        std::unordered_map<std::string, std::shared_ptr<BundleIndex>> bundleInfos;
        // proxyID --> bundleIDs
        std::unordered_map<std::string, std::unordered_set<std::string>> proxyID2BundleIDs;
        std::unordered_map<std::string, std::pair<std::shared_ptr<inner_service::ForwardKillRequest>, litebus::AID>>
            toDeleteResourceGroups;

        std::unordered_set<std::string> createRequests;
        std::unordered_map<std::string, std::shared_ptr<litebus::Promise<Status>>> removeReqPromises;
        std::shared_ptr<litebus::Promise<messages::QueryResourceGroupResponse>> queryResourceGroupPromise;
    };

    class Business : public leader::BusinessPolicy {
    public:
        Business(const std::shared_ptr<Member> &member, const std::shared_ptr<ResourceGroupManagerActor> &actor)
            : member_(member), actor_(actor){};
        ~Business() override = default;

        virtual litebus::Future<Status> OnLocalAbnormal(const std::string &abnormalLocal) = 0;
        virtual void ForwardCreateResourceGroup(
            const litebus::AID &from, const std::shared_ptr<core_service::CreateResourceGroupRequest> request) = 0;
        virtual void ForwardDeleteResourceGroup(const litebus::AID &from,
                                                 const std::shared_ptr<inner_service::ForwardKillRequest> request) = 0;
        virtual void ForwardReportUnitAbnormal(const litebus::AID &from,
                                               const std::shared_ptr<messages::ReportAgentAbnormalRequest> request) = 0;
        virtual litebus::Future<messages::QueryResourceGroupResponse> QueryResourceGroup(
            const std::shared_ptr<messages::QueryResourceGroupRequest> req) = 0;

    protected:
        std::shared_ptr<Member> member_;
        std::weak_ptr<ResourceGroupManagerActor> actor_;
    };

    class MasterBusiness : public Business {
    public:
        MasterBusiness(const std::shared_ptr<Member> &member, const std::shared_ptr<ResourceGroupManagerActor> &actor)
            : Business(member, actor){};
        ~MasterBusiness() override = default;

        void OnChange() override;
        litebus::Future<Status> OnLocalAbnormal(const std::string &abnormalLocal) override;
        void ForwardCreateResourceGroup(
            const litebus::AID &from,
            const std::shared_ptr<core_service::CreateResourceGroupRequest> request) override;
        void ForwardDeleteResourceGroup(const litebus::AID &from,
                                         const std::shared_ptr<inner_service::ForwardKillRequest> request) override;
        void ForwardReportUnitAbnormal(const litebus::AID &from,
                                       const std::shared_ptr<messages::ReportAgentAbnormalRequest> request) override;
        litebus::Future<messages::QueryResourceGroupResponse> QueryResourceGroup(
            const std::shared_ptr<messages::QueryResourceGroupRequest> req) override;
    };

    class SlaveBusiness : public Business {
    public:
        SlaveBusiness(const std::shared_ptr<Member> &member, const std::shared_ptr<ResourceGroupManagerActor> &actor)
            : Business(member, actor){};
        ~SlaveBusiness() override = default;

        void OnChange() override;
        void ForwardCreateResourceGroup(
            const litebus::AID &from,
            const std::shared_ptr<core_service::CreateResourceGroupRequest> request) override;
        void ForwardDeleteResourceGroup(const litebus::AID &from,
                                         const std::shared_ptr<inner_service::ForwardKillRequest> request) override;
        void ForwardReportUnitAbnormal(const litebus::AID &from,
                                       const std::shared_ptr<messages::ReportAgentAbnormalRequest> request) override;
        litebus::Future<Status> OnLocalAbnormal(const std::string &abnormalLocal) override;
        litebus::Future<messages::QueryResourceGroupResponse> QueryResourceGroup(
            const std::shared_ptr<messages::QueryResourceGroupRequest> req) override;
    };

    void ForwardQueryResourceGroupHandler(const litebus::AID &from, std::string &&name, std::string &&msg);
    void ForwardQueryResourceGroupResponseHandler(const litebus::AID &from, std::string &&name, std::string &&msg);
    void OnHandleForwardQueryResourceGroup(const litebus::AID &from,
                                           const litebus::Future<messages::QueryResourceGroupResponse> &rsp);

    std::shared_ptr<Member> member_{ nullptr };
    std::unordered_map<std::string, std::shared_ptr<Business>> businesses_;
    std::string curStatus_;
    std::shared_ptr<Business> business_{ nullptr };
    std::shared_ptr<ResourceGroupOperator> groupOperator_;
    int32_t defaultRescheduleInterval_{ DEFAULT_RESCHEDULE_INTERVAL };
};
}  // namespace functionsystem::resource_group_manager
#endif  // FUNCTION_MASTER_RESOURCE_GROUP_MANAGER_ACTOR_H
