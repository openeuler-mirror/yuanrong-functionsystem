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
#ifndef FUNCTION_MASTER_INSTANCE_MANAGER_GROUP_MANAGER_ACTOR_H
#define FUNCTION_MASTER_INSTANCE_MANAGER_GROUP_MANAGER_ACTOR_H

#include "actor/actor.hpp"
#include "async/defer.hpp"
#include "async/future.hpp"

#include "common/constants/actor_name.h"
#include "common/types/instance_state.h"
#include "common/leader/business_policy.h"
#include "meta_store_client/meta_store_client.h"
#include "common/resource_view/resource_type.h"
#include "common/status/status.h"
#include "common/utils/meta_store_kv_operation.h"
#include "function_master/global_scheduler/global_sched.h"
#include "instance_manager.h"

namespace functionsystem::instance_manager {
// { instanceID => *instanceInfo }
using InstanceKeyInfoMap = std::unordered_map<std::string, std::shared_ptr<resource_view::InstanceInfo>>;
// { groupID => group }
using GroupKeyInfoMap = std::unordered_map<std::string, std::shared_ptr<messages::GroupInfo>>;
using GroupKeyInfoPair = std::pair<std::string, std::shared_ptr<messages::GroupInfo>>;

const int32_t KILLGROUP_TIMEOUT = 60 * 1000;  // s

class GroupManagerActor : public litebus::ActorBase, public std::enable_shared_from_this<GroupManagerActor> {
public:
    GroupManagerActor(const std::shared_ptr<MetaStoreClient> &metaClient,
                      const std::shared_ptr<functionsystem::global_scheduler::GlobalSched> &scheduler)
        : ActorBase(GROUP_MANAGER_ACTOR_NAME)
    {
        member_ = std::make_shared<Member>();
        member_->groupCaches = std::make_shared<GroupCaches>();
        member_->globalScheduler = scheduler;
        member_->metaClient = metaClient;
    }

    void BindInstanceManager(const std::shared_ptr<InstanceManager> &instanceManager)
    {
        ASSERT_IF_NULL(instanceManager);
        member_->instanceManager = instanceManager;
    }

    void UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo)
    {
        litebus::AID masterAID(GROUP_MANAGER_ACTOR_NAME, leaderInfo.address);
        auto newStatus = leader::GetStatus(GetAID(), masterAID, curStatus_);
        if (businesses_.find(newStatus) == businesses_.end()) {
            YRLOG_WARN("new status({}) business don't exist", newStatus);
            return;
        }
        business_ = businesses_[newStatus];
        RETURN_IF_NULL(business_);
        business_->OnChange();
        curStatus_ = newStatus;
    }

    ~GroupManagerActor() override = default;

    void Init() override;

    /// kill all instances (::messages::KillGroupRequest)
    void KillGroup(const litebus::AID &from, std::string &&name, std::string &&msg);

    // clear group response from local
    void OnClearGroup(const litebus::AID &from, std::string &&name, std::string &&msg);

    /// Unimplemented
    void QueryGroupStatus(const litebus::AID &from, std::string &&name, std::string &&msg);

    /// instance abnormal, kill all other instances
    litebus::Future<Status> OnInstanceAbnormal(const std::string &instanceKey,
                                               const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo);

    /// local abnormal, kill all other instances
    litebus::Future<Status> OnLocalAbnormal(const std::string &abnormalLocal);

    /// OnInstancePut
    /// handles the update of instances on normal local, it will do nothing, just record the group informations
    /// instance on abnormal local will be handled by instance manager (set FATAL and recycle / reschedule)
    litebus::Future<Status> OnInstancePut(const std::string &instanceKey,
                                          const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo);

    /// OnInstanceDelete
    /// handles the deletion of instances.
    /// once instance deleted, clear the local cache and do nothing, the recyle job would be done when fatal received.
    litebus::Future<Status> OnInstanceDelete(const std::string &instanceKey,
                                             const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo);

    /// ClearGroupInfo
    /// clear group info in metastore
    litebus::Future<Status> ClearGroupInfo(const std::string &groupID, const Status &status);

    litebus::Future<Status> SendClearGroupToLocal(const litebus::Option<std::string> &proxyAddress,
                                                  const std::string &groupKey,
                                                  const std::shared_ptr<messages::KillGroup> clearReq,
                                                  const std::shared_ptr<litebus::Promise<Status>> &promise);

    void DeleteGroupInfoFromMetaStore(const std::string &groupKey,
                                      const std::shared_ptr<litebus::Promise<Status>> promise);

    /// OnGroupPutCheckParentStatus
    litebus::Future<Status> OnGroupPutCheckParentStatus(
        const std::string &groupKey, const std::shared_ptr<messages::GroupInfo> &groupInfo,
        const std::pair<std::string, std::shared_ptr<resource_view::InstanceInfo>> &parentInfo);

    /// OnGroupPutParentMissing
    litebus::Future<Status> OnGroupPutParentMissing(const std::string &groupKey,
                                                    const std::shared_ptr<messages::GroupInfo> &groupInfo);

    /// OnGroupPutParentFatal
    litebus::Future<Status> OnGroupPutParentFatal(const std::string &groupKey,
                                                  const std::shared_ptr<messages::GroupInfo> &groupInfo);

protected:
    void OnForwardCustomSignalResponse(const litebus::AID &from, std::string &&name, std::string &&msg);

    void FatalAllInstanceOfGroup(const std::string &groupID, const std::string &ignoredInstanceID,
                                 const std::string &errMsg);

    /// kill all instances (::messages::KillGroupRequest)
    litebus::Future<Status> InnerKillInstance(const litebus::Option<std::string> &proxyAddress,
                                              const std::shared_ptr<resource_view::InstanceInfo> &instance,
                                              const std::shared_ptr<internal::ForwardKillRequest> killReq);

    void InnerKillInstanceOnComplete(const litebus::AID &from, const std::string &groupID, const std::string &requestID,
                                     const litebus::Future<Status> &future);
    void WatchGroups();
    void OnGroupWatch(const std::shared_ptr<Watcher> &watcher);
    void OnGroupWatchEvent(const std::vector<WatchEvent> &events);

    void OnGroupPut(const std::string &groupKey, std::shared_ptr<messages::GroupInfo> groupInfo);
    void OnGroupDelete(const std::string &groupKey, const std::shared_ptr<messages::GroupInfo> &groupInfo);

    litebus::Future<SyncResult> GroupInfoSyncer(const std::shared_ptr<GetResponse> &getResponse);

    litebus::Future<Status> OnGetInstanceFromMetaStore(const litebus::Future<std::shared_ptr<GetResponse>> &getResponse,
                                                       const std::string &instanceID, const std::string &groupID);
    litebus::Future<SyncResult> GroupInfoSyncer();
    litebus::Future<SyncResult> OnGroupInfoSyncer(const std::shared_ptr<GetResponse> &getResponse);
    litebus::Future<Status> BroadCastSignalForGroup(const std::string &groupID, const std::string &srcInstanceID,
                                                    const int32_t &signal);
    void OnGroupSuspend(const litebus::Future<Status> &future, const litebus::AID &from, const std::string &groupID,
                        const std::string &requestID);
    void OnGroupResume(const litebus::Future<Status> &future, const litebus::AID &from, const std::string &groupID,
                       const std::string &requestID);

protected:
    class GroupCaches {
    public:
        std::pair<GroupKeyInfoPair, bool> GetGroupInfo(const std::string &groupID);
        GroupKeyInfoMap GetNodeGroups(const std::string &nodeName);
        GroupKeyInfoMap GetChildGroups(const std::string &parentID);
        InstanceKeyInfoMap GetGroupInstances(const std::string &groupID);
        std::string GetGroupOwner(const std::string &groupID);
        std::unordered_map<std::string, GroupKeyInfoPair> GetGroups()
        {
            return groups_;
        }

        virtual void AddGroup(const std::string groupKey, const std::shared_ptr<messages::GroupInfo> &group);
        virtual void RemoveGroup(const std::string &groupID);
        virtual void AddGroupInstance(const std::string &groupID, const std::string &instanceKey,
                                      const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo);

        virtual void RemoveGroupInstance(const std::string &instanceKey,
                                         const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo)
        {
            if (auto it = groupID2Instances_.find(instanceInfo->groupid()); it != groupID2Instances_.end()) {
                it->second.erase(instanceKey);
                if (it->second.empty()) {
                    groupID2Instances_.erase(it);
                }
            }
        }

        virtual std::unordered_map<std::string, GroupKeyInfoPair> GetGroupInfos();
        virtual ~GroupCaches() = default;

    private:
        // { groupID : { groupKey , groupInfo } }
        //  used when instance abnormal, to get belonged group info, and set group to FAILED
        std::unordered_map<std::string, GroupKeyInfoPair> groups_;

        // { nodeName : { groupKey : groupInfo } }
        //  used when local abnormal, to find groups on local, and set group to FAILED
        std::unordered_map<std::string, GroupKeyInfoMap> nodeName2Groups_;

        // { groupID : { instanceKey : intanceInfo } }
        //  used when kill group, to find instances in group
        std::unordered_map<std::string, InstanceKeyInfoMap> groupID2Instances_;

        // { parentInstanceID : { groupID } }
        //  used when kill group, to find instances in group
        std::unordered_map<std::string, GroupKeyInfoMap> parent2Groups_;
    };

    struct Member {
        std::shared_ptr<GroupCaches> groupCaches;
        std::shared_ptr<MetaStoreClient> metaClient{ nullptr };
        std::shared_ptr<Watcher> watcher{ nullptr };
        std::shared_ptr<InstanceManager> instanceManager{ nullptr };
        std::shared_ptr<functionsystem::global_scheduler::GlobalSched> globalScheduler{ nullptr };
        std::unordered_set<std::string> killingGroups;
        std::unordered_map<std::string, std::shared_ptr<litebus::Promise<Status>>> killRspPromises;
    };

protected:
    // For master/slave switching
    class Business : public leader::BusinessPolicy {
    public:
        Business(const std::shared_ptr<Member> &member, const std::shared_ptr<GroupManagerActor> &actor)
            : member_(member), actor_(actor){};
        ~Business() override = default;

        virtual void OnGroupPut(const std::string &groupKey, std::shared_ptr<messages::GroupInfo> groupInfo) = 0;
        virtual void KillGroup(const litebus::AID &from, std::string &&name, std::string &&msg) = 0;
        virtual void SuspendGroup(const litebus::AID &from,
                                  const std::shared_ptr<::messages::KillGroup> &killGroupReq) = 0;
        virtual void ResumeGroup(const litebus::AID &from,
                                 const std::shared_ptr<::messages::KillGroup> &killGroupReq) = 0;
        virtual litebus::Future<Status> InnerKillGroup(const std::string &groupID,
                                                       const std::string &srcInstanceID) = 0;
        virtual litebus::Future<Status> OnInstanceAbnormal(
            const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo) = 0;

        virtual litebus::Future<Status> OnLocalAbnormal(const std::string &abnormalLocal) = 0;
        virtual litebus::Future<Status> FatalGroup(const std::string &groupID, const std::string &ignoredInstanceID,
                                                   const std::string &errMsg) = 0;

        virtual litebus::Future<Status> OnInstancePut(
            const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo) = 0;

        virtual void OnForwardCustomSignalResponse(const litebus::AID &from, std::string &&name, std::string &&msg) = 0;

        virtual litebus::Future<Status> OnInstanceDelete(
            const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo) = 0;
        virtual litebus::Future<Status> BroadCastSignalForGroup(const std::string &groupID,
                                                                const std::string &srcInstanceID,
                                                                const int32_t &signal) = 0;
        virtual litebus::Future<Status> PersistentGroupInfo(const std::string &groupID, const GroupState &state,
                                                            const std::string &description) = 0;

    protected:
        std::shared_ptr<Member> member_;
        std::weak_ptr<GroupManagerActor> actor_;
    };

    class MasterBusiness : public Business {
    public:
        MasterBusiness(const std::shared_ptr<Member> &member, const std::shared_ptr<GroupManagerActor> &actor)
            : Business(member, actor){};
        ~MasterBusiness() override = default;

        void OnChange() override;

        void OnGroupPut(const std::string &groupKey, std::shared_ptr<messages::GroupInfo> groupInfo) override;
        void KillGroup(const litebus::AID &from, std::string &&name, std::string &&msg) override;
        void SuspendGroup(const litebus::AID &from,
                          const std::shared_ptr<::messages::KillGroup> &killGroupReq) override;
        void ResumeGroup(const litebus::AID &from, const std::shared_ptr<::messages::KillGroup> &killGroupReq) override;
        litebus::Future<Status> InnerKillGroup(const std::string &groupID, const std::string &srcInstanceID) override;

        litebus::Future<Status> OnInstanceAbnormal(
            const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo) override;
        litebus::Future<Status> FatalGroup(const std::string &groupID, const std::string &ignoredInstanceID,
                                           const std::string &errMsg) override;

        litebus::Future<Status> OnLocalAbnormal(const std::string &abnormalLocal) override;
        litebus::Future<Status> OnInstancePut(
            const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo) override;
        void OnForwardCustomSignalResponse(const litebus::AID &from, std::string &&name, std::string &&msg) override;

        litebus::Future<Status> OnInstanceDelete(
            const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo) override
        {
            YRLOG_DEBUG("(master)group manager receive instance({}) delete event", instanceInfo->instanceid());
            if (!instanceInfo->groupid().empty()) {
                auto [groupKeyInfo, exists] = member_->groupCaches->GetGroupInfo(instanceInfo->groupid());
                // only running group care about the instance delete event
                if (exists && groupKeyInfo.second->status() == static_cast<int32_t>(GroupState::RUNNING)) {
                    member_->groupCaches->RemoveGroupInstance(instanceKey, instanceInfo);
                    FatalGroup(instanceInfo->groupid(), instanceInfo->instanceid(),
                               fmt::format("group({}) instance({}) is killed separately", instanceInfo->groupid(),
                                           instanceInfo->instanceid()));
                }
                // The group may be cleared in advance. In this case, the instance may receive the exiting event and add
                // it to groupID2Instances_.
                if (!exists) {
                    member_->groupCaches->RemoveGroupInstance(instanceKey, instanceInfo);
                }
            }
            // master also clear the group created by the instance
            return ProcessDeleteInstanceChildrenGroup(instanceKey, instanceInfo);
        }

        litebus::Future<Status> ProcessAbnormalInstanceChildrenGroup(
            const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo);
        litebus::Future<Status> ProcessDeleteInstanceChildrenGroup(
            const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo);
        void CheckGroupInstanceConsistency(std::shared_ptr<messages::GroupInfo> &groupInfo);

        litebus::Future<Status> BroadCastSignalForGroup(const std::string &groupID, const std::string &srcInstanceID,
                                                        const int32_t &signal);

        litebus::Future<Status> PersistentGroupInfo(const std::string &groupID, const GroupState &state,
                                                    const std::string &description);

        litebus::Future<Status> ReScheduleGroup(const std::string &groupID);
    };

    class SlaveBusiness : public Business {
    public:
        SlaveBusiness(const std::shared_ptr<Member> &member, const std::shared_ptr<GroupManagerActor> &actor)
            : Business(member, actor){};
        ~SlaveBusiness() override = default;

        void OnChange() override
        {
        }

        void OnGroupPut(const std::string &groupKey, std::shared_ptr<messages::GroupInfo> groupInfo) override
        {
            member_->groupCaches->AddGroup(groupKey, groupInfo);
        }

        void KillGroup(const litebus::AID &from, std::string &&name, std::string &&msg) override
        {
            // slave do nothing about kill group
            YRLOG_INFO("slave get kill group message");
        }

        void SuspendGroup(const litebus::AID &from, const std::shared_ptr<::messages::KillGroup> &killGroupReq) override
        {
            YRLOG_INFO("slave get suspend group message from {}", from.HashString());
        }

        void ResumeGroup(const litebus::AID &from, const std::shared_ptr<::messages::KillGroup> &killGroupReq) override
        {
            YRLOG_INFO("slave get resume group message from {}", from.HashString());
        }

        litebus::Future<Status> OnInstanceAbnormal(
            const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo) override
        {
            // TBC: maybe cache and process when become master
            YRLOG_INFO("slave get OnInstanceAbnormal event, do nothing, let master do this job");
            return Status::OK();
        }

        litebus::Future<Status> OnLocalAbnormal(const std::string &abnormalLocal) override
        {
            // TBC: maybe cache and process when become master
            YRLOG_INFO("slave get OnLocalAbnormal event");
            return Status::OK();
        }

        litebus::Future<Status> OnInstancePut(const std::string &instanceKey,
                                              const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo) override
        {
            if (instanceInfo->groupid().empty()) {
                return Status::OK();
            }
            YRLOG_DEBUG("slave got inst put {}", instanceKey);
            member_->groupCaches->AddGroupInstance(instanceInfo->groupid(), instanceKey, instanceInfo);
            return Status::OK();
        }

        void OnForwardCustomSignalResponse(const litebus::AID &from, std::string &&name, std::string &&msg) override
        {
            YRLOG_INFO("slave get OnForwardCustomSignalResponse request");
        }

        litebus::Future<Status> OnInstanceDelete(
            const std::string &instanceKey, const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo) override
        {
            YRLOG_DEBUG("(slave)group manager receive instance({}) delete event", instanceInfo->instanceid());
            if (!instanceInfo->groupid().empty()) {
                member_->groupCaches->RemoveGroupInstance(instanceKey, instanceInfo);
            }
            return Status::OK();
        }

        litebus::Future<Status> InnerKillGroup(const std::string &groupID, const std::string &srcInstanceID) override
        {
            return Status::OK();
        }

        litebus::Future<Status> FatalGroup(const std::string &groupID, const std::string &ignoredInstanceID,
                                           const std::string &errMsg) override

        {
            return Status::OK();
        }

        litebus::Future<Status> BroadCastSignalForGroup(const std::string &groupID, const std::string &srcInstanceID,
                                                        const int32_t &signal)
        {
            return Status::OK();
        }

        litebus::Future<Status> PersistentGroupInfo(const std::string &groupID, const GroupState &state,
                                                    const std::string &description)
        {
            return Status::OK();
        }
    };

    std::shared_ptr<Member> member_{ nullptr };
    std::unordered_map<std::string, std::shared_ptr<Business>> businesses_;
    std::string curStatus_;
    std::shared_ptr<Business> business_{ nullptr };
    const uint32_t groupClearTimeout_ = 5000;
    REQUEST_SYNC_HELPER(GroupManagerActor, Status, groupClearTimeout_, requestGroupClearMatch_);

protected:
    // ================= FOR TEST ONLY
    GroupCaches GetCurrentGroupCaches()
    {
        return *member_->groupCaches;
    }
    // ================= FOR TEST ONLY DONE

    void CommitSuicide();
    litebus::Future<Status> CheckSyncResponse(const std::shared_ptr<GetResponse> &response);
    bool isSuicide_{ false };
};
}  // namespace functionsystem::instance_manager
#endif  // FUNCTION_MASTER_INSTANCE_MANAGER_GROUP_MANAGER_ACTOR_H
