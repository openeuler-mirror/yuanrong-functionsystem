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

#ifndef FUNCTION_MASTER_SCALER_POOL_MANAGER_H
#define FUNCTION_MASTER_SCALER_POOL_MANAGER_H

#include <map>
#include <string>

#include "common/constants/constants.h"
#include "meta_storage_accessor/meta_storage_accessor.h"
#include "common/status/status.h"
#include "common/kube_client/kube_client.h"
#include "common/kube_client/model/common/v1_resource_requirements.h"
#include "common/kube_client/model/container/v1_container.h"
#include "common/kube_client/model/deployment/v1_deployment.h"
#include "common/kube_client/model/pod/v1_pod.h"
#include "function_master/scaler/utils/parse_helper.h"
#include "common/kube_client/model/horizontal_pod_autoscaler/v2_horizontal_pod_autoscaler.h"

namespace functionsystem::scaler {

// value: {"resource.owner": "default"}
const std::string DEFAULT_INIT_LABELS = R"({")" + RESOURCE_OWNER_KEY + R"(":")" + DEFAULT_OWNER_VALUE + R"("})";
const uint32_t TERMINATION_GRACE_PERIOD_SECONDS = 30;

using V1Deployment = functionsystem::kube_client::model::V1Deployment;
using V1ResourceRequirements = functionsystem::kube_client::model::V1ResourceRequirements;
using V1Deployment = functionsystem::kube_client::model::V1Deployment;
using V1Affinity = functionsystem::kube_client::model::V1Affinity;
using V1PodAntiAffinity = functionsystem::kube_client::model::V1PodAntiAffinity;
using V1WeightedPodAffinityTerm = functionsystem::kube_client::model::V1WeightedPodAffinityTerm;

using V1Toleration = functionsystem::kube_client::model::V1Toleration;
using V1SeccompProfile = functionsystem::kube_client::model::V1SeccompProfile;
using V1TopologySpreadConstraint = functionsystem::kube_client::model::V1TopologySpreadConstraint;

enum class PoolState : int32_t { NEW, CREATING, UPDATE, UPDATING, RUNNING, FAILED, DELETED };

struct ResourcePool {
    std::string name;
    int32_t poolSize{ 0 };
    // cpu, mem, nvidia.com/gpu, amd.com/gpu, ...
    std::map<std::string, std::string> requestResources;
    std::map<std::string, std::string> limitResources;
    bool isReuse{ false };
    std::string initLabels{ DEFAULT_INIT_LABELS };
    litebus::Option<std::shared_ptr<V1Container>> delegateContainer = litebus::None();
    litebus::Option<std::shared_ptr<V1Container>> delegateRuntimeManager = litebus::None();
    std::vector<std::shared_ptr<V1Volume>> delegateVolumes{};
    std::vector<std::shared_ptr<V1VolumeMount>> delegateVolumeMounts{};
    std::vector<std::shared_ptr<V1VolumeMount>> delegateAgentVolumeMounts{};
    std::vector<std::shared_ptr<V1HostAlias>> delegateHostAliases{};
    std::vector<std::shared_ptr<V1Container>> delegateSidecars{};
    std::vector<std::shared_ptr<V1Container>> delegateInitContainers{};
    std::map<std::string, std::string> nodeSelector;

    std::vector<std::shared_ptr<V1EnvVar>> delegateDecryptEnvs{};
    std::shared_ptr<V1Affinity> affinity{ nullptr };
    std::vector<std::shared_ptr<V1Toleration>> delegateTolerations{};
    std::shared_ptr<V1SeccompProfile> delegateSecCompProfile{ nullptr };
    std::vector<std::shared_ptr<V1VolumeMount>> delegateInitVolumeMounts{};
    std::vector<std::shared_ptr<V1EnvVar>> delegateInitEnvs{};
    bool isSystemFunc{ false };
    bool isNeedBindCPU{ false };
    int64_t terminationGracePeriodSeconds{ TERMINATION_GRACE_PERIOD_SECONDS };
    std::map<std::string, std::string> delegatePoolLabels;
    std::string initImage;
    std::shared_ptr<V2HorizontalPodAutoscaler> horizontalPodAutoscaler{ nullptr };
    std::string runtimeClassName;
    std::vector<std::shared_ptr<V1TopologySpreadConstraint>> delegateTopologySpreadConstraints{};
    bool isAggregationNodeAffinity{ false };
    int32_t podPendingDurationThreshold { 0 };
    bool isScalable{ false };
};

struct IdleRecyclePolicy {
    int32_t reserved{ 0 };
    int32_t scaled{ 0 };
};

struct PodPoolInfo {
    std::string id;
    std::string group;
    int32_t size;
    int32_t maxSize;
    bool reuse{ false };
    int32_t readyCount;
    int32_t status;
    std::string msg;
    std::string image;
    std::string initImage;
    std::map<std::string, std::string> labels;
    std::map<std::string, std::string> environments;
    std::string volumes;
    std::string volumeMounts;
    std::string affinities;
    std::shared_ptr<V1ResourceRequirements> resources{ nullptr };
    bool scalable{ false };
    std::string horizontalPodAutoscalerSpec;
    std::map<std::string, std::string> nodeSelectors;
    std::string tolerations;
    std::string runtimeClassName;
    std::string topologySpreadConstraints;
    int32_t podPendingDurationThreshold;
    IdleRecyclePolicy idleRecycleTime;
    // pod is created from k8s (contains pending pod)
    std::unordered_set<std::string> readyPodSet;
    // pod spec is generate, but waiting to be created
    std::unordered_set<std::string> pendingCreatePodSet;
    // pod is deleting
    std::unordered_set<std::string> deletingPodSet;
};

class PoolManager {
public:
    explicit PoolManager(const std::shared_ptr<MetaStorageAccessor> &metaStorageAccessor);

    ~PoolManager() = default;

    Status LoadPodPoolsConfig(const std::string &configFile);

    static ResourcePool ParsePoolInfo(const nlohmann::json &parser);

    static void TransToPoolInfoFromJson(const std::string &jsonStr, const std::shared_ptr<PodPoolInfo> podPoolInfo);

    std::shared_ptr<PodPoolInfo> GetOrNewPool(const std::string &poolID);

    void DeletePodPoolInfo(const std::string &poolID, const bool sync = false);

    std::shared_ptr<PodPoolInfo> GetPodPool(const std::string &poolID);

    void PutDeployment(const std::shared_ptr<V1Deployment> &deployment);

    void DeleteDeployment(const std::string &name);

    void OnPodUpdate(const std::shared_ptr<V1Pod> &pod);

    void OnPodDelete(const std::shared_ptr<V1Pod> &pod, bool isSync = true);

    std::shared_ptr<V1Deployment> GetDeployment(const std::string &name);

    static bool ValidatePodPoolCreateParams(const std::shared_ptr<PodPoolInfo>& podPool);

    void PersistencePoolInfo(const std::string &poolID);

    ResourcePool GenerateResourcePool(const std::shared_ptr<PodPoolInfo> &podPoolInfo);

    std::string TransToJsonFromPodPoolInfo(const std::shared_ptr<PodPoolInfo> &poolInfo);

    void RegisterScaleUpHandler(const std::function<void(const std::string &, bool)> &cb)
    {
        scaleUpHandler_ = cb;
    }

    void RegisterPersistHandler(const std::function<void(const std::string &)> &cb)
    {
        persistHandler_ = cb;
    }

    litebus::Option<std::shared_ptr<V1Pod>> GenerateNewPodForPodPool(const std::shared_ptr<PodPoolInfo> &poolInfo,
                                                                     bool isReserved = false);

    litebus::Option<std::shared_ptr<V1Pod>> TryScaleUpPod(const std::string &poolID, bool isReserved = true);

    std::shared_ptr<std::vector<ResourcePool>> GetLoadedResourcePools()
    {
        return loadedResourcePools_;
    }

    std::unordered_map<std::string, std::shared_ptr<PodPoolInfo>> GetLocalPodPools()
    {
        return localPoolMap_;
    }

    std::shared_ptr<std::set<std::string>> GetLoadedResourcePoolNames()
    {
        return loadedResourcePoolNames_;
    }

    std::unordered_map<std::string, std::shared_ptr<PodPoolInfo>> GetPoolMap()
    {
        return poolMap_;
    }

    // for test
    [[maybe_unused]] std::unordered_map<std::string, std::shared_ptr<V1Deployment>> GetPoolDeploymentsMap()
    {
        return this->deploymentsMap_;
    }

private:
    void InitOwnerReference(const std::shared_ptr<V1Deployment> deployment, const std::shared_ptr<V1Pod> pod);
    std::shared_ptr<PodPoolInfo> GetPodPoolFromPod(const std::shared_ptr<V1Pod> &pod);
    std::shared_ptr<MetaStorageAccessor> metaStorageAccessor_{ nullptr };
    std::shared_ptr<std::vector<ResourcePool>> loadedResourcePools_{ nullptr };
    std::shared_ptr<std::set<std::string>> loadedResourcePoolNames_{ nullptr };
    std::unordered_map<std::string, std::shared_ptr<PodPoolInfo>> poolMap_;
    std::unordered_map<std::string, std::shared_ptr<PodPoolInfo>> localPoolMap_ {};
    std::unordered_map<std::string, std::shared_ptr<V1Deployment>> deploymentsMap_;
    std::function<void(const std::string &, bool)> scaleUpHandler_;
    std::function<void(const std::string &)> persistHandler_;
};
}  // namespace functionsystem::scaler
#endif  // FUNCTION_MASTER_SCALER_POOL_MANAGER_H
