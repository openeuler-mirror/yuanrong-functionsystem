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

#ifndef FUNCTION_MASTER_INSTANCE_MANAGER_INSTANCE_MGR_ACTOR_H
#define FUNCTION_MASTER_INSTANCE_MANAGER_INSTANCE_MGR_ACTOR_H

#include <string>
#include <unordered_map>

#include "actor/actor.hpp"

#include "common/constants/actor_name.h"
#include "common/leader/business_policy.h"
#include "common/meta_store_adapter/instance_operator.h"
#include "meta_store_client/meta_store_client.h"
#include "common/meta_store_adapter/meta_store_operate_cacher.h"
#include "meta_store_client/meta_store_struct.h"
#include "common/resource_view/resource_type.h"
#include "function_master/global_scheduler/global_sched.h"
#include "group_manager.h"
#include "instance_family_caches.h"

namespace functionsystem::instance_manager {
using InstanceManagerMap = std::unordered_map<std::string, std::shared_ptr<resource_view::InstanceInfo>>;
using InstanceKeyInfoPair = std::pair<std::string, std::shared_ptr<resource_view::InstanceInfo>>;

using GlobalScheduler = functionsystem::global_scheduler::GlobalSched;

struct InstanceManagerStartParam {
    bool runtimeRecoverEnable{ false };
    bool isMetaStoreEnable{ false };
    std::string servicesPath;
    std::string libPath;
    std::string functionMetaPath;
    bool enableAbnormalDoubleCheck;
};

class InstanceManagerActor : public litebus::ActorBase, public std::enable_shared_from_this<InstanceManagerActor> {
public:
    InstanceManagerActor() = delete;

    InstanceManagerActor(const std::shared_ptr<MetaStoreClient> &metaClient,
                         const std::shared_ptr<GlobalScheduler> &scheduler,
                         const std::shared_ptr<GroupManager> &groupManager, const InstanceManagerStartParam &param);

    ~InstanceManagerActor() override = default;

    /**
     * just for unit test, Get instance by node id.
     *
     * @param nodeName node id
     * @param map instance map ptr
     * @return instance map
     */
    InstanceManagerMap *Get(const std::string &nodeName, InstanceManagerMap *map);             // for ut
    Status GetAbnormalScheduler(const std::shared_ptr<std::unordered_set<std::string>> &map);  // for ut

    std::unordered_map<std::string, std::unordered_set<std::string>> GetInstanceJobMap();       // for ut
    std::unordered_map<std::string, std::unordered_set<std::string>> GetInstanceFuncMetaMap();  // for ut

    /**
     * call by global_scheduler when a local_scheduler fault
     *
     * @param nodeName local_schedule's id
     * @return OK success to migrate instances
     */
    litebus::Future<Status> OnLocalSchedFault(const std::string &nodeName);

    bool IsLocalAbnormal(const std::string &nodeName);

    void ForwardKill(const litebus::AID &from, std::string &&name, std::string &&msg);
    void ForwardCustomSignalResponse(const litebus::AID &from, std::string &&name, std::string &&msg);
    void TryCancelResponse(const litebus::AID &from, std::string &&name, std::string &&msg);

    litebus::Future<Status> TryCancelSchedule(const std::string &id, const messages::CancelType &type,
                                              const std::string &reason);

    bool UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo);

    void HandleSystemUpgrade(bool isUpgrading);

    void AddNode(const std::string &nodeName);

    void DelNode(const std::string &nodeName);

    void SetInstancesReady();

    litebus::Future<std::pair<std::string, std::shared_ptr<InstanceInfo>>> GetInstanceInfoByID(
        const std::string &instanceID);

    std::pair<std::string, std::shared_ptr<InstanceInfo>> GetInstanceInfoByInstanceID(const std::string &instanceID)
    {
        if (auto it = member_->instID2Instance.find(instanceID); it != member_->instID2Instance.end()) {
            return it->second;
        }
        return {};
    }

    void OnHealthyStatus(const Status &status);

    // for query instance info request
    void ForwardQueryInstancesInfoHandler(const litebus::AID &from, std::string &&name, std::string &&msg);
    void OnQueryInstancesInfoFinished(const litebus::AID &from,
                                      const litebus::Future<messages::QueryInstancesInfoResponse> &rsp);
    void ForwardQueryInstancesInfoResponseHandler(const litebus::AID &from, std::string &&name, std::string &&msg);
    litebus::Future<messages::QueryInstancesInfoResponse> QueryInstancesInfo(
        std::shared_ptr<messages::QueryInstancesInfoRequest> req);
    litebus::Future<messages::QueryNamedInsResponse> QueryNamedIns(std::shared_ptr<messages::QueryNamedInsRequest> req);
    litebus::Future<messages::QueryDebugInstanceInfosResponse> QueryDebugInstancesInfo(
        std::shared_ptr<messages::QueryDebugInstanceInfosRequest> req);

    void ForwardQueryDebugInstancesInfoHandler(const litebus::AID &from, std::string &&name, std::string &&msg);

    void ForwardQueryDebugInstancesInfoResponseHandler(const litebus::AID &from, std::string &&name, std::string &&msg);

    void OnQueryDebugInstancesInfoFinished(const litebus::AID &from,
                                      const litebus::Future<messages::QueryDebugInstanceInfosResponse> &rsp);

protected:
    void Init() override;
    void Finalize() override;

private:
    /**
     * called when an instance is created or modified.
     *
     * @param key in metastore
     * @param instance will be added
     */
    void OnInstancePut(const std::string &key, const std::shared_ptr<resource_view::InstanceInfo> &instance);

    /**
     * called when an instance is deleted.
     *
     * @param key in metastore
     * @param instance will be deleted
     */
    void OnInstanceDelete(const std::string &key, const std::shared_ptr<resource_view::InstanceInfo> &instance);

    /**
     * called when instance is created, modified or deleted
     *
     * @param events created, modified or deleted event list
     */
    void OnInstanceWatchEvent(const std::vector<WatchEvent> &events, bool synced);

    /**
     * called when debug_instance_kv is created, modified or deleted
     * @param events created, modified or deleted event list
     */
    void OnDebugInstanceWatchEvent(const std::vector<WatchEvent> &events, bool synced);

    /**
     * called when function meta is created, modified or deleted
     *
     * @param events created, modified or deleted event list
     */
    void OnFuncMetaWatchEvent(const std::vector<WatchEvent> &events, bool synced);

    /**
     * Get all instance and cache
     *
     * @param response to get all instance
     */
    void GetAndWatchInstance();
    void GetAndWatchFunctionMeta();
    void OnSyncInstance(const std::shared_ptr<GetResponse> &response);
    Status OnSyncNodes(const std::unordered_set<std::string> &nodes);

    void GetAndWatchDebugInstance();
    void OnSyncDebugInstance(const std::shared_ptr<GetResponse> &response);
    void OnInstanceWatch(const std::shared_ptr<Watcher> &watcher);

    void GetAndWatchAbnormal();
    void OnAbnormalSchedulerWatch(const std::shared_ptr<Watcher> &watcher);
    void OnAbnormalSchedulerWatchEvent(const std::vector<WatchEvent> &events, bool synced);

    bool CheckKillResult(const OperateResult &result, const std::string &instanceID, const std::string &requestID,
                         const litebus::AID &from);

    void EraseAbnormalScheduler(const std::string &nodeName);

    void TryReschedule(const std::string &key, const std::shared_ptr<resource_view::InstanceInfo> &instance,
                       uint32_t retryTimes);

    litebus::Future<Status> KillInstanceWithRetry(const std::string& instanceID,
                                                  const std::shared_ptr<internal::ForwardKillRequest> &killReq);

    void CompleteKillInstance(const litebus::Future<Status> &status, const std::string &requestID,
                              const std::string &instanceID);

    std::shared_ptr<internal::ForwardKillRequest> MakeKillReq(
        const std::shared_ptr<resource_view::InstanceInfo> &instanceInfo, const std::string &srcInstanceID,
        int32_t signal, const std::string &msg) const
    {
        core_service::KillRequest killRequest{};
        killRequest.set_signal(signal);
        killRequest.set_instanceid(instanceInfo->instanceid());
        killRequest.set_payload(msg);

        auto forwardKillRequest = std::make_shared<internal::ForwardKillRequest>();
        auto requestID = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
        killRequest.set_requestid(requestID);
        forwardKillRequest->set_requestid(requestID);
        forwardKillRequest->set_srcinstanceid(srcInstanceID);
        forwardKillRequest->set_instancerequestid(instanceInfo->requestid());
        *forwardKillRequest->mutable_req() = std::move(killRequest);

        return forwardKillRequest;
    }

    litebus::Future<Status> KillInstanceWithLocalAddr(const litebus::Option<std::string> &localAddressOpt,
                                                      const std::shared_ptr<InstanceInfo> &info,
                                                      const std::shared_ptr<internal::ForwardKillRequest> &killReq)
    {
        if (localAddressOpt.IsNone()) {
            return Status(ERR_INNER_SYSTEM_ERROR, fmt::format("failed to get local address({}) of instance({})",
                                                              info->functionproxyid(), info->instanceid()));
        }
        auto localAID =
            litebus::AID(info->functionproxyid() + LOCAL_SCHED_INSTANCE_CTRL_ACTOR_NAME_POSTFIX, localAddressOpt.Get());
        YRLOG_INFO("{}|send instance({}) kill request to local({}), msg({})", killReq->requestid(), info->instanceid(),
                   std::string(localAID), killReq->req().payload());
        (void)Send(localAID, "ForwardCustomSignalRequest", killReq->SerializeAsString());
        return Status::OK();
    }

    void SetKillRetryInterval(int intervalMs)
    {
        member_->retryKillIntervalMs = intervalMs;
    }

    void OnPutAbnormalScheduler(const litebus::Future<std::shared_ptr<PutResponse>> &ret,
                                const std::shared_ptr<litebus::Promise<Status>> &promise,
                                const std::string &nodeName);

    void OnLocalScheduleChange(const std::vector<WatchEvent> &events);
    void OnLocalScheduleWatch(const std::shared_ptr<Watcher> &watcher);

    litebus::Future<SyncResult> InstanceInfoSyncer();
    litebus::Future<SyncResult> FunctionMetaSyncer();
    void DoFunctionMetaSyncer();
    litebus::Future<SyncResult> ProxyAbnormalSyncer(const std::shared_ptr<GetResponse> &getResponse);

    litebus::Future<SyncResult> OnInstanceInfoSyncer(const std::shared_ptr<GetResponse> &getResponse);
    litebus::Future<SyncResult> OnFunctionMetaSyncer(const std::shared_ptr<GetResponse> &getResponse);
    litebus::Future<SyncResult> ReplayFailedInstanceOperation(int64_t revision);
    void ReplayFailedPutOperation(std::list<litebus::Future<Status>> &futures,
                                  std::shared_ptr<std::set<std::string>> erasePutKeys);
    void ReplayFailedDeleteOperation(std::list<litebus::Future<Status>> &futures,
                                     std::shared_ptr<std::set<std::string>> eraseDelKeys);

    // wrapper for lambda function executor which run in instance-manager actor
    Status Execute(std::function<Status()> fn);

    void DoTryCancel(const litebus::Future<litebus::Option<NodeInfo>> &future,
                     const std::shared_ptr<messages::CancelSchedule> &cancelRequest,
                     const std::shared_ptr<litebus::Promise<Status>> &promise);

    void OnKillInstance(const litebus::Future<Status> &status, const messages::ForwardKillRequest &req,
                        const litebus::AID &from);

    void ClearAbnormalSchedulerMetaInfo(const std::string &node);

    void ClearAbnormalScheduler(const std::string &node);

    bool IsInstanceManagedByJob(const std::shared_ptr<InstanceInfo> &info);

private:
    struct Member {
        std::shared_ptr<GlobalScheduler> globalScheduler{ nullptr };
        std::shared_ptr<MetaStoreClient> client{ nullptr };

        // busproxy route info from etcd
        std::unordered_set<std::string> proxyRouteSet;
        std::shared_ptr<Watcher> proxyRouteWatcher{ nullptr };

        std::shared_ptr<InstanceOperator> instanceOpt{ nullptr };
        std::shared_ptr<Watcher> abnormalSchedulerWatcher{ nullptr };
        bool runtimeRecoverEnable{ false };
        std::vector<std::shared_ptr<Watcher>> watchers{ nullptr };
        bool enableAbnormalDoubleCheck_{ false };
        std::shared_ptr<std::unordered_set<std::string>> abnormalScheduler{ nullptr };
        std::unordered_map<std::string, litebus::Timer> abnormalDeferTimer;
        std::unordered_map<std::string, InstanceManagerMap> instances;
        std::unordered_map<std::string, InstanceKeyInfoPair> instID2Instance;
        // key is instanceID
        std::unordered_map<std::string, std::shared_ptr<messages::DebugInstanceInfo>> debugInstInfoMap;
        bool isUpgrading{ false };
        std::shared_ptr<GroupManager> groupManager{ nullptr };
        std::shared_ptr<InstanceFamilyCaches> family{ nullptr };
        std::set<std::string> exitingInstances;
        // instanceID: promise
        std::unordered_map<std::string, std::shared_ptr<litebus::Promise<Status>>> killReqPromises;
        int retryKillIntervalMs = 15000;

        std::unordered_map<std::string, std::unordered_set<std::string>> jobID2InstanceIDs;
        std::unordered_map<std::string, std::unordered_set<std::string>> funcMeta2InstanceIDs;

        std::shared_ptr<MetaStoreOperateCacher> operateCacher = std::make_shared<MetaStoreOperateCacher>();

        bool isMetaStoreEnable{ false };
        explorer::LeaderInfo leaderInfo;

        std::string servicesPath;
        std::string libPath;
        std::string functionMetaPath;
        std::unordered_set<std::string> innerFuncMetaKeys;

        std::shared_ptr<litebus::Promise<messages::QueryInstancesInfoResponse>> queryInstancesPromise;
        std::shared_ptr<litebus::Promise<messages::QueryDebugInstanceInfosResponse>> queryDebugInstancesPromise;
    };

    class Business : public leader::BusinessPolicy {
    public:
        Business(const std::shared_ptr<Member> &member, const std::shared_ptr<InstanceManagerActor> &actor)
            : member_(member), actor_(actor){};
        ~Business() override = default;

        virtual litebus::Future<Status> OnLocalSchedFault(const std::string &nodeName) = 0;
        virtual void OnPutAbnormalScheduler(const litebus::Future<std::shared_ptr<PutResponse>> &ret,
                                            const std::shared_ptr<litebus::Promise<Status>> &promise,
                                            const std::string &nodeName) = 0;
        virtual void DelNode(const std::string &nodeName, bool force) = 0;
        virtual void AddNode(const std::string &nodeName) = 0;
        virtual bool NodeExists(const std::string &nodeName) = 0;
        virtual void OnSyncNodes(const std::unordered_set<std::string> &nodes) = 0;

        virtual bool IsLocalAbnormal(const std::string &nodeName) = 0;
        virtual void OnSyncAbnormalScheduler(const InstanceManagerMap &instances) = 0;

        virtual void OnFaultLocalInstancePut(const std::string &key,
                                             const std::shared_ptr<resource_view::InstanceInfo> &instance,
                                             const std::string &reason) = 0;

        virtual void ForwardKill(const litebus::AID &from, std::string &&name, std::string &&msg) = 0;

        virtual void ForwardCustomSignalResponse(const litebus::AID &from, std::string &&name, std::string &&msg) = 0;

        virtual void TryReschedule(const std::string &key, const std::shared_ptr<resource_view::InstanceInfo> &instance,
                                   uint32_t retryTimes) = 0;

        // there is some different behaviours in master/slave
        virtual void OnInstancePutForFamilyManagement(std::shared_ptr<InstanceInfo> info) = 0;
        virtual void OnInstanceDeleteForFamilyManagement(const std::string &instanceKey,
                                                         const std::shared_ptr<resource_view::InstanceInfo> &info) = 0;

        virtual void OnFuncMetaDelete(const std::string &funcKey) = 0;

        virtual litebus::Future<messages::QueryInstancesInfoResponse> QueryInstancesInfo(
            std::shared_ptr<messages::QueryInstancesInfoRequest> req) = 0;

        virtual litebus::Future<messages::QueryDebugInstanceInfosResponse> QueryDebugInstancesInfo(
            std::shared_ptr<messages::QueryDebugInstanceInfosRequest> req) = 0;

    protected:
        std::shared_ptr<Member> member_;
        std::weak_ptr<InstanceManagerActor> actor_;
    };

    class MasterBusiness : public Business {
    public:
        MasterBusiness(const std::shared_ptr<Member> &member, const std::shared_ptr<InstanceManagerActor> &actor)
            : Business(member, actor)
        {
        }

        ~MasterBusiness() override = default;

        void OnChange() override;

        litebus::Future<Status> OnLocalSchedFault(const std::string &nodeName) override;
        void OnPutAbnormalScheduler(const litebus::Future<std::shared_ptr<PutResponse>> &ret,
                                    const std::shared_ptr<litebus::Promise<Status>> &promise,
                                    const std::string &nodeName) override;
        void DelNode(const std::string &nodeName, bool force) override;
        void AddNode(const std::string &nodeName) override;
        bool NodeExists(const std::string &nodeName) override;
        void ResetNodes();
        void OnSyncNodes(const std::unordered_set<std::string> &nodes) override;

        bool IsLocalAbnormal(const std::string &nodeName) override;

        void OnSyncAbnormalScheduler(const InstanceManagerMap &instances) override;

        void OnFaultLocalInstancePut(const std::string &key,
                                     const std::shared_ptr<resource_view::InstanceInfo> &instance,
                                     const std::string &reason) override;

        void ForwardKill(const litebus::AID &from, std::string &&name, std::string &&msg) override;

        void ForwardCustomSignalResponse(const litebus::AID &from, std::string &&name, std::string &&msg) override;

        void TryReschedule(const std::string &key, const std::shared_ptr<resource_view::InstanceInfo> &instance,
                           uint32_t retryTimes) override;

        void ProcessInstanceOnFaultLocal(const std::string &nodeName, const std::string &reason);

        void ProcessInstanceNotReSchedule(
            const std::pair<const std::string, std::shared_ptr<resource_view::InstanceInfo>> &instance,
            const std::string &nodeName, const std::string &reason);

        void KillAllInstances(const std::list<std::shared_ptr<InstanceInfo>> &allInstances, int32_t signal,
                              const std::string &msg);

        litebus::Future<Status> KillInstance(const std::shared_ptr<InstanceInfo> &info, int32_t signal,
                                             const std::string &msg);

        bool IsInstanceShouldBeKilled(const std::shared_ptr<InstanceInfo>& info);

        bool IsAppDriverFinished(const std::shared_ptr<InstanceInfo>& info);

        void OnInstancePutForFamilyManagement(std::shared_ptr<InstanceInfo> info) override;

        void OnInstanceDeleteForFamilyManagement(const std::string &instanceKey,
                                                 const std::shared_ptr<resource_view::InstanceInfo> &info) override;

        void OnFuncMetaDelete(const std::string &funcKey) override;

        void HandleShutDownAll(const litebus::AID &from, const messages::ForwardKillRequest &forwardKillRequest);

        void ForceDelete(const std::string &key, const std::shared_ptr<resource_view::InstanceInfo> &instance);

        litebus::Future<messages::QueryInstancesInfoResponse> QueryInstancesInfo(
            std::shared_ptr<messages::QueryInstancesInfoRequest> req) override;

        litebus::Future<messages::QueryDebugInstanceInfosResponse> QueryDebugInstancesInfo(
            std::shared_ptr<messages::QueryDebugInstanceInfosRequest> req) override;
    private:
        bool nodeSynced_{false};
        std::unordered_set<std::string> nodes_;
    };

    class SlaveBusiness : public Business {
    public:
        SlaveBusiness(const std::shared_ptr<Member> &member, const std::shared_ptr<InstanceManagerActor> &actor)
            : Business(member, actor){};
        ~SlaveBusiness() override = default;

        void OnChange() override;

        litebus::Future<Status> OnLocalSchedFault(const std::string &) override;
        void OnPutAbnormalScheduler(const litebus::Future<std::shared_ptr<PutResponse>> &,
                                    const std::shared_ptr<litebus::Promise<Status>> &, const std::string &) override;
        void DelNode(const std::string &nodeName, bool force) override {};
        void AddNode(const std::string &nodeName) override {};
        bool NodeExists(const std::string &nodeName) override;
        void OnSyncNodes(const std::unordered_set<std::string> &nodes) override {};

        bool IsLocalAbnormal(const std::string &) override;
        void OnSyncAbnormalScheduler(const InstanceManagerMap &) override;

        void OnFaultLocalInstancePut(const std::string &, const std::shared_ptr<resource_view::InstanceInfo> &,
                                     const std::string &) override;

        void ForwardKill(const litebus::AID &, std::string &&, std::string &&) override;

        void ForwardCustomSignalResponse(const litebus::AID &, std::string &&, std::string &&) override;

        void TryReschedule(const std::string &key, const std::shared_ptr<resource_view::InstanceInfo> &instance,
                           uint32_t retryTimes) override;

        void OnInstancePutForFamilyManagement(std::shared_ptr<InstanceInfo> info) override;

        void OnInstanceDeleteForFamilyManagement(const std::string &instanceKey,
                                                 const std::shared_ptr<resource_view::InstanceInfo> &info) override;

        void OnFuncMetaDelete(const std::string &funcKey) override;

        litebus::Future<messages::QueryInstancesInfoResponse> QueryInstancesInfo(
            std::shared_ptr<messages::QueryInstancesInfoRequest> req) override;

        litebus::Future<messages::QueryDebugInstanceInfosResponse> QueryDebugInstancesInfo(
            std::shared_ptr<messages::QueryDebugInstanceInfosRequest> req) override;
    };

    litebus::Future<Status>  CheckSyncResponse(const std::shared_ptr<GetResponse> &response);
    void CommitSuicide();

    std::shared_ptr<Member> member_{ nullptr };

    std::unordered_map<std::string, std::shared_ptr<Business>> businesses_;

    std::string curStatus_;
    std::shared_ptr<Business> business_{ nullptr };

    int64_t cancelTimout_;
    std::unordered_map<std::string, std::shared_ptr<litebus::Promise<Status>>> cancelPromise_;

    bool isSuicide_{ false };

    litebus::Promise<bool> isInstancesReady_;

    friend class InstanceManagerTest;
};  // class InstanceManagerActor

}  // namespace functionsystem::instance_manager

#endif  // FUNCTION_MASTER_INSTANCE_MANAGER_INSTANCE_MGR_ACTOR_H
