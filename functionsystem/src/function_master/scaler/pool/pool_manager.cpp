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
#include <regex>

#include "function_master/scaler/pool/pool_manager.h"

#include "function_master/scaler/constants.h"
#include "common/utils/meta_store_kv_operation.h"
#include "common/logs/logging.h"
#include "common/utils/files.h"
#include "common/kube_client/model/common/model_utils.h"
#include "function_master/scaler/utils/parse_helper.h"

namespace functionsystem::scaler {

const std::string POOL_NAME = "name";
const std::string POOL_SIZE = "poolSize";
const std::string POOL_REQUEST_CPU = "requestCpu";
const std::string POOL_REQUEST_MEMORY = "requestMemory";
const std::string POOL_LIMIT_CPU = "limitCpu";
const std::string POOL_LIMIT_MEMORY = "limitMemory";
const std::string POOL_IS_REUSE = "isReuse";
const std::string ID_STR = "id";
const std::string SIZE_STR = "size";
const std::string REUSE = "reuse";
const std::string GROUP = "group";
const std::string IMAGE = "image";
const std::string INIT_IMAGE = "init_image";
const std::string READY_COUNT = "ready_count";
const std::string POOL_STATUS = "status";
const std::string POOL_MSG = "msg";
const std::string POOL_LABELS = "labels";
const std::string ENVIRONMENTS = "environment";
const std::string VOLUMES = "volumes";
const std::string VOLUME_MOUNTS = "volume_mounts";
const std::string RESOURCES = "resources";
const std::string AFFINITIES = "affinities";
const std::string RUNTIME_CLASS_NAME = "runtime_class_name";
const std::string TOLERATIONS = "tolerations";
const std::string NODE_SELECTOR = "node_selector";
const std::string TOPOLOGY_SPREAD_CONSTRAINTS = "topology_spread_constraints";
const std::string POD_PENDING_DURATION_THRESHOLD = "pod_pending_duration_threshold";
const std::string MAX_SIZE_STR = "max_size";
const std::string RESERVED_STR = "reserved";
const std::string SCALED_STR = "scaled";
const std::string IDLE_RECYCLE_TIME = "idle_recycle_time";
const std::string SCALABLE = "scalable";
const std::string LABEL_IDLE_TO_RECYCLE = "yr-idle-to-recycle";
const std::string UNLIMITED = "unlimited";
const std::string POOLS_STR = "pools";
const std::regex ID_REGEX = std::regex("^[a-z0-9]([-a-z0-9]{0,38}[a-z0-9])?$");
const std::regex GROUP_REGEX = std::regex("^[a-zA-Z0-9]([-a-zA-Z0-9]{0,38}[a-zA-Z0-9])?$");
constexpr int32_t MIN_SIZE = 0;
constexpr int32_t MIN_IDLE_TIME = -1;
constexpr int32_t MAX_IMAGE_LEN = 200;
constexpr int32_t MAX_INIT_IMAGE_LEN = 200;
constexpr int32_t MAX_RUNTIME_CLASS_LEN = 64;

const std::unordered_set<int32_t> CAN_BE_RUNNING_STATE = { static_cast<int32_t>(PoolState::CREATING),
                                                           static_cast<int32_t>(PoolState::UPDATING),
                                                           static_cast<int32_t>(PoolState::RUNNING) };

PoolManager::PoolManager(const std::shared_ptr<MetaStorageAccessor> &metaStorageAccessor)
{
    metaStorageAccessor_ = metaStorageAccessor;
    loadedResourcePools_ = std::make_shared<std::vector<ResourcePool>>();
    loadedResourcePoolNames_ = std::make_shared<std::set<std::string>>();
}

ResourcePool PoolManager::GenerateResourcePool(const std::shared_ptr<PodPoolInfo> &podPoolInfo)
{
    ResourcePool pool;
    pool.name = podPoolInfo->id;
    pool.poolSize = podPoolInfo->size;
    pool.isReuse = podPoolInfo->reuse;
    pool.initImage = podPoolInfo->initImage;
    pool.isScalable = podPoolInfo->scalable;
    if (!podPoolInfo->volumeMounts.empty()) {
        ParseVolumeMountsFromJson(podPoolInfo->volumeMounts, pool.delegateVolumeMounts);
        pool.delegateAgentVolumeMounts = pool.delegateVolumeMounts;
        pool.delegateInitVolumeMounts = pool.delegateVolumeMounts;
    }
    if (!podPoolInfo->image.empty() || !podPoolInfo->environments.empty()) {
        std::shared_ptr<V1Container> runtimeManager = std::make_shared<V1Container>();
        if (!podPoolInfo->image.empty()) {
            runtimeManager->SetImage(podPoolInfo->image);
        }
        if (!podPoolInfo->environments.empty()) {
            runtimeManager->SetEnv(ParseContainerEnvFromMap(podPoolInfo->environments));
        }
        pool.delegateRuntimeManager = runtimeManager;
    }
    if (!podPoolInfo->volumes.empty()) {
        ParseVolumesFromJson(podPoolInfo->volumes, pool.delegateVolumes);
    }
    if (podPoolInfo->labels.size() > 0) {
        pool.delegatePoolLabels = podPoolInfo->labels;
    }
    if (!podPoolInfo->group.empty()) {
        pool.delegatePoolLabels.emplace(podPoolInfo->group, podPoolInfo->group);
    }
    if (!podPoolInfo->tolerations.empty()) {
        ParseDelegateTolerationsFromStr(podPoolInfo->tolerations, pool.delegateTolerations);
    }
    if (!podPoolInfo->topologySpreadConstraints.empty()) {
        ParseDelegateTopologySpreadConstraintsFromStr(podPoolInfo->topologySpreadConstraints,
                                                      pool.delegateTopologySpreadConstraints);
    }
    if (!podPoolInfo->affinities.empty()) {
        pool.affinity = std::make_shared<V1Affinity>();
        ParseAffinityFromStr(podPoolInfo->affinities, pool.affinity);
    }
    if (podPoolInfo->nodeSelectors.size() > 0) {
        pool.nodeSelector = podPoolInfo->nodeSelectors;
    }
    if (!podPoolInfo->horizontalPodAutoscalerSpec.empty()) {
        auto spec = ParseHorizontalPodAutoscaler(podPoolInfo->horizontalPodAutoscalerSpec);
        if (spec != nullptr) {
            pool.horizontalPodAutoscaler = std::make_shared<V2HorizontalPodAutoscaler>();
            pool.horizontalPodAutoscaler->SetSpec(spec);
        }
    }
    pool.podPendingDurationThreshold = podPoolInfo->podPendingDurationThreshold;
    pool.limitResources = podPoolInfo->resources->GetLimits();
    pool.requestResources = podPoolInfo->resources->GetRequests();
    pool.runtimeClassName = podPoolInfo->runtimeClassName;
    return pool;
}

void ParseBasic(const nlohmann::json &j, const std::shared_ptr<PodPoolInfo> &podPoolInfo)
{
    if (j.find(ID_STR) != j.end()) {
        podPoolInfo->id = j.at(ID_STR).get<std::string>();
    }
    if (j.find(GROUP) != j.end()) {
        podPoolInfo->group = j.at(GROUP).get<std::string>();
    }
    if (j.find(SIZE_STR) != j.end()) {
        podPoolInfo->size = j.at(SIZE_STR).get<int32_t>();
    }
    if (j.find(REUSE) != j.end()) {
        podPoolInfo->reuse = j.at(REUSE).get<bool>();
    }
    if (j.find(IMAGE) != j.end()) {
        podPoolInfo->image = j.at(IMAGE).get<std::string>();
    }
    if (j.find(INIT_IMAGE) != j.end()) {
        podPoolInfo->initImage = j.at(INIT_IMAGE).get<std::string>();
    }
    if (j.find(MAX_SIZE_STR) != j.end()) {
        podPoolInfo->maxSize = j.at(MAX_SIZE_STR).get<int32_t>();
    }
    if (j.find(IDLE_RECYCLE_TIME) != j.end()) {
        auto recycleTime = j.at(IDLE_RECYCLE_TIME).get<std::unordered_map<std::string, int32_t>>();
        if (recycleTime.find(RESERVED_STR) != recycleTime.end()) {
            podPoolInfo->idleRecycleTime.reserved = recycleTime[RESERVED_STR];
        }
        if (recycleTime.find(SCALED_STR) != recycleTime.end()) {
            podPoolInfo->idleRecycleTime.scaled = recycleTime[SCALED_STR];
        }
    }
    if (j.find(SCALABLE) != j.end()) {
        podPoolInfo->scalable = j.at(SCALABLE).get<bool>();
    }
}

void PoolManager::TransToPoolInfoFromJson(const std::string &jsonStr, const std::shared_ptr<PodPoolInfo> podPoolInfo)
{
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonStr);
    } catch (std::exception &error) {
        YRLOG_WARN("failed to parse pod pool info, error: {}", error.what());
        return;
    }
    ParseBasic(j, podPoolInfo);
    if (j.find(RUNTIME_CLASS_NAME) != j.end()) {
        podPoolInfo->runtimeClassName = j.at(RUNTIME_CLASS_NAME).get<std::string>();
    }
    if (j.find(VOLUMES) != j.end()) {
        podPoolInfo->volumes = j.at(VOLUMES).get<std::string>();
    }
    if (j.find(VOLUME_MOUNTS) != j.end()) {
        podPoolInfo->volumeMounts = j.at(VOLUME_MOUNTS).get<std::string>();
    }
    if (j.find(AFFINITIES) != j.end()) {
        podPoolInfo->affinities = j.at(AFFINITIES).get<std::string>();
    }
    if (j.find(RESOURCES) != j.end()) {
        podPoolInfo->resources = ParseResourceRequirements(j.at(RESOURCES));
    }
    if (j.find(TOLERATIONS) != j.end()) {
        podPoolInfo->tolerations = j.at(TOLERATIONS).get<std::string>();
    }
    if (j.find(TOPOLOGY_SPREAD_CONSTRAINTS) != j.end()) {
        podPoolInfo->topologySpreadConstraints = j.at(TOPOLOGY_SPREAD_CONSTRAINTS).get<std::string>();
    }
    if (j.find(POD_PENDING_DURATION_THRESHOLD) != j.end()) {
        podPoolInfo->podPendingDurationThreshold = j.at(POD_PENDING_DURATION_THRESHOLD).get<int32_t>();
    }
    podPoolInfo->labels = std::move(ParseMapFromJson(j, POOL_LABELS));
    podPoolInfo->environments = std::move(ParseMapFromJson(j, ENVIRONMENTS));
    podPoolInfo->nodeSelectors = std::move(ParseMapFromJson(j, NODE_SELECTOR));
    if (j.find(READY_COUNT) != j.end()) {
        podPoolInfo->readyCount = j.at(READY_COUNT).get<int32_t>();
    }
    if (j.find(POOL_STATUS) != j.end()) {
        podPoolInfo->status = j.at(POOL_STATUS).get<int32_t>();
    }
    if (j.find(POOL_MSG) != j.end()) {
        podPoolInfo->msg = j.at(POOL_MSG).get<std::string>();
    }
    if (j.find("horizontal_pod_autoscaler_spec") != j.end() && j.at("horizontal_pod_autoscaler_spec").is_string()) {
        podPoolInfo->horizontalPodAutoscalerSpec = j.at("horizontal_pod_autoscaler_spec").get<std::string>();
    }
}

std::string PoolManager::TransToJsonFromPodPoolInfo(const std::shared_ptr<PodPoolInfo> &poolInfo)
{
    nlohmann::json val = nlohmann::json::object();
    val[ID_STR] = poolInfo->id;
    val[SIZE_STR] = poolInfo->size;
    val[MAX_SIZE_STR] = poolInfo->maxSize;
    val[REUSE] = poolInfo->reuse;
    val[SCALABLE] = poolInfo->scalable;
    val[GROUP] = poolInfo->group;
    val[READY_COUNT] = poolInfo->readyCount;
    val[POOL_STATUS] = poolInfo->status;
    val[POOL_MSG] = poolInfo->msg;
    val[VOLUMES] = poolInfo->volumes;
    val[VOLUME_MOUNTS] = poolInfo->volumeMounts;
    val[AFFINITIES] = poolInfo->affinities;
    val[IMAGE] = poolInfo->image;
    val[INIT_IMAGE] = poolInfo->initImage;
    val[RUNTIME_CLASS_NAME] = poolInfo->runtimeClassName;
    val[NODE_SELECTOR] = functionsystem::kube_client::model::ModelUtils::ToJson(poolInfo->nodeSelectors);
    val[TOLERATIONS] = poolInfo->tolerations;
    val[ENVIRONMENTS] = functionsystem::kube_client::model::ModelUtils::ToJson(poolInfo->environments);
    val[POOL_LABELS] = functionsystem::kube_client::model::ModelUtils::ToJson(poolInfo->labels);
    val[RESOURCES] = functionsystem::kube_client::model::ModelUtils::ToJson(poolInfo->resources);
    val["horizontal_pod_autoscaler_spec"] = poolInfo->horizontalPodAutoscalerSpec;
    val[TOPOLOGY_SPREAD_CONSTRAINTS] =
        functionsystem::kube_client::model::ModelUtils::ToJson(poolInfo->topologySpreadConstraints);
    val[IDLE_RECYCLE_TIME] = { { RESERVED_STR, poolInfo->idleRecycleTime.reserved },
                               { SCALED_STR, poolInfo->idleRecycleTime.scaled } };
    return val.dump();
}

std::shared_ptr<PodPoolInfo> PoolManager::GetOrNewPool(const std::string &poolID)
{
    if (poolMap_.find(poolID) == poolMap_.end()) {
        auto pool = std::make_shared<PodPoolInfo>();
        pool->id = poolID;
        poolMap_.emplace(poolID, pool);
    }
    return poolMap_[poolID];
}

void PoolManager::PersistencePoolInfo(const std::string &poolID)
{
    if (poolMap_.find(poolID) == poolMap_.end() || poolMap_[poolID] == nullptr) {
        YRLOG_ERROR("update pod pool({}), pool is not found", poolID);
        return;
    }
    auto poolInfo = poolMap_[poolID];
    // when readyCount >= size, set status to Running
    if (poolInfo->scalable && poolInfo->readyCount >= poolInfo->size &&
        (CAN_BE_RUNNING_STATE.find(poolInfo->status) != CAN_BE_RUNNING_STATE.end())) {
        poolInfo->status = static_cast<int32_t>(PoolState::RUNNING);
        poolInfo->msg = "Running";
    }
    auto jsonStr = TransToJsonFromPodPoolInfo(poolInfo);
    YRLOG_INFO("update pod pool({}) status({}) readyCount({}) msg({})", poolID, poolInfo->status, poolInfo->readyCount,
               poolInfo->msg);
    (void)metaStorageAccessor_->Put(GenPodPoolKey(poolID).Get(), jsonStr);
}

void PoolManager::DeletePodPoolInfo(const std::string &poolID, const bool sync)
{
    if (poolMap_.find(poolID) == poolMap_.end()) {
        return;
    }
    YRLOG_INFO("delete pod pool({})", poolID);
    poolMap_.erase(poolID);
    if (sync) {
        metaStorageAccessor_->Delete(GenPodPoolKey(poolID).Get());
    }
}

std::shared_ptr<PodPoolInfo> PoolManager::GetPodPool(const std::string &poolID)
{
    if (poolMap_.find(poolID) == poolMap_.end()) {
        return nullptr;
    }
    return poolMap_[poolID];
}

void PoolManager::PutDeployment(const std::shared_ptr<V1Deployment> &deployment)
{
    deploymentsMap_[deployment->GetMetadata()->GetName()] = deployment;
}

void PoolManager::DeleteDeployment(const std::string &name)
{
    deploymentsMap_.erase(name);
}

std::shared_ptr<V1Deployment> PoolManager::GetDeployment(const std::string &name)
{
    if (deploymentsMap_.find(name) == deploymentsMap_.end()) {
        return nullptr;
    }
    return deploymentsMap_[name];
}

bool PoolManager::ValidatePodPoolCreateParams(const std::shared_ptr<PodPoolInfo>& podPool)
{
    // PodId
    bool isMatch = std::regex_match(podPool->id, ID_REGEX);
    if (!isMatch) {
        YRLOG_ERROR("failed to create pod pool, id: {}, err: {}", podPool->id,
            "pod pool id can contain only lowercase letters, digits and '-'. it cannot start or end with '-' and "
            "cannot exceed 40 characters or less than 1 characters");
        return false;
    }
    // PodGroup
    if (podPool->group.empty()) {
        podPool->group = "default";
    }
    isMatch = std::regex_match(podPool->group, GROUP_REGEX);
    if (!isMatch) {
        YRLOG_ERROR("failed to create pod pool, id: {}, group: {}, err: pod pool group can contain only letters, "
                    "digits and '-'. it cannot start or end with '-' and cannot exceed 40 characters",
                    podPool->id, podPool->group);
        return false;
    }
    // Size
    if (podPool->size < MIN_SIZE) {
        YRLOG_ERROR("failed to create pod pool, id: {}, err: size is less than 0", podPool->id);
        return false;
    }
    // MaxSize
    if (podPool->size != 0 && podPool->maxSize == 0) {
        podPool->maxSize = podPool->size;
    }
    if (podPool->maxSize < podPool->size) {
        YRLOG_ERROR("failed to create pod pool, id: {}, err: max_size is less than size", podPool->id);
        return false;
    }
    // HorizontalPodAutoscalerSpec
    if (podPool->maxSize > podPool->size && !podPool->horizontalPodAutoscalerSpec.empty()) {
        YRLOG_ERROR("failed to create pod pool, id: {}, err: max_size greater than size and "
                    "horizontal_pod_autoscaler_spec cannot be set at the same time", podPool->id);
        return false;
    }
    // IdleRecycleTime.Reserved
    if (podPool->idleRecycleTime.reserved < MIN_IDLE_TIME) {
        YRLOG_ERROR("failed to create pod pool, id: {}, err: idle time of reserved should not less than {}",
            podPool->id, MIN_IDLE_TIME);
        return false;
    }
    // IdleRecycleTime.Scaled
    if (podPool->idleRecycleTime.scaled < MIN_IDLE_TIME) {
        YRLOG_ERROR("failed to create pod pool, id: {}, err: idle time of scaled should not less than {}",
            podPool->id, MIN_IDLE_TIME);
        return false;
    }
    // Image
    if (podPool->image.length() > MAX_IMAGE_LEN) {
        YRLOG_ERROR("failed to create pod pool, id: {}, err: image len is not in [1, 200])", podPool->id);
        return false;
    }
    // InitImage
    if (podPool->initImage.length() > MAX_INIT_IMAGE_LEN) {
        YRLOG_ERROR("failed to create pod pool, id: {}, err: init image len is not in [1, 200]", podPool->id);
        return false;
    }
    // RuntimeClassName
    if (podPool->runtimeClassName.length() > MAX_RUNTIME_CLASS_LEN) {
        YRLOG_ERROR("failed to create pod pool, id: {}, err: runtime_class_name should not greater than {}",
            podPool->id, MAX_RUNTIME_CLASS_LEN);
        return false;
    }
    // PodPendingDurationThreshold
    if (podPool->podPendingDurationThreshold < 0) {
        YRLOG_ERROR("failed to create pod pool, id: {}, err: podPendingDurationThreshold should not be less than 0, "
                    "which is {}", podPool->id, podPool->podPendingDurationThreshold);
        return false;
    }
    podPool->scalable = podPool->maxSize > podPool->size;
    return true;
}

Status PoolManager::LoadPodPoolsConfig(const std::string &configFile)
{
    std::string jsonStr = Read(configFile);
    nlohmann::json confJson;
    try {
        confJson = nlohmann::json::parse(jsonStr);
    } catch (nlohmann::detail::parse_error &e) {
        YRLOG_ERROR("parse json failed, {}, error: {}", jsonStr, e.what());
        return Status(StatusCode::JSON_PARSE_ERROR, "parse json failed, " + jsonStr + ", error: " + e.what());
    }
    if (!confJson.contains(POOLS_STR) || !confJson[POOLS_STR].is_array()) {
        return Status(StatusCode::POINTER_IS_NULL);
    }
    for (auto& poolConfJson : confJson[POOLS_STR]) {
        std::string poolID = poolConfJson[ID_STR].get<std::string>();
        if (localPoolMap_.find(poolID) != localPoolMap_.end()) {
            YRLOG_WARN("load local pool({}), already exists skip it", poolID);
            continue;
        }
        auto podPool = std::make_shared<PodPoolInfo>();
        podPool->id = poolID;
        TransToPoolInfoFromJson(poolConfJson.dump(), podPool);
        if (!ValidatePodPoolCreateParams(podPool)) {
            continue;
        }
        localPoolMap_[poolID] = podPool;
        loadedResourcePoolNames_->emplace(poolID);
        YRLOG_INFO("load local pool({})", poolID);
    }
    return Status::OK();
}

ResourcePool PoolManager::ParsePoolInfo(const nlohmann::json &parser)
{
    ResourcePool pool;
    if (parser.find(POOL_NAME) != parser.end()) {
        pool.name = parser.at(POOL_NAME);
    }
    if (parser.find(POOL_SIZE) != parser.end()) {
        pool.poolSize = parser.at(POOL_SIZE);
    }
    if (parser.find(POOL_REQUEST_CPU) != parser.end()) {
        pool.requestResources[CPU_RESOURCE] = parser.at(POOL_REQUEST_CPU);
    }
    if (parser.find(POOL_REQUEST_MEMORY) != parser.end()) {
        pool.requestResources[MEMORY_RESOURCE] = parser.at(POOL_REQUEST_MEMORY);
    }
    if (parser.find(POOL_LIMIT_CPU) != parser.end()) {
        pool.limitResources[CPU_RESOURCE] = parser.at(POOL_LIMIT_CPU);
    }
    if (parser.find(POOL_LIMIT_MEMORY) != parser.end()) {
        pool.limitResources[MEMORY_RESOURCE] = parser.at(POOL_LIMIT_MEMORY);
    }
    if (auto iter = parser.find(POOL_IS_REUSE); iter != parser.end()) {
        pool.isReuse = iter.value();
    }
    return pool;
}

std::shared_ptr<PodPoolInfo> PoolManager::GetPodPoolFromPod(const std::shared_ptr<V1Pod> &pod)
{
    if (pod->GetMetadata()->GetAnnotations().find(POD_POOL_ID) == pod->GetMetadata()->GetAnnotations().end()) {
        return nullptr;
    }
    auto poolID =  pod->GetMetadata()->GetAnnotations()[POD_POOL_ID];
    auto podPool = GetPodPool(poolID);
    if (podPool == nullptr || !podPool->scalable) {
        return nullptr;
    }
    return podPool;
}

void PoolManager::OnPodUpdate(const std::shared_ptr<V1Pod> &pod)
{
    // if pod is terminating, remove it from readyPodSet
    if (IsPodTerminating(pod)) {
        OnPodDelete(pod, false);
        return;
    }
    auto podPool = GetPodPoolFromPod(pod);
    if (podPool == nullptr) {
        return;
    }
    if (podPool->deletingPodSet.find(pod->GetMetadata()->GetName()) != podPool->deletingPodSet.end()) {
        OnPodDelete(pod, false);
        return;
    }
    auto oldStatus = podPool->status;
    auto oldReadyCount = podPool->readyCount;
    if (IsPodReady(pod)) {
        podPool->readyPodSet.emplace(pod->GetMetadata()->GetName());
        podPool->pendingCreatePodSet.erase(pod->GetMetadata()->GetName());
    } else {
        podPool->pendingCreatePodSet.emplace(pod->GetMetadata()->GetName());
    }
    if (static_cast<int32_t>(podPool->readyPodSet.size()) != podPool->readyCount) {
        podPool->readyCount = podPool->readyPodSet.size();
    }
    if (podPool->readyCount >= podPool->size) {
        podPool->status = static_cast<int32_t>(PoolState::RUNNING);
    }
    if (persistHandler_ != nullptr && (oldStatus != podPool->status || oldReadyCount != podPool->readyCount)) {
        persistHandler_(podPool->id);
    }
    if (scaleUpHandler_ != nullptr) {
        scaleUpHandler_(podPool->id, true);
    }
}

void PoolManager::OnPodDelete(const std::shared_ptr<V1Pod> &pod, bool isSync)
{
    auto podPool = GetPodPoolFromPod(pod);
    if (podPool == nullptr) {
        return;
    }
    podPool->readyPodSet.erase(pod->GetMetadata()->GetName());
    podPool->pendingCreatePodSet.erase(pod->GetMetadata()->GetName());
    if (isSync) {
        // means pod is not exist anymore
        podPool->deletingPodSet.erase(pod->GetMetadata()->GetName());
    } else {
        // means pod is deleting
        podPool->deletingPodSet.emplace(pod->GetMetadata()->GetName());
    }
    auto oldStatus = podPool->status;
    auto oldReadyCount = podPool->readyCount;
    if (static_cast<int32_t>(podPool->readyPodSet.size()) != podPool->readyCount) {
        podPool->readyCount = static_cast<int32_t>(podPool->readyPodSet.size());
    }
    if (podPool->readyCount >= podPool->size) {
        podPool->status = static_cast<int32_t>(PoolState::RUNNING);
        podPool->msg = "Running";
    }
    if (persistHandler_ != nullptr && (oldStatus != podPool->status || oldReadyCount != podPool->readyCount)) {
        persistHandler_(podPool->id);
    }
    if (scaleUpHandler_ != nullptr) {
        scaleUpHandler_(podPool->id, true);
    }
}

litebus::Option<std::shared_ptr<V1Pod>> PoolManager::GenerateNewPodForPodPool(
    const std::shared_ptr<PodPoolInfo> &podPool, bool isReserved)
{
    auto deploymentName = POOL_NAME_PREFIX + podPool->id;
    auto templateDeployment = GetDeployment(deploymentName);
    if (templateDeployment == nullptr) {
        YRLOG_ERROR("failed to get deployment for pool {}", podPool->id);
        return litebus::None();
    }
    auto deployment = std::make_shared<V1Deployment>();
    // deep copy from template
    if (!deployment->FromJson(templateDeployment->ToJson())) {
        YRLOG_WARN("failed to decode from template deployment.");
    }
    auto rt = deployment->GetSpec()->GetRTemplate();
    auto uuid = litebus::uuid_generator::UUID::GetRandomUUID();
    auto splits = litebus::strings::Split(uuid.ToString(), "-");
    std::string uuidSuffix = splits[0] + splits[splits.size() - 1];
    auto podName = deploymentName + "-" + uuidSuffix;
    if (podName.size() > POD_NAME_MAX_LENGTH) {
        podName = podName.substr(0, POD_NAME_MAX_LENGTH);
    }
    auto pod = std::make_shared<V1Pod>();
    pod->SetSpec(rt->GetSpec());
    pod->SetMetadata(rt->GetMetadata());
    pod->GetMetadata()->SetName(podName);
    if (!pod->GetMetadata()->AnnotationsIsSet()) {
        pod->GetMetadata()->SetAnnotations({});
    }
    if (!pod->GetMetadata()->LabelsIsSet()) {
        pod->GetMetadata()->SetLabels({});
    }
    auto &podLabels = pod->GetMetadata()->GetLabels();
    auto recycleTime = isReserved ? podPool->idleRecycleTime.reserved : podPool->idleRecycleTime.scaled;
    // if recycleTime is -1, means pod will not be deleted
    if (recycleTime < 0) {
        podLabels[LABEL_IDLE_TO_RECYCLE] = UNLIMITED;
    } else if (recycleTime > 0) {
        podLabels[LABEL_IDLE_TO_RECYCLE] = std::to_string(recycleTime);
    }
    // ignore `reuse`
    podLabels.erase(REUSE_LABEL_KEY);
    auto &podAnnotations = pod->GetMetadata()->GetAnnotations();
    // add the annotation to pod to distinguish identify poolID of pod
    podAnnotations[POD_POOL_ID] = podPool->id;
    InitOwnerReference(templateDeployment, pod);
    return pod;
}

void PoolManager::InitOwnerReference(const std::shared_ptr<V1Deployment> deployment, const std::shared_ptr<V1Pod> pod)
{
    auto ownerReference = std::make_shared<V1OwnerReference>();
    ownerReference->SetName(deployment->GetMetadata()->GetName());
    ownerReference->SetController(true);
    ownerReference->SetBlockOwnerDeletion(true);
    ownerReference->SetKind("Deployment");
    ownerReference->SetApiVersion("apps/v1");
    ownerReference->SetUid(deployment->GetMetadata()->GetUid());
    pod->GetMetadata()->SetOwnerReferences({ ownerReference });
}

litebus::Option<std::shared_ptr<V1Pod>> PoolManager::TryScaleUpPod(const std::string &poolID, bool isReserved)
{
    auto podPool = GetPodPool(poolID);
    if (podPool == nullptr || !podPool->scalable) {
        return litebus::None();
    }
    if (podPool->status == static_cast<int32_t>(PoolState::NEW) ||
        podPool->status == static_cast<int32_t>(PoolState::FAILED) ||
        podPool->status == static_cast<int32_t>(PoolState::DELETED)) {
        return litebus::None();
    }
    auto currentNum = static_cast<int32_t>(podPool->pendingCreatePodSet.size() + podPool->readyPodSet.size());
    if (currentNum >= podPool->maxSize) {
        // reach max pod count, no need to scale
        return litebus::None();
    }
    if (isReserved && currentNum >= podPool->size) {
        // pod count reach min pod size, no need to scale reserved pod
        return litebus::None();
    }
    auto podInfo = GenerateNewPodForPodPool(podPool, currentNum < podPool->size);
    if (podInfo.IsSome()) {
        podPool->pendingCreatePodSet.emplace(podInfo.Get()->GetMetadata()->GetName());
    }
    return podInfo;
}
}
