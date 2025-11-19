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

#ifndef FUNCTION_MASTER_SCALER_UTILS_PARSE_HELPER_H
#define FUNCTION_MASTER_SCALER_UTILS_PARSE_HELPER_H

#include <nlohmann/json.hpp>

#include "async/option.hpp"
#include "common/logs/logging.h"
#include "common/proto/pb/posix/resource.pb.h"
#include "common/resource_view/resource_type.h"
#include "common/kube_client/model/common/model_utils.h"
#include "common/kube_client/model/deployment/v1_deployment.h"
#include "common/kube_client/model/node/v1_node.h"
#include "common/kube_client/model/node/v1_node_affinity.h"
#include "common/kube_client/model/pod/v1_pod.h"
#include "common/kube_client/model/volume/v1_projected_volume_source.h"
#include "common/kube_client/model/horizontal_pod_autoscaler/v2_horizontal_pod_autoscaler.h"
#include "common/kube_client/model/common/v1_object_meta.h"
#include "common/kube_client/model/common/v2_cross_version_object_reference.h"

namespace functionsystem {

using V1Container = functionsystem::kube_client::model::V1Container;
using V1SecurityContext = functionsystem::kube_client::model::V1SecurityContext;
// Pod
using V1Pod = functionsystem::kube_client::model::V1Pod;
using V1PodTemplateSpec = functionsystem::kube_client::model::V1PodTemplateSpec;
using V1TopologySpreadConstraint = functionsystem::kube_client::model::V1TopologySpreadConstraint;
// Env
using V1EnvVar = functionsystem::kube_client::model::V1EnvVar;
using V1EnvVarSource = functionsystem::kube_client::model::V1EnvVarSource;
using V1ObjectFieldSelector = functionsystem::kube_client::model::V1ObjectFieldSelector;
using V1ResourceFieldSelector = functionsystem::kube_client::model::V1ResourceFieldSelector;
using V1SecretKeySelector = functionsystem::kube_client::model::V1SecretKeySelector;
// VolumeMount
using V1VolumeMount = functionsystem::kube_client::model::V1VolumeMount;
// Resource
using V1ResourceRequirements = functionsystem::kube_client::model::V1ResourceRequirements;
// Lifecycle
using V1Lifecycle = functionsystem::kube_client::model::V1Lifecycle;
using V1LifecycleHandler = functionsystem::kube_client::model::V1LifecycleHandler;
using V1ExecAction = functionsystem::kube_client::model::V1ExecAction;
// Volume
using V1Volume = functionsystem::kube_client::model::V1Volume;
using V1HostPathVolumeSource = functionsystem::kube_client::model::V1HostPathVolumeSource;
using V1ConfigMapVolumeSource = functionsystem::kube_client::model::V1ConfigMapVolumeSource;
using V1EmptyDirVolumeSource = functionsystem::kube_client::model::V1EmptyDirVolumeSource;
using V1SecretVolumeSource = functionsystem::kube_client::model::V1SecretVolumeSource;
using V1ProjectedVolumeSource = functionsystem::kube_client::model::V1ProjectedVolumeSource;
using V1PersistentVolumeClaimVolumeSource = functionsystem::kube_client::model::V1PersistentVolumeClaimVolumeSource;
using V1KeyToPath = functionsystem::kube_client::model::V1KeyToPath;
using V1HostAlias = functionsystem::kube_client::model::V1HostAlias;
using V1PodSpec = functionsystem::kube_client::model::V1PodSpec;
using V1Toleration = functionsystem::kube_client::model::V1Toleration;
// Node
using V1Node = functionsystem::kube_client::model::V1Node;
using V1Affinity = functionsystem::kube_client::model::V1Affinity;
using V1NodeAffinity = functionsystem::kube_client::model::V1NodeAffinity;
using V1SeccompProfile = functionsystem::kube_client::model::V1SeccompProfile;
using V1Affinity = functionsystem::kube_client::model::V1Affinity;
using V1PodAffinity = functionsystem::kube_client::model::V1PodAffinity;
using V1PodAntiAffinity = functionsystem::kube_client::model::V1PodAntiAffinity;
using V1WeightedPodAffinityTerm = functionsystem::kube_client::model::V1WeightedPodAffinityTerm;
using V1PodAffinityTerm = functionsystem::kube_client::model::V1PodAffinityTerm;
using V1LabelSelector = functionsystem::kube_client::model::V1LabelSelector;
// HPA
using V2HorizontalPodAutoscaler = functionsystem::kube_client::model::V2HorizontalPodAutoscaler;
using V2HorizontalPodAutoscalerSpec = functionsystem::kube_client::model::V2HorizontalPodAutoscalerSpec;
// common
using V1ObjectMeta = functionsystem::kube_client::model::V1ObjectMeta;
using V2CrossVersionObjectReference = functionsystem::kube_client::model::V2CrossVersionObjectReference;

// Probe
using V1Probe = functionsystem::kube_client::model::V1Probe;

const int32_t INVALID_ID = -1;

enum class PodPhase {
    PENDING,
    RUNNING,
    SUCCEEDED,
    FAILED,
    UNKNOWN,
};

void ParseDelegateContainer(const ::resources::InstanceInfo &instanceInfo,
                            litebus::Option<std::shared_ptr<V1Container>> &delegateContainer);
void ParseDelegateRuntimeManager(const ::resources::InstanceInfo &instanceInfo,
                                 litebus::Option<std::shared_ptr<V1Container>> &delegateRuntimeManager);
void ParseDelegateSidecars(const ::resources::InstanceInfo &instanceInfo,
                           std::vector<std::shared_ptr<V1Container>> &delegateSidecars);
void ParseDelegateInitContainers(const ::resources::InstanceInfo &instanceInfo,
                                 std::vector<std::shared_ptr<V1Container>> &delegateInitContainers);
void ParseEnvValueFrom(const nlohmann::json &parser, const std::shared_ptr<V1EnvVar> &envVar);
void ParseVolumeMounts(const nlohmann::json &parser, std::vector<std::shared_ptr<V1VolumeMount>> &volumeMounts,
                       bool needFilterSensitiveVolume = false);
void ParseDelegateVolumes(const ::resources::InstanceInfo &instanceInfo,
                          std::vector<std::shared_ptr<V1Volume>> &volumes);
void ParseVolumesFromJson(const std::string jsonStr, std::vector<std::shared_ptr<V1Volume>> &volumes);
void ParseDelegateVolumeMounts(const ::resources::InstanceInfo &instanceInfo, const std::string optionKey,
                               std::vector<std::shared_ptr<V1VolumeMount>> &volumeMounts);
void ParseVolumeMountsFromJson(const std::string jsonStr, std::vector<std::shared_ptr<V1VolumeMount>> &volumeMounts);
void ParseDelegateDecryptEnv(const ::resources::InstanceInfo &instanceInfo,
                             std::vector<std::shared_ptr<V1EnvVar>> &delegateDecryptEnvs);
void ParseDelegateHostAliases(const ::resources::InstanceInfo &instanceInfo,
                              std::vector<std::shared_ptr<V1HostAlias>> &delegateHostAliases);

void ParseDelegateTolerations(const ::resources::InstanceInfo &instanceInfo,
                              std::vector<std::shared_ptr<V1Toleration>> &delegateTolerations);

void ParseDelegateTolerationsFromStr(const std::string &val,
                                     std::vector<std::shared_ptr<V1Toleration>> &delegateTolerations);

void ParseDelegateInitEnv(const ::resources::InstanceInfo &instanceInfo,
                          std::vector<std::shared_ptr<V1EnvVar>> &delegateInitEnvs);
void ParseAffinityFromStr(const std::string &val, const std::shared_ptr<V1Affinity> &affinity);
void ParseAffinityFromCreateOpts(const ::resources::InstanceInfo &instanceInfo,
                                 const std::shared_ptr<V1Affinity> &affinity);
void ParseNodeAffinity(const ::resources::InstanceInfo &instanceInfo, const std::shared_ptr<V1Affinity> &affinity);
std::shared_ptr<V1Affinity> ParseAffinity(
    const std::unordered_map<resource_view::AffinityType, std::vector<std::string>> &affinityTypeLabels);
std::shared_ptr<V1WeightedPodAffinityTerm> CreateWeightedPodAffinityTerm(const std::string &label);
std::shared_ptr<V1PodAffinityTerm> CreatePodAffinityTerm(const std::string &label);
std::shared_ptr<V1LabelSelector> CreateLabelSelector(const std::string &label);
std::unordered_map<resource_view::AffinityType, std::vector<std::string>> GetAffinityInfo(
    const ::resources::InstanceInfo &instanceInfo);
std::map<std::string, std::string> GetPodLabelsForCreate(const ::resources::InstanceInfo &instanceInfo);
std::shared_ptr<V1SeccompProfile> ParseSeccompProfile(const ::resources::InstanceInfo &instanceInfo);

std::map<std::string, std::string> ParseDelegateAnnotations(const ::resources::InstanceInfo &instanceInfo);

std::vector<std::shared_ptr<V1EnvVar>> ParseContainerEnv(const nlohmann::json &parser);
std::vector<std::shared_ptr<V1EnvVar>> ParseContainerEnvFromMap(const std::map<std::string, std::string> &environments);
std::shared_ptr<V1ResourceRequirements> ParseResourceRequirements(const nlohmann::json &parser);
std::shared_ptr<V1ExecAction> ParseExecAction(const nlohmann::json &parser);
std::shared_ptr<V1LifecycleHandler> ParseLifecycleHandler(const nlohmann::json &parser);
std::shared_ptr<V1Lifecycle> ParseLifecycle(const nlohmann::json &parser);
std::shared_ptr<V1Probe> ParseV1Probe(const nlohmann::json &parser);
void ParseContainerInfo(const nlohmann::json &parser, const std::shared_ptr<V1Container> &container);
litebus::Option<std::shared_ptr<V1Container>> ParseContainer(const nlohmann::json &parser);
bool ParseVolumeHostPath(const nlohmann::json &json, const std::shared_ptr<V1Volume> &volume);
bool ParseVolumeConfigMap(const nlohmann::json &json, const std::shared_ptr<V1Volume> &volume);
bool ParseVolumeEmptyDir(const nlohmann::json &json, std::shared_ptr<V1Volume> &volume);
bool ParseVolumeSecret(const nlohmann::json &json, const std::shared_ptr<V1Volume> &volume);
bool ParseVolumeProjected(const nlohmann::json &json, const std::shared_ptr<V1Volume> &volume);
bool ParseVolumePersistent(const nlohmann::json &json, const std::shared_ptr<V1Volume> &volume);
void SetHostAliases(const std::shared_ptr<V1PodSpec> &spec,
                    const std::vector<std::shared_ptr<V1HostAlias>> &delegateHostAliases);
bool IsSystemFunction(const ::resources::InstanceInfo &instanceInfo);
bool IsNeedBindCPU(const std::map<std::string, std::string> &resourcesMap);
std::string ParseNodeInternalIP(const std::shared_ptr<V1Node> &node);
std::map<std::string, std::string> ParseMapFromJson(const nlohmann::json &parser, const std::string &key);
std::map<std::string, std::string> ParseMapFromStr(const std::string &val);
std::string GetPodIPFromFunctionAgentID(const std::string &functionAgentID);
std::map<std::string, std::string> GetLabelsFromCreateOpt(const ::resources::InstanceInfo &instanceInfo);
std::string ParseCPUEnv(std::string &cpuRes);
std::string ParseMemoryEnv(std::string &memRes);
bool IsPodReady(const std::shared_ptr<V1Pod> &pod);
bool IsDsWorkerPodReady(const std::shared_ptr<V1Pod> &pod);
bool IsPendingPod(const std::shared_ptr<V1Pod> &pod);
bool IsPodTerminating(const std::shared_ptr<V1Pod> &pod);
bool IsAbnormalPod(const std::shared_ptr<V1Pod> &pod);
bool IsTimeoutPendingPod(const std::shared_ptr<V1Pod> &pod, const std::chrono::system_clock::time_point &receivedTime,
                         const int podPendingDurationThreshold);
void AddLabels(const ::resources::InstanceInfo &instanceInfo, std::map<std::string, std::string> &labels);
void HandlePodLabelsWithAnnotation(const ::resources::InstanceInfo &instanceInfo, const std::shared_ptr<V1Pod> &pod,
                                   bool isInstanceDelete);
void AddPodLabelsByInstance(const ::resources::InstanceInfo &instanceInfo, const std::shared_ptr<V1Pod> &pod);
void RemovePodLabelsByInstance(const ::resources::InstanceInfo &instanceInfo, const std::shared_ptr<V1Pod> &pod);
void RemovePodLabelsByAnnotations(const std::string instanceID, const std::shared_ptr<V1Pod> &pod);
std::shared_ptr<V2HorizontalPodAutoscalerSpec> ParseHorizontalPodAutoscaler(const std::string &jsonStr);
void AddAffinityForPodSpec(const std::shared_ptr<V1Affinity> affinity, const std::shared_ptr<V1PodTemplateSpec> &spec,
                           const bool isAggregation = false);
void ParseDelegateTopologySpreadConstraintsFromStr(
    const std::string &val,
    std::vector<std::shared_ptr<V1TopologySpreadConstraint>> &delegateTopologySpreadConstraints);
bool IsAggregationMergePolicy(const ::resources::InstanceInfo &instanceInfo);
std::shared_ptr<V1NodeAffinity> MergeNodeAffinity(const std::shared_ptr<V1NodeAffinity> &oldAff,
                                                  const std::shared_ptr<V1NodeAffinity> &newAff,
                                                  const bool isAggregation);
std::shared_ptr<kube_client::model::V1NodeSelector> AggregationNodeSelector(
    const std::shared_ptr<kube_client::model::V1NodeSelector> &oldSel,
    const std::shared_ptr<kube_client::model::V1NodeSelector> &newSel);
std::string TruncateIllegalLabel(const std::string& label);
}  // namespace functionsystem

#endif  // FUNCTION_MASTER_SCALER_UTILS_PARSE_HELPER_H