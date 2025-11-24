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

#ifndef FUNCTION_MASTER_GLOBAL_SCHEDULER_GLOBAL_SCHEDULER_ACTOR_H
#define FUNCTION_MASTER_GLOBAL_SCHEDULER_GLOBAL_SCHEDULER_ACTOR_H

#include <async/async.hpp>
#include <deque>
#include <functional>
#include <queue>

#include "common/explorer/explorer.h"
#include "common/leader/business_policy.h"
#include "meta_store_client/meta_store_client.h"
#include "common/proto/pb/message_pb.h"
#include "common/scheduler_topology/tree.h"
#include "common/status/status.h"
#include "domain_activator.h"
#include "scheduler_manager/domain_sched_mgr.h"
#include "scheduler_manager/local_sched_mgr.h"

namespace functionsystem::global_scheduler {
using LocalSchedAbnormalCallbackFunc = std::function<litebus::Future<Status>(const std::string &name)>;

using CheckLocalAbnormalCallbackFunc = std::function<litebus::Future<bool>(const std::string &name)>;
using LocalDeleteCallbackFunc = std::function<void(const std::string &name)>;
using LocalAddCallbackFunc = std::function<void(const std::string &name)>;

enum class LocalExitType {
    ABNORMAL = 0,
    UNREGISTER = 1,
};

class GlobalSchedActor : public litebus::ActorBase, public std::enable_shared_from_this<GlobalSchedActor> {
public:
    GlobalSchedActor(const std::string &name, std::shared_ptr<MetaStoreClient> metaStoreClient,
                     std::shared_ptr<DomainActivator> domainActivator, std::unique_ptr<Tree> &&topologyTree);

    ~GlobalSchedActor() override = default;

    void ResponseUpdateTaint(const litebus::AID &from, std::string &&name, std::string &&msg);

    void UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo);

    void BindDomainSchedMgr(const std::shared_ptr<DomainSchedMgr> &domainSchedMgr);

    void BindLocalSchedMgr(const std::shared_ptr<LocalSchedMgr> &localSchedMgr);
    /**
     * Handle adding local scheduler to the topology tree.
     * @param from The AID of the actor of local scheduler.
     * @param name The name identifier of the local scheduler.
     * @param address The IP and port of the local scheduler.
     */
    void AddLocalSchedHandler(const litebus::AID &from, const std::string &name, const std::string &address);

    /**
     * Handle deleting local scheduler to the topology tree asynchronously.
     * @param name The name identifier of the local scheduler.
     */
    void DelLocalSchedHandler(const std::string &name, LocalExitType exitType = LocalExitType::ABNORMAL);

    /**
     * Handle adding domain scheduler to the topology tree.
     * @param from The AID of the actor of the domain scheduler.
     * @param name The name identifier of the domain scheduler.
     * @param address The IP and port the domain scheduler.
     */
    void AddDomainSchedHandler(const litebus::AID &from, const std::string &name, const std::string &address);

    /**
     * Handle deleting domain scheduler to the topology tree asynchronously.
     * @param name The name identifier of the domain scheduler.
     */
    void DelDomainSchedHandler(const std::string &name);

    void UpdateNodeTaintsHandler(const std::string &ip, const std::string &key, const bool healthy);

    /**
     * Add the LocalScheduler node to the topology tree. This method needs to be called asynchronously by using
     * litebus::Async. And the topology info will be saved into MetaStore.
     * @param name The host name of the local scheduler.
     * @param address The IP and port of the local scheduler.
     * @return ScheduleTopology contains the IP address of the upstream domain scheduler of the local scheduler to be
     * added. Return None if failed.
     */
    virtual Node::TreeNode AddLocalSched(const std::string &name, const std::string &address);

    virtual Node::TreeNode DelLocalSched(const std::string &name);

    virtual Node::TreeNode AddDomainSched(const std::string &name, const std::string &address);

    virtual Node::TreeNode DelDomainSched(const std::string &name);

    virtual Status CacheLocalSched(const litebus::AID &from, const std::string &name, const std::string &address);

    /**
     * Reconstruct the scheduler tree.
     * @return Status.
     */
    virtual Status RecoverSchedTopology();

    void SetTopoRecovered(bool isRecovered);

    virtual bool DomainHasActivated();

    virtual Node::TreeNode FindRootDomainSched();

    virtual std::unordered_map<std::string, Node::TreeNode> FindNodes(const uint64_t level);

    virtual litebus::Option<std::string> GetLocalAddress(const std::string &name);

    virtual litebus::Option<NodeInfo> GetRootDomainInfo();

    virtual litebus::Future<std::unordered_set<std::string>> QueryNodes();

    virtual litebus::Future<Status> UpdateSchedTopology();

    void BindLocalSchedAbnormalCallback(const LocalSchedAbnormalCallbackFunc &func);

    void BindCheckLocalAbnormalCallback(const CheckLocalAbnormalCallbackFunc &func);

    void BindLocalDeleteCallback(const LocalDeleteCallbackFunc &func);

    void BindLocalAddCallback(const LocalAddCallbackFunc &func);

    void AddLocalSchedAbnormalNotifyCallback(const std::string &name, const LocalSchedAbnormalCallbackFunc &func);

    litebus::Future<Status> DoSchedule(const std::shared_ptr<messages::ScheduleRequest> &req);
    litebus::Future<messages::GroupResponse> GroupSchedule(const std::shared_ptr<messages::GroupInfo> &groupInfo,
                                                           uint32_t retryCycle);

    litebus::Future<messages::QueryAgentInfoResponse> QueryAgentInfo(
        const std::shared_ptr<messages::QueryAgentInfoRequest> &req);

    litebus::Future<messages::QueryInstancesInfoResponse> GetSchedulingQueue(
        const std::shared_ptr<messages::QueryInstancesInfoRequest> &req);

    litebus::Future<Status> EvictAgent(const std::string &localID,
                                       const std::shared_ptr<messages::EvictAgentRequest> &req);

    litebus::Future<messages::QueryResourcesInfoResponse> HandleQueryResourcesInfo(
        const std::shared_ptr<messages::QueryResourcesInfoRequest> &req);

    void OnHealthyStatus(const Status &status);

    // for test
    [[maybe_unused]] void ResponseResourcesInfo(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        ProcessResourcesInfo(from, std::move(name), std::move(msg));
    }
protected:
    void Init() override;
    void Finalize() override;
private:
    struct Member {
        std::shared_ptr<DomainSchedMgr> domainSchedMgr = nullptr;
        std::unique_ptr<Tree> topologyTree = nullptr;
        litebus::AID scalerAID;
    };
    class Business : public leader::BusinessPolicy {
    public:
        Business(const std::shared_ptr<GlobalSchedActor> &actor, const std::shared_ptr<Member> &member)
            : actor_(actor), member_{ member }
        {
        }
        ~Business() override = default;
        virtual Node::TreeNode FindRootDomainSched() = 0;
        virtual void ResponseUpdateTaint(const litebus::AID &from, std::string &&name, std::string &&msg) = 0;
        virtual void OnHealthyStatus(const Status &status) = 0;
        virtual litebus::Future<messages::QueryResourcesInfoResponse> QueryResourcesInfo(
            const std::shared_ptr<messages::QueryResourcesInfoRequest> &req) = 0;
        virtual void HandleResourceInfoResponse(const messages::QueryResourcesInfoResponse &rsp) = 0;
        virtual void UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo) = 0;

    protected:
        std::weak_ptr<GlobalSchedActor> actor_;
        std::shared_ptr<Member> member_;
        explorer::LeaderInfo leaderInfo_;
    };

    class MasterBusiness : public Business {
    public:
        MasterBusiness(const std::shared_ptr<GlobalSchedActor> &actor, const std::shared_ptr<Member> &member)
            : Business(actor, member)
        {
        }
        ~MasterBusiness() override = default;
        void OnChange() override;
        Node::TreeNode FindRootDomainSched() override;
        void ResponseUpdateTaint(const litebus::AID &from, std::string &&name, std::string &&msg) override;
        void OnHealthyStatus(const Status &status) override;
        litebus::Future<messages::QueryResourcesInfoResponse> QueryResourcesInfo(
            const std::shared_ptr<messages::QueryResourcesInfoRequest> &req) override;
        void HandleResourceInfoResponse(const messages::QueryResourcesInfoResponse &rsp) override;
        void UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo) override;
    };

    class SlaveBusiness : public Business {
    public:
        SlaveBusiness(const std::shared_ptr<GlobalSchedActor> &actor, const std::shared_ptr<Member> &member)
            : Business(actor, member)
        {
        }
        ~SlaveBusiness() override = default;
        void OnChange() override;
        Node::TreeNode FindRootDomainSched() override;
        void ResponseUpdateTaint(const litebus::AID &, std::string &&, std::string &&) override;
        void OnHealthyStatus(const Status &status) override
        {
        }
        litebus::Future<messages::QueryResourcesInfoResponse> QueryResourcesInfo(
            const std::shared_ptr<messages::QueryResourcesInfoRequest> &req) override;
        void HandleResourceInfoResponse(const messages::QueryResourcesInfoResponse &rsp) override;
        void UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo) override;

    private:
        std::shared_ptr<litebus::Promise<messages::QueryResourcesInfoResponse>> queryResourcesInfoPromise_ = nullptr;
    };

    Status RecoverFromString(const std::string &topologyInfo);
    void UpdateLastUpdatedTopology(const std::string &topologyInfo);
    void AddLocalSched(const litebus::Future<bool> &isLocalAbnormal, const litebus::AID &from, const std::string &name,
                       const std::string &address);
    void PutTopology();
    void OnTopologyPut(const litebus::Future<Status> &future);
    void OnTopologyEvent(const std::vector<WatchEvent> &events);

    void QueryResourcesInfo(const litebus::AID &from, std::string &&name, std::string &&msg);

    virtual void CheckMasterStatus(const litebus::AID &from, std::string &&name, std::string &&msg);

    void OnQueryResourcesInfo(const litebus::Future<messages::QueryResourcesInfoResponse> &future,
                              const litebus::AID &to);

    void ProcessResourcesInfo(const litebus::AID &from, std::string &&name, std::string &&msg);

    void OnLocalExit(const std::string &name);
    void OnLocalAbnormal(const std::string &name);

    void DoGroupSchedule(const std::shared_ptr<messages::GroupInfo> &groupInfo,
                         const std::shared_ptr<litebus::Promise<messages::GroupResponse>> &promise,
                         uint32_t retryCycle);

    std::shared_ptr<MetaStoreClient> metaStoreClient_;
    std::shared_ptr<DomainActivator> domainActivator_;
    std::string lastUpdatedTopology_;
    std::deque<std::pair<std::string, NodeInfo>> cachedLocalSched_;
    std::deque<std::string> abnormalDomainSched_;
    uint32_t waitDomainToRegister_ = 0;
    bool hasActivateDomain_ = false;

    std::shared_ptr<LocalSchedMgr> localSchedMgr_ = nullptr;
    LocalSchedAbnormalCallbackFunc localSchedAbnormalCallback_;
    CheckLocalAbnormalCallbackFunc checkLocalAbnormalCallbackFunc_;
    LocalDeleteCallbackFunc localDeleteCallback_;
    LocalAddCallbackFunc localAddCallback_;
    std::unordered_map<std::string, LocalSchedAbnormalCallbackFunc> localSchedAbnormalNotifyCallbacks_{};

    std::shared_ptr<Member> member_;

    std::unordered_map<std::string, std::shared_ptr<Business>> businesses_;

    std::string curStatus_;
    std::shared_ptr<Business> business_{ nullptr };

    std::shared_ptr<litebus::Promise<Status>> waitToPersistence_;
    std::shared_ptr<litebus::Promise<Status>> persisting_;
    litebus::Promise<bool> topoRecovered;
    std::string cacheTopo_;
};

}  // namespace functionsystem::global_scheduler

#endif  // FUNCTION_MASTER_GLOBAL_SCHEDULER_GLOBAL_SCHEDULER_ACTOR_H
