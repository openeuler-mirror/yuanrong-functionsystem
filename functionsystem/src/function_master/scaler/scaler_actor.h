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

#ifndef FUNCTION_MASTER_SCALER_ACTOR_H
#define FUNCTION_MASTER_SCALER_ACTOR_H

#include <cctype>
#include <string>
#include <unordered_set>
#include <vector>

#include "actor/actor.hpp"

#include "common/constants/constants.h"
#include "function_master/scaler/constants.h"
#include "common/explorer/explorer.h"
#include "common/leader/business_policy.h"
#include "meta_storage_accessor/meta_storage_accessor.h"
#include "common/proto/pb/message_pb.h"
#include "common/utils/meta_store_kv_operation.h"
#include "function_master/common/flags/flags.h"
#include "function_master/global_scheduler/global_sched.h"
#include "common/kube_client/api/apps_v1_api.h"
#include "common/kube_client/api/core_v1_api.h"
#include "common/kube_client/kube_client.h"
#include "common/kube_client/model/deployment/v1_deployment.h"
#include "function_master/scaler/pool/pool_manager.h"
#include "function_master/scaler/utils/system_upgrade_switch_utils.h"
#include "utils/parse_helper.h"
#include "function_master/scaler/system_function_pod_manager/system_function_pod_manager.h"

namespace functionsystem::scaler {
const std::string DEFAULT_NAMESPACE = "default";
const std::string CUSTOM_POD_PREFIX = "custom-";

const std::string SCHEDULER_CPU_RESOURCE = "CPU";
const std::string SCHEDULER_MEMORY_RESOURCE = "Memory";

const uint32_t POD_STATUS_CHECK_INTERVAL_MS = 15000;
const uint32_t DEFAULT_POD_PENDING_DURATION_ALARM_THRESHOLD = 120;

using ApiClient = kube_client::api::ApiClient;
using SslConfig = kube_client::api::SslConfig;
using V1Deployment = kube_client::model::V1Deployment;
using V1DeploymentList = kube_client::model::V1DeploymentList;
using V1PodTemplateSpec = kube_client::model::V1PodTemplateSpec;
using V1Pod = kube_client::model::V1Pod;
using Object = kube_client::model::Object;
using V1Affinity = kube_client::model::V1Affinity;
using V1Toleration = kube_client::model::V1Toleration;

using V1Taint = kube_client::model::V1Taint;
using ModelBase = kube_client::model::ModelBase;
using V1SeccompProfile = kube_client::model::V1SeccompProfile;

const int64_t DEFAULT_GRACE_PERIOD_SECONDS = 25;  // s


struct ScalerParams {
    std::string k8sNamespace{ DEFAULT_NAMESPACE };
    uint32_t gracePeriodSeconds{ DEFAULT_GRACE_PERIOD_SECONDS };
    SystemUpgradeParam systemUpgradeParam{};
    std::string clusterId = "";
    std::string poolConfigPath{ "" };
    std::string agentTemplatePath{ "" };
    bool enableFrontendPool{ false };
};

struct UpdateLabelReq {
    std::shared_ptr<std::pair<resource_view::InstanceInfo, bool>> waitingReq{ nullptr };
    std::shared_ptr<std::pair<resource_view::InstanceInfo, bool>> patchingReq{ nullptr };
};

enum MigrateType : int32_t { NOT_MIGRATE, MIGRATE_ALL, MIGRATE_UNIQUE, MIGRATE_GRACEFUL, MIGRATE_EVICTED};

enum LocalStatusCode : int32_t { SUCCESS = 1, EVICTED = 4 };

class ScalerActor : public litebus::ActorBase,
                    public std::enable_shared_from_this<ScalerActor> {
public:
    explicit ScalerActor(const std::string &name);

    ScalerActor(const std::string &name, const std::shared_ptr<KubeClient> &kubeClient,
                const std::shared_ptr<MetaStorageAccessor> &metaStorageAccessor, const ScalerParams &params = {});

    ~ScalerActor() override = default;

    void UpdateLeaderInfo(const explorer::LeaderInfo &leaderInfo);

    virtual Status Start();

    litebus::Future<Status> SyncNodes();

    virtual litebus::Future<Status> SyncPodAndInstance();

    virtual litebus::Future<Status> SyncDeploymentAndPodPool();

    litebus::Future<Status> SyncPodPoolInfo(const Status &status);

    [[nodiscard]] Status CreateResourcePools(const std::shared_ptr<std::vector<ResourcePool>> &pools);

    Status CreateDeployment(const litebus::Future<std::shared_ptr<V1DeploymentList>> &future,
                            const std::shared_ptr<std::vector<ResourcePool>> &pools);

    static bool IsSameDeploymentConfig(const ResourcePool &pool, const std::shared_ptr<V1Deployment> &deployment);

    std::shared_ptr<V1Deployment> GenerateDeploymentByTemplate(const ResourcePool &pool,
                                                               const std::shared_ptr<V1Deployment> &templateDeployment);

    static std::shared_ptr<V1Deployment> GenerateDeploymentByOrigin(const ResourcePool &pool,
                                                                    std::shared_ptr<V1Deployment> templateDeployment);

    void CheckExistingDeployment(const std::shared_ptr<V1Deployment> &deployment, const ResourcePool &pool);

    static void SetResourceEnvs(const std::shared_ptr<V1Container> &container, const ResourcePool &pool);

    void OnCreateDeploymentComplete(const litebus::Future<std::shared_ptr<V1Deployment>> &retDeployment,
                                    const std::string &poolName);

    void OnCreatePodPoolComplete(const litebus::Future<std::shared_ptr<V1Deployment>> &retDeployment,
                                 const std::shared_ptr<PodPoolInfo> &poolInfo);

    void OnCreatePodComplete(const litebus::Future<std::shared_ptr<V1Pod>> &retPod,
                             const std::shared_ptr<messages::CreateAgentRequest> &createAgentRequest,
                             const litebus::AID &from, bool needWaitPodReady);

    void OnScaleUpPodComplete(const litebus::Future<std::shared_ptr<V1Pod>> &retPod,
                                 const std::shared_ptr<V1Pod> &srcPod, const std::string &poolID,
                                 const std::string &requestID, const litebus::AID &from);

    void OnPatchDeploymentComplete(const litebus::Future<std::shared_ptr<V1Deployment>> &retDeployment,
                                   const std::string &poolName);

    Status Register();

    void UpdateInstanceEvent(const std::vector<WatchEvent> &events);

    void UpdatePodPoolEvent(const std::vector<WatchEvent> &events);

    void UpdateSystemUpgradeSwitchEvent(const std::vector<WatchEvent> &events);

    void HandleSystemUpgrade(std::shared_ptr<UpgradeInfo> upgradeInfo);

    void OnSystemUpgradeFinished();

    virtual void HandleInstancePut(const resource_view::InstanceInfo &instanceInfo);

    void HandleSharedInstancePut(const resource_view::InstanceInfo &instanceInfo);

    void HandleInstanceDelete(const resource_view::InstanceInfo &instanceInfo);

    void HandleSharedInstanceDelete(const resource_view::InstanceInfo &instanceInfo);

    void UpdatePodLabels(const litebus::Future<std::shared_ptr<V1Pod>> &pod,
                         const resource_view::InstanceInfo &instanceInfo, bool isInstanceDelete = false);

    virtual void PatchPod(const std::string &podName, const std::string &nameSpace,
                          const std::shared_ptr<V1Pod> &source, const std::shared_ptr<V1Pod> &target,
                          const std::string &instanceID);

    virtual void OnPodModified(const K8sEventType &type, const std::shared_ptr<ModelBase> &model);

    bool CheckPodNeedDelete(const std::shared_ptr<V1Pod> &pod);

    void OnNodeModified(const K8sEventType &type, const std::shared_ptr<ModelBase> &model);

    void ProcessNodeFault(const std::string &nodeName, const MigrateType type);

    void OnDeploymentModified(const K8sEventType &type, const std::shared_ptr<ModelBase> &model);

    static void UpdateContainerResource(const std::shared_ptr<V1Container> &container, const ResourcePool &pool);

    /**
     * request to create a function-agent deployment from domain scheduler to scaler
     * @param from: interface caller's AID
     * @param name: function name
     * @param msg: request data, type is messages::CreateAgentRequest
     */
    virtual void CreateAgent(const litebus::AID &from, std::string &&name, std::string &&msg);

    void PersistencePoolInfo(const std::string &poolID);

    void DoScaleUpPodByPoolID(const std::string &poolID, bool isReserved, const std::string &requestID,
                            const litebus::AID &from);

    void ScaleUpPodByPoolID(const std::string &poolID, bool isReserved, const std::string &requestID,
                            const litebus::AID &from);

    /**
     * when ds_worker/proxy is faulty or recovered, need to add taint/remove taint to node
     *
     * @param from interface caller's AID
     * @param name function name
     * @param msg request data, type is messages::ReportNodeFaultRequest
     */
    void UpdateNodeTaint(const litebus::AID &from, std::string &&name, std::string &&msg);

    void DeletePod(const litebus::AID &from, std::string &&name, std::string &&msg);

    void DeletePodByName(const std::string &podName);

    void QueryTerminatingLongRunPod(const litebus::AID &from, std::string &&name, std::string &&msg);

    void HandlePodModifiedEvent(const K8sEventType &type, const std::shared_ptr<ModelBase> &model);

    void HandleNodeModifiedEvent(const K8sEventType &type, const std::shared_ptr<ModelBase> &model);

    std::shared_ptr<V1PodTemplateSpec> GeneratePodTemplateSpec(const ResourcePool &pool,
                                                               const std::shared_ptr<V1Deployment> &templateDeployment);

    void ParseFaultRecoveryParams(const functionsystem::functionmaster::Flags &flags);

    void ParseParams(const functionsystem::functionmaster::Flags &flags);

    litebus::Option<std::shared_ptr<V1DeleteOptions>> GetPodDeleteOptions(const std::string &podName,
                                                                          bool isForce = true);

    void DeletePodNotBindInstance(const std::shared_ptr<V1Pod> &pod);

    void UpdatePodLabelsWithoutInstance(const std::shared_ptr<V1Pod> &pod);

    void MigratePodInstanceWithTaints(const std::string &hostIP, const std::string &podName);

    void MigrateNodeInstanceWithTaints(const std::shared_ptr<V1Node> node);

    void UpdateLocalStatusResponse(const litebus::AID &from, std::string&& /* name */, std::string &&msg);

    void DoSendUpdateLocalRequest(const litebus::AID &from, const uint32_t &status);

    bool CheckPodHasSpecialize(const std::string &podName);

    void SendUpdateLocalStatusRequest(const std::shared_ptr<V1Node> node, const bool &hasEvictedTaint);

    void StartCheckSystemFunctionNeedScale();

    static std::string PoolDeploymentName(const std::string &poolName, bool isCustom = false)
    {
        std::string name = POOL_NAME_PREFIX + poolName;
        return isCustom ? CUSTOM_POD_PREFIX + name : name;
    }

    static std::string GeneratePodName(const std::string &name, const std::string functionName = "",
                                       bool isCustom = false, bool isSystemFunc = false)
    {
        auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
        auto splits = litebus::strings::Split(uuid.ToString(), "-");
        std::string uuidSuffix = splits[0] + splits[splits.size() - 1];
        std::string podName = "";
        if (isSystemFunc) {
            auto funcLastName = GetLastFunctionNameFromKey(functionName);
            if (funcLastName.IsNone()) {
                podName = PoolDeploymentName(name, isCustom) + "-" + uuidSuffix;
            } else {
                podName = PoolDeploymentName(name, isCustom) + "-" + funcLastName.Get() + "-" + uuidSuffix;
            }
        } else {
            podName = PoolDeploymentName(name, isCustom) + "-" + uuidSuffix;
        }
        if (podName.size() > POD_NAME_MAX_LENGTH) {
            podName = podName.substr(0, POD_NAME_MAX_LENGTH);
        }
        return podName;
    }

    /**
     * @brief generate pool name by resources, final result would be like:
     *   function-agent-{instanceIdPart1}-[{resourceName}-{resourceValue}]-fusion-{uuid}
     * @example:
     *  function-agent-b951befab80f-cpu-777m-memory-777mi-fusion-52fa1f94-84a0-4100-8000-000000000806
     *  function-agent-d92f6942ef61-cpu-1000m-memory-1024mi-nvidia.com-gpu-1-fusion-a3761400-0000-4000-8000-0146a30e2495
     *
     * @param instanceID the instanceID
     * @param resourcesMap the resources
     * @return std::string the pool name
     */
    static std::string GeneratePoolNameByResources(const std::string &instanceID,
                                                   const std::map<std::string, std::string> &resourcesMap)
    {
        auto splits = litebus::strings::Split(instanceID, "-");
        std::string result = splits[splits.size() - 1];
        for (auto &it : resourcesMap) {
            result += "-" + it.second;
        }
        (void)std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            // k8s requires podname in (a-z || A-Z || 0-9 || - || .)
            if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || (c == '-') || (c == '.') ||
                ('0' <= c && c <= '9')) {
                return std::tolower(c);
            }
            return int('-');
        });
        return result;
    }

    std::shared_ptr<V2HorizontalPodAutoscaler> GenerateV2HorizontalPodAutoscaler(
        const ResourcePool &pool, const std::string &rNamespace);

    // for test
    [[maybe_unused]] void SetKubeClient(std::shared_ptr<KubeClient> kubeClient)
    {
        member_->kubeClient = std::move(kubeClient);
    }

    // for test
    [[maybe_unused]] void SetTemplateDeployment(std::shared_ptr<V1Deployment> deployment)
    {
        member_->templateDeployment = std::move(deployment);
    }

    // for test
    [[maybe_unused]] std::unordered_map<std::string, std::shared_ptr<V1Deployment>> GetPoolDeploymentsMap()
    {
        return this->member_->poolManager->GetPoolDeploymentsMap();
    }

    // for test
    [[maybe_unused]] void SetPodMap(const std::unordered_map<std::string, std::shared_ptr<V1Pod>> &podNameMap)
    {
        member_->podNameMap = podNameMap;
    }

    // for test
    [[maybe_unused]] void SetNodeMap(const std::unordered_map<std::string, std::shared_ptr<V1Node>> &nodeMap)
    {
        member_->nodes = nodeMap;
    }

    [[maybe_unused]] std::unordered_map<std::string, std::shared_ptr<V1Node>> GetNodeMap()
    {
        return member_->nodes;
    }

    [[maybe_unused]] void SetPoolManager(std::shared_ptr<PoolManager> &poolManager)
    {
        member_->poolManager = poolManager;
    }

    // only for test
    [[maybe_unused]] bool GetIsUpgrading();

    // only for test
    [[maybe_unused]] void SetIsUpgrading(bool isUpgrading);

    // for test
    [[maybe_unused]] void SetInstanceId2PodName(std::string instanceID, std::string podName)
    {
        member_->instanceId2PodName[instanceID] = podName;
    }

    // for test
    [[maybe_unused]] void SetEvictedTaintKey(std::string taint)
    {
        member_->evictedTaintKey = taint;
    }

protected:
    void Init() override;
    void Finalize() override;

    void InitLeader();
    virtual void DoCreateAgent(ResourcePool &pool, const ::resources::InstanceInfo &instanceInfo,
                               const std::shared_ptr<messages::CreateAgentRequest> &createAgentRequest,
                               const litebus::AID &from);

    void CreatePodPool(const std::shared_ptr<PodPoolInfo> &podPool);
    void CreatePodPoolWithResourcePool(const std::shared_ptr<PodPoolInfo> &podPool, const ResourcePool &resourcePool);

    litebus::Future<std::shared_ptr<V2HorizontalPodAutoscaler>> CreateHPA(
        const std::shared_ptr<PodPoolInfo> &podPool,
        const ResourcePool &pool,
        const std::shared_ptr<V2HorizontalPodAutoscalerList> &list);

    litebus::Future<std::shared_ptr<V2HorizontalPodAutoscaler>> UpdateHPA(
        const std::shared_ptr<PodPoolInfo> &podPool,
        const ResourcePool &pool,
        const std::shared_ptr<V2HorizontalPodAutoscalerList> &list);

    void UpdatePodPool(const std::shared_ptr<PodPoolInfo> &podPool);
    void UpdatePodPoolWithResourcePool(const std::shared_ptr<PodPoolInfo> &podPool, const ResourcePool &resourcePool);

    void DoUpdatePodPool(const std::shared_ptr<PodPoolInfo> &podPool, const std::shared_ptr<V1Deployment> &deployment,
                         const ResourcePool &resourcePool);

    void DoPersistencePoolInfo(const std::string &poolID);

    void DeletePodPool(const std::shared_ptr<PodPoolInfo> &podPool);

    void OnDeletePodPoolComplete(const litebus::Future<std::shared_ptr<V1Deployment>> &retDeployment,
                                 const std::string &name, const std::shared_ptr<PodPoolInfo> &podPool);

    void OnUpdatePodPoolComplete(const litebus::Future<std::shared_ptr<V1Deployment>> &retDeployment,
                                 const std::shared_ptr<PodPoolInfo> &podPool);

    virtual litebus::Future<Status> DoDeletePod(const std::string &deletePodName, bool isForce = true);

    MigrateType CheckNodeFault(const std::shared_ptr<V1Node> node);

    void DoPodInstanceMigrate(const std::string &podName, const MigrateType type);

    void DoEvictedPodInstanceMigrate(const std::string &podName);

    void DoPodInstanceGracefulMigrate(const std::string &podName, const std::string &instanceID);

    void CheckInstanceNeedMigrate(const ::resources::InstanceInfo &instanceInfo);

    void RemoveEvictAnnotation(const std::shared_ptr<V1Pod> pod, const std::string &instanceID);

    litebus::Future<std::shared_ptr<V1Pod>> CreatePod(const std::string &name,
                                                      const std::shared_ptr<V1PodTemplateSpec> &spec,
                                                      const std::string &instanceID,
                                                      const std::map<std::string, std::string> &affinityTypeLabels,
                                                      const std::map<std::string, std::string> &annotations);

protected:
    struct Member {
        std::shared_ptr<KubeClient> kubeClient{ nullptr };
        std::shared_ptr<MetaStorageAccessor> metaStorageAccessor{ nullptr };
        std::unordered_map<std::string, messages::CreateAgentRequest> creatingRequests;
        std::shared_ptr<V1Deployment> templateDeployment{ nullptr };
        std::shared_ptr<PoolManager> poolManager{ nullptr };
        std::shared_ptr<SystemFunctionPodManager> frontendManager{ nullptr };
        std::unordered_map<std::string, std::shared_ptr<V1Node>> nodes;
        std::string k8sNamespace;
        SystemUpgradeParam systemUpgradeParam;
        bool isUpgrading{ false };
        bool migrateEnable{ false };
        std::string migrateResourcePrefix;
        std::unordered_map<std::string, std::string> workerTaintExcludeLabels;
        uint32_t gracePeriodSeconds{ DEFAULT_GRACE_PERIOD_SECONDS };
        std::string poolConfigPath;
        std::string agentTemplatePath;

        /**
        * The meaning of the two mappings is as follows,
        *   [1] instanceID -> podName : whether instance exists and the corresbonding pod is managed by scaler
        *   [2] podName -> instanceID : whether the pod is managed by scaler
        *
        * > When CreateAgent, record both Instance@Pod and Pod@Instance
        *
        * > When receive event of one pod is ok,
        *   [1] check whether the pod is managed by scaler(i.e. podName in Pod@Instance)
        *       if not, do nothing
        *   [2] check whether the pod belongs to an delete/exiting/exited... instance (i.e. instanceID not in
                Instance@Pod)
        *       if so, send delete pod request to k8s
        *
        * > When receive event of one pod is deleted, remove the record of podName in Pod@Instance.
        *
        * > When receive instance delete/exiting/exited... events
        *   [1] delete the record of instanceID in Instance@Pod
        *   [2] send delete pod request to k8s
        *
        * Pooled pods without any unique labels are not really cared since they can be reused when there is no instance
        * running inside them.
        */
        std::unordered_map<std::string, std::string> instanceId2PodName;
        std::unordered_map<std::string, std::string> podName2InstanceId;
        std::unordered_map<std::string, std::set<std::string>> nodeIdPodNameMap;
        std::unordered_map<std::string, std::set<std::string>> nodeIdReadyWorkerMap;
        std::set<std::string> needMigratingInstanceIds;
        std::set<std::string> taintToleranceSet;
        std::set<std::string> migratingPodSet;

        std::unordered_map<std::string, std::shared_ptr<V1Pod>> podNameMap;
        // Pending pod map, key: podName, value: time for first reception of pod
        // start time of Pending: when pod comes first time, set ReceivedTime
        std::unordered_map<std::string, std::chrono::system_clock::time_point> pendingPods;

        std::string etcdAuthType;
        std::string etcdSecretName;

        std::string evictedTaintKey;
        bool hasEvictedTaint {false};
        std::string localSchedPort;

        std::string workerTaintKey {DS_WORKER_TAINT_KEY};
        std::string proxyTaintKey {FUNCTION_PROXY_TAINT_KEY};

        bool isSynced{ false };
    };

    class Business : public leader::BusinessPolicy {
    public:
        Business(const std::shared_ptr<ScalerActor> &actor, const std::shared_ptr<Member> &member)
            : actor_(actor), member_(member)
        {
        }
        ~Business() override = default;

        virtual Status CreateResourcePools(const std::shared_ptr<std::vector<ResourcePool>> &pools) = 0;
        virtual Status CreatePodPools(const std::unordered_map<std::string, std::shared_ptr<PodPoolInfo>> &pools) = 0;
        virtual void UpdatePodLabels(const InstanceInfo &instanceInfo, bool isInstanceDelete = false) = 0;
        virtual void DeletePod(const std::string &podName) = 0;
        virtual void CreateAgent(const litebus::AID &from, std::string &&name, std::string &&msg) = 0;
        virtual void DeletePod(const litebus::AID &from, std::string &&name, std::string &&msg) = 0;
        virtual void CreatePodPool(const std::shared_ptr<PodPoolInfo> &podPool) = 0;
        virtual void UpdatePodPool(const std::shared_ptr<PodPoolInfo> &podPool) = 0;
        virtual void PersistencePoolInfo(const std::string &poolID) = 0;
        virtual void DeletePodPool(const std::shared_ptr<PodPoolInfo> &podPool) = 0;
        virtual void UpdateNodeTaint(const litebus::AID &from, std::string &&name, std::string &&msg) = 0;
        virtual void DeletePodNotBindInstance(const std::shared_ptr<V1Pod> &pod) = 0;
        virtual void UpdatePodLabelsWithoutInstance(const std::shared_ptr<V1Pod> &pod) = 0;
        virtual void MigratePodInstanceWithTaints(const std::string &hostIP, const std::string &podName) = 0;
        virtual void MigrateNodeInstanceWithTaints(const std::shared_ptr<V1Node> node) = 0;
        virtual void CheckPodStatus() = 0;
        virtual void HandleAbnormalPod(const std::shared_ptr<V1Pod> pod) = 0;
        virtual void ScaleUpPodByPoolID(const std::string &poolID, bool isReserved, const std::string &requestID,
                                const litebus::AID &from) = 0;

    protected:
        std::weak_ptr<ScalerActor> actor_;
        std::shared_ptr<Member> member_;
    };

    class MasterBusiness : public Business {
    public:
        MasterBusiness(const std::shared_ptr<ScalerActor> &actor, const std::shared_ptr<Member> &member)
            : Business(actor, member)
        {
        }
        ~MasterBusiness() override = default;
        void OnChange() override;
        void UpdatePodLabels(const InstanceInfo &instanceInfo, bool isInstanceDelete = false) override;
        void DeletePod(const std::string &podName) override;
        Status CreateResourcePools(const std::shared_ptr<std::vector<ResourcePool>> &pools) override;
        Status CreatePodPools(const std::unordered_map<std::string, std::shared_ptr<PodPoolInfo>> &pools) override;
        void CreatePodPool(const std::shared_ptr<PodPoolInfo> &podPool) override;
        void UpdatePodPool(const std::shared_ptr<PodPoolInfo> &podPool) override;
        void PersistencePoolInfo(const std::string &poolID) override;
        void DeletePodPool(const std::shared_ptr<PodPoolInfo> &podPool) override;
        void CreateAgent(const litebus::AID &from, std::string &&name, std::string &&msg) override;
        void DeletePod(const litebus::AID &from, std::string &&name, std::string &&msg) override;
        void UpdateNodeTaint(const litebus::AID &from, std::string &&name, std::string &&msg) override;
        void DeletePodNotBindInstance(const std::shared_ptr<V1Pod> &pod) override;
        void UpdatePodLabelsWithoutInstance(const std::shared_ptr<V1Pod> &pod) override;
        void MigratePodInstanceWithTaints(const std::string &hostIP, const std::string &podName) override;
        void MigrateNodeInstanceWithTaints(const std::shared_ptr<V1Node> node) override;
        void CheckPodStatus() override;
        void HandleAbnormalPod(const std::shared_ptr<V1Pod> pod) override;
        void ScaleUpPodByPoolID(const std::string &poolID, bool isReserved, const std::string &requestID,
                                const litebus::AID &from) override;
    };

    class SlaveBusiness : public Business {
    public:
        SlaveBusiness(const std::shared_ptr<ScalerActor> &actor, const std::shared_ptr<Member> &member)
            : Business(actor, member)
        {
        }
        ~SlaveBusiness() override = default;
        void OnChange() override;
        void UpdatePodLabels(const InstanceInfo &instanceInfo, bool isInstanceDelete = false) override;
        void DeletePod(const std::string &podName) override;
        Status CreateResourcePools(const std::shared_ptr<std::vector<ResourcePool>> &pools) override;
        Status CreatePodPools(const std::unordered_map<std::string, std::shared_ptr<PodPoolInfo>> &pools) override;
        void CreateAgent(const litebus::AID &from, std::string &&name, std::string &&msg) override;
        void DeletePod(const litebus::AID &from, std::string &&name, std::string &&msg) override;
        void CreatePodPool(const std::shared_ptr<PodPoolInfo> &podPool) override;
        void UpdatePodPool(const std::shared_ptr<PodPoolInfo> &podPool) override;
        void DeletePodPool(const std::shared_ptr<PodPoolInfo> &podPool) override;
        void PersistencePoolInfo(const std::string &poolID) override;
        void UpdateNodeTaint(const litebus::AID &from, std::string &&name, std::string &&msg) override;
        void DeletePodNotBindInstance(const std::shared_ptr<V1Pod> &pod) override;
        void UpdatePodLabelsWithoutInstance(const std::shared_ptr<V1Pod> &pod) override;
        void MigratePodInstanceWithTaints(const std::string &hostIP, const std::string &podName) override;
        void MigrateNodeInstanceWithTaints(const std::shared_ptr<V1Node> node) override;
        void CheckPodStatus() override;
        void HandleAbnormalPod(const std::shared_ptr<V1Pod> pod) override;
        void ScaleUpPodByPoolID(const std::string &poolID, bool isReserved, const std::string &requestID,
                                const litebus::AID &from) override;
    };

    std::string dsWorkPort_;

    std::set<std::string> deletingPodSet_;
    std::unordered_map<std::string, litebus::Promise<std::shared_ptr<V1Pod>>> waitForReadyPods_;

    std::shared_ptr<Member> member_;

    std::unordered_map<std::string, std::shared_ptr<Business>> businesses_;

    std::string curStatus_;
    std::shared_ptr<Business> business_;

    std::map<std::string, std::string> CheckAndBuildResourcesMap(const ::resources::InstanceInfo &instanceInfo);

    messages::CreateAgentResponse GenCreateAgentResponse(const std::string &requestID, const int32_t code,
                                                         const std::string &message);

    messages::CreateAgentResponse InitCreateAgentResponse(int32_t code, const std::string &message,
                                                          const messages::CreateAgentRequest &source);

    void SendCreateAgentFailedResponse(const litebus::AID &from, int32_t code, const std::string &message,
                                       const messages::CreateAgentRequest &source);

    void DeleteFailedPod(const std::string &uid, const std::string &podName);

    static void SetMetaData(const ResourcePool &pool, const std::shared_ptr<V1Deployment> &newDeployment);

    void OnUpdateNodeTaintsComplete(const litebus::Future<std::shared_ptr<V1Node>> &retNode,
                                    const std::shared_ptr<messages::UpdateNodeTaintRequest> &req,
                                    const litebus::AID &from);

    litebus::Future<Status> DoUpdateNodeTaint(const std::shared_ptr<messages::UpdateNodeTaintRequest> &req,
                                              const litebus::AID &from);

    litebus::Future<std::shared_ptr<V1Node>> HandleNodeTaint(const std::string &ip, const std::string &key,
                                                             const bool isAdd);

    litebus::Future<std::shared_ptr<V1Node>> DoHandleNodeTaint(
        const std::string &ip, const std::string &key, const bool isAdd,
        const std::unordered_map<std::string, std::shared_ptr<V1Node>> &nodeList);

    bool CheckNodeLabels(const std::shared_ptr<V1Node> &node);

    virtual litebus::Future<Status> SyncPods(const std::shared_ptr<V1PodList> &podList);

    litebus::Future<std::unordered_map<std::string, std::shared_ptr<V1Node>>> DoSyncNode(
        const std::shared_ptr<V1NodeList> &nodeList);

    litebus::Future<Status> SyncInstances(const Status &status);

    litebus::Future<Status> SyncDeployments(const std::shared_ptr<V1DeploymentList> &deploymentList);

    void SyncCreatePodPromise(const std::shared_ptr<V1Pod> &pod);

    void HandlerWaitPodReady(const litebus::Future<std::shared_ptr<V1Pod>> &readyPod,
                             const std::shared_ptr<messages::CreateAgentRequest> &createAgentRequest,
                             const std::string &uid, const std::string &name, const litebus::AID &from);

    void HandlerWaitPoolPodReady(const litebus::Future<std::shared_ptr<V1Pod>> &readyPod, const std::string &requestID,
                                 const std::string &uid, const std::string &name, const litebus::AID &from);

    void GetDsWorkerPort(const std::shared_ptr<V1Deployment> &deployment);

    void AddDataSystemIpAndPort(const std::shared_ptr<V1Container> &container);

    ResourcePool BuildResourcePool(const ::resources::InstanceInfo &instanceInfo,
                                   const std::map<std::string, std::string> &resourcesMap);

    static void SetAffinityForPool(const ResourcePool &pool, const std::shared_ptr<V1PodTemplateSpec> &spec);

    virtual void RegisterKubeWatcher();

    friend class ScalerTest;

    void AddDelegateConfig(const ResourcePool &pool, const std::shared_ptr<V1PodTemplateSpec> &rt) const;

    bool IsNeedUpdateLabel(int32_t code);

    virtual void SyncKubeResourceForChange();

    void ParseSCCParams(const functionsystem::functionmaster::Flags &flags);

    void ParseEtcdParams(const functionsystem::functionmaster::Flags &flags);

    void ParseK8sClientParams(const functionsystem::functionmaster::Flags &flags);

    void AddVolumesAndMountsForSystemFunc(ResourcePool &pool);

    void AddUpdateLabelReq(const InstanceInfo &instanceInfo, bool isInstanceDelete);
    void TriggerUpdateLabel(std::string instanceID, const std::shared_ptr<V1Pod> &pod = nullptr);

    void PatchPodWithOwnerReference(const std::shared_ptr<V1Pod> &source, const std::shared_ptr<V1Pod> &target);

    void UpdatePodLabelsWithLatestPod(const InstanceInfo &instanceInfo, bool isInstanceDelete,
                                   const std::shared_ptr<V1Pod> &pod = nullptr);

    void OnDeletePodComplete(const litebus::Future<Status> &status, const litebus::AID &from,
                             const std::string &requestID);

    virtual litebus::Future<std::shared_ptr<V1Pod>> DoDeleteNamespacedPod(
        const std::string &podName, const std::string &rNamespace,
        const litebus::Option<std::shared_ptr<V1DeleteOptions>> &body = litebus::None(),
        const litebus::Option<bool> &orphanDependents = litebus::None());

    virtual litebus::Future<std::shared_ptr<V1Pod>> ReadNamespacedPod(const std::string &rNamespace,
                                                                      const std::string &podName);

    void ParseInstanceInfoToPool(ResourcePool &pool, const ::resources::InstanceInfo &instanceInfo);

    void UpdateMemberOnPodDeleted(const std::shared_ptr<V1Pod> &pod);
    void UpdateMemberOnPodModified(const std::shared_ptr<V1Pod> &pod);
    void HandleReadyPodOnModified(const K8sEventType &type, const std::shared_ptr<V1Pod> &pod);

    virtual litebus::Future<std::shared_ptr<V1DeploymentList>> ListNamespacedDeployment(const std::string &rNamespace);
    virtual Status SyncTemplateDeployment();

    std::unordered_map<std::string, UpdateLabelReq> updateLabelMap_;
    std::unordered_map<std::string, litebus::Promise<bool>> updateNodeMap_;

private:
    litebus::Future<SyncResult> SystemUpgradeSyncer(const std::shared_ptr<GetResponse> &getResponse);
    litebus::Future<SyncResult> InstanceInfoSyncer(const std::shared_ptr<GetResponse> &getResponse);
    litebus::Future<SyncResult> PodPoolSyncer(const std::shared_ptr<GetResponse> &getResponse);
    void StartCheckPodStatus();
};  // class ScalerActor
}  // namespace functionsystem::scaler

#endif  // FUNCTION_MASTER_SCALER_ACTOR_H
