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

#include "v1_pod_spec.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1PodSpec);

V1PodSpec::V1PodSpec()
{
    m_affinityIsSet = false;
    m_automountServiceAccountToken = false;
    m_automountServiceAccountTokenIsSet = false;
    m_containersIsSet = false;
    m_dnsPolicy = std::string("");
    m_dnsPolicyIsSet = false;
    m_hostAliasesIsSet = false;
    m_hostNetwork = false;
    m_hostNetworkIsSet = false;
    m_imagePullSecretsIsSet = false;
    m_initContainersIsSet = false;
    m_nodeSelectorIsSet = false;
    m_priorityClassName = std::string("");
    m_priorityClassNameIsSet = false;
    m_restartPolicy = std::string("");
    m_restartPolicyIsSet = false;
    m_schedulerName = std::string("");
    m_schedulerNameIsSet = false;
    m_securityContextIsSet = false;
    m_serviceAccountName = std::string("");
    m_serviceAccountNameIsSet = false;
    m_terminationGracePeriodSeconds = 0L;
    m_terminationGracePeriodSecondsIsSet = false;
    m_volumesIsSet = false;
    m_tolerationsIsSet = false;
    m_hostPID = false;
    m_hostPIDIsSet = false;
    m_nodeNameIsSet = false;
    m_runtimeClassNameIsSet = false;
    m_topologySpreadConstraintsIsSet = false;
}

V1PodSpec::~V1PodSpec()
{
}

nlohmann::json V1PodSpec::ToJson() const
{
    auto val = GetBasicJson();
    if (m_affinityIsSet) {
        val["affinity"] = ModelUtils::ToJson(m_affinity);
    }
    if (m_automountServiceAccountTokenIsSet) {
        val["automountServiceAccountToken"] = ModelUtils::ToJson(m_automountServiceAccountToken);
    }
    if (m_containersIsSet) {
        val["containers"] = ModelUtils::ToJson(m_containers);
    }
    if (m_dnsPolicyIsSet) {
        val["dnsPolicy"] = ModelUtils::ToJson(m_dnsPolicy);
    }
    if (m_hostAliasesIsSet) {
        val["hostAliases"] = ModelUtils::ToJson(m_hostAliases);
    }
    if (m_hostNetworkIsSet) {
        val["hostNetwork"] = ModelUtils::ToJson(m_hostNetwork);
    }
    if (m_imagePullSecretsIsSet) {
        val["imagePullSecrets"] = ModelUtils::ToJson(m_imagePullSecrets);
    }
    if (m_initContainersIsSet) {
        val["initContainers"] = ModelUtils::ToJson(m_initContainers);
    }
    if (m_hostPIDIsSet) {
        val["hostPID"] = ModelUtils::ToJson(m_hostPID);
    }
    if (m_securityContextIsSet) {
        val["securityContext"] = ModelUtils::ToJson(m_securityContext);
    }
    if (m_serviceAccountNameIsSet) {
        val["serviceAccountName"] = ModelUtils::ToJson(m_serviceAccountName);
    }
    if (m_terminationGracePeriodSecondsIsSet) {
        val["terminationGracePeriodSeconds"] = ModelUtils::ToJson(m_terminationGracePeriodSeconds);
    }
    if (m_volumesIsSet) {
        val["volumes"] = ModelUtils::ToJson(m_volumes);
    }
    if (m_tolerationsIsSet) {
        val["tolerations"] = ModelUtils::ToJson(m_tolerations);
    }
    if (m_nodeNameIsSet) {
        val["nodeName"] = ModelUtils::ToJson(m_nodeName);
    }
    return val;
}

nlohmann::json V1PodSpec::GetBasicJson() const
{
    nlohmann::json val = nlohmann::json::object();
    if (m_nodeSelectorIsSet) {
        val["nodeSelector"] = ModelUtils::ToJson(m_nodeSelector);
    }
    if (m_priorityClassNameIsSet) {
        val["priorityClassName"] = ModelUtils::ToJson(m_priorityClassName);
    }
    if (m_restartPolicyIsSet) {
        val["restartPolicy"] = ModelUtils::ToJson(m_restartPolicy);
    }
    if (m_schedulerNameIsSet) {
        val["schedulerName"] = ModelUtils::ToJson(m_schedulerName);
    }
    if (m_runtimeClassNameIsSet) {
        val["runtimeClassName"] = ModelUtils::ToJson(m_runtimeClassName);
    }
    if (m_topologySpreadConstraintsIsSet) {
        val["topologySpreadConstraints"] = ModelUtils::ToJson(m_topologySpreadConstraints);
    }
    return val;
}

bool V1PodSpec::FromJson(const nlohmann::json &val)
{
    bool ok = ParseBaseFromJson(val) && ParseOtherFromJson(val);
    if (ModelUtils::CheckHasKey(val, "containers")) {
        std::vector<std::shared_ptr<V1Container>> refValSetContainers;
        ok &= ModelUtils::FromJson(val.at("containers"), refValSetContainers);
        SetContainers(refValSetContainers);
    }
    if (ModelUtils::CheckHasKey(val, "dnsPolicy")) {
        std::string refValSetDnsPolicy;
        ok &= ModelUtils::FromJson(val.at("dnsPolicy"), refValSetDnsPolicy);
        SetDnsPolicy(refValSetDnsPolicy);
    }
    if (ModelUtils::CheckHasKey(val, "hostAliases")) {
        std::vector<std::shared_ptr<V1HostAlias>> refValSetHostAliases;
        ok &= ModelUtils::FromJson(val.at("hostAliases"), refValSetHostAliases);
        SetHostAliases(refValSetHostAliases);
    }
    if (ModelUtils::CheckHasKey(val, "nodeSelector")) {
        std::map<std::string, std::string> refValSetNodeSelector;
        ok &= ModelUtils::FromJson(val.at("nodeSelector"), refValSetNodeSelector);
        SetNodeSelector(refValSetNodeSelector);
    }
    if (ModelUtils::CheckHasKey(val, "schedulerName")) {
        std::string refValSetSchedulerName;
        ok &= ModelUtils::FromJson(val.at("schedulerName"), refValSetSchedulerName);
        SetSchedulerName(refValSetSchedulerName);
    }
    if (ModelUtils::CheckHasKey(val, "securityContext")) {
        std::shared_ptr<V1PodSecurityContext> refValSetSecurityContext;
        ok &= ModelUtils::FromJson(val.at("securityContext"), refValSetSecurityContext);
        SetSecurityContext(refValSetSecurityContext);
    }
    if (ModelUtils::CheckHasKey(val, "tolerations")) {
        std::vector<std::shared_ptr<V1Toleration>> refValSetTolerations;
        ok &= ModelUtils::FromJson(val.at("tolerations"), refValSetTolerations);
        SetTolerations(refValSetTolerations);
    }
    if (ModelUtils::CheckHasKey(val, "hostPID")) {
        bool refValSetHostPID;
        ok &= ModelUtils::FromJson(val.at("hostPID"), refValSetHostPID);
        SetHostPID(refValSetHostPID);
    }
    if (ModelUtils::CheckHasKey(val, "topologySpreadConstraints")) {
        std::vector<std::shared_ptr<V1TopologySpreadConstraint>> refValSetTopologySpreadConstraints;
        ok &= ModelUtils::FromJson(val.at("topologySpreadConstraints"), refValSetTopologySpreadConstraints);
        SetTopologySpreadConstraints(refValSetTopologySpreadConstraints);
    }
    return ok;
}

bool V1PodSpec::ParseOtherFromJson(const nlohmann::json &json)
{
    bool ok = true;
    if (json.contains("automountServiceAccountToken")) {
        const nlohmann::json &fieldValue = json.at("automountServiceAccountToken");
        if (!fieldValue.is_null()) {
            bool refValSetAutomountServiceAccountToken;
            ok &= ModelUtils::FromJson(fieldValue, refValSetAutomountServiceAccountToken);
            SetAutomountServiceAccountToken(refValSetAutomountServiceAccountToken);
        }
    }
    if (ModelUtils::CheckHasKey(json, "runtimeClassName")) {
        std::string runtimeClassName;
        ok = ok && ModelUtils::FromJson(json.at("runtimeClassName"), runtimeClassName);
        SetRuntimeClassName(runtimeClassName);
    }
    if (json.contains("serviceAccountName")) {
        const nlohmann::json &fieldValue = json.at("serviceAccountName");
        if (!fieldValue.is_null()) {
            std::string refValSetServiceAccountName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetServiceAccountName);
            SetServiceAccountName(refValSetServiceAccountName);
        }
    }
    if (ModelUtils::CheckHasKey(json, "terminationGracePeriodSeconds")) {
        int64_t refValSetTerminationGracePeriodSeconds;
        ok &= ModelUtils::FromJson(json.at("terminationGracePeriodSeconds"), refValSetTerminationGracePeriodSeconds);
        SetTerminationGracePeriodSeconds(refValSetTerminationGracePeriodSeconds);
    }
    if (json.contains("volumes")) {
        const nlohmann::json &fieldValue = json.at("volumes");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1Volume>> refValSetVolumes;
            ok &= ModelUtils::FromJson(fieldValue, refValSetVolumes);
            SetVolumes(refValSetVolumes);
        }
    }
    if (json.contains("nodeName")) {
        const nlohmann::json &fieldValue = json.at("nodeName");
        if (!fieldValue.is_null()) {
            std::string refValSetNodeName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetNodeName);
            SetNodeName(refValSetNodeName);
        }
    }
    return ok;
}

bool V1PodSpec::ParseBaseFromJson(const nlohmann::json &val)
{
    bool ok = true;
    if (val.contains("affinity")) {
        const nlohmann::json &fieldValue = val.at("affinity");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1Affinity> refValSetAffinity;
            ok &= ModelUtils::FromJson(fieldValue, refValSetAffinity);
            SetAffinity(refValSetAffinity);
        }
    }
    if (val.contains("hostNetwork")) {
        const nlohmann::json &fieldValue = val.at("hostNetwork");
        if (!fieldValue.is_null()) {
            bool refValSetHostNetwork;
            ok &= ModelUtils::FromJson(fieldValue, refValSetHostNetwork);
            SetHostNetwork(refValSetHostNetwork);
        }
    }
    if (val.contains("imagePullSecrets")) {
        const nlohmann::json &fieldValue = val.at("imagePullSecrets");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1LocalObjectReference>> refValSetImagePullSecrets;
            ok &= ModelUtils::FromJson(fieldValue, refValSetImagePullSecrets);
            SetImagePullSecrets(refValSetImagePullSecrets);
        }
    }
    if (val.contains("initContainers")) {
        const nlohmann::json &fieldValue = val.at("initContainers");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1Container>> refValSetInitContainers;
            ok &= ModelUtils::FromJson(fieldValue, refValSetInitContainers);
            SetInitContainers(refValSetInitContainers);
        }
    }
    if (val.contains("priorityClassName")) {
        const nlohmann::json &fieldValue = val.at("priorityClassName");
        if (!fieldValue.is_null()) {
            std::string refValSetPriorityClassName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPriorityClassName);
            SetPriorityClassName(refValSetPriorityClassName);
        }
    }
    if (val.contains("restartPolicy")) {
        const nlohmann::json &fieldValue = val.at("restartPolicy");
        if (!fieldValue.is_null()) {
            std::string refValSetRestartPolicy;
            ok &= ModelUtils::FromJson(fieldValue, refValSetRestartPolicy);
            SetRestartPolicy(refValSetRestartPolicy);
        }
    }
    return ok;
}

std::shared_ptr<V1Affinity> V1PodSpec::GetAffinity() const
{
    return m_affinity;
}

void V1PodSpec::SetAffinity(const std::shared_ptr<V1Affinity> &value)
{
    m_affinity = value;
    m_affinityIsSet = true;
}

bool V1PodSpec::AffinityIsSet() const
{
    return m_affinityIsSet;
}

void V1PodSpec::UnsetAffinity()
{
    m_affinityIsSet = false;
}
bool V1PodSpec::IsAutomountServiceAccountToken() const
{
    return m_automountServiceAccountToken;
}

void V1PodSpec::SetAutomountServiceAccountToken(bool value)
{
    m_automountServiceAccountToken = value;
    m_automountServiceAccountTokenIsSet = true;
}

bool V1PodSpec::AutomountServiceAccountTokenIsSet() const
{
    return m_automountServiceAccountTokenIsSet;
}

void V1PodSpec::UnsetAutomountServiceAccountToken()
{
    m_automountServiceAccountTokenIsSet = false;
}
std::vector<std::shared_ptr<V1Container>> &V1PodSpec::GetContainers()
{
    return m_containers;
}

void V1PodSpec::SetContainers(const std::vector<std::shared_ptr<V1Container>> &value)
{
    m_containers = value;
    m_containersIsSet = true;
}

bool V1PodSpec::ContainersIsSet() const
{
    return m_containersIsSet;
}

void V1PodSpec::UnsetContainers()
{
    m_containersIsSet = false;
}
std::string V1PodSpec::GetDnsPolicy() const
{
    return m_dnsPolicy;
}

void V1PodSpec::SetDnsPolicy(const std::string &value)
{
    m_dnsPolicy = value;
    m_dnsPolicyIsSet = true;
}

bool V1PodSpec::DnsPolicyIsSet() const
{
    return m_dnsPolicyIsSet;
}

void V1PodSpec::UnsetDnsPolicy()
{
    m_dnsPolicyIsSet = false;
}

std::vector<std::shared_ptr<V1HostAlias>> &V1PodSpec::GetHostAliases()
{
    return m_hostAliases;
}

void V1PodSpec::SetHostAliases(const std::vector<std::shared_ptr<V1HostAlias>> &value)
{
    m_hostAliases = value;
    m_hostAliasesIsSet = true;
}

bool V1PodSpec::HostAliasesIsSet() const
{
    return m_hostAliasesIsSet;
}

void V1PodSpec::UnsetHostAliases()
{
    m_hostAliasesIsSet = false;
}

bool V1PodSpec::IsHostNetwork() const
{
    return m_hostNetwork;
}

void V1PodSpec::SetHostNetwork(bool value)
{
    m_hostNetwork = value;
    m_hostNetworkIsSet = true;
}

bool V1PodSpec::HostNetworkIsSet() const
{
    return m_hostNetworkIsSet;
}

void V1PodSpec::UnsetHostNetwork()
{
    m_hostNetworkIsSet = false;
}

std::vector<std::shared_ptr<V1LocalObjectReference>> &V1PodSpec::GetImagePullSecrets()
{
    return m_imagePullSecrets;
}

void V1PodSpec::SetImagePullSecrets(const std::vector<std::shared_ptr<V1LocalObjectReference>> &value)
{
    m_imagePullSecrets = value;
    m_imagePullSecretsIsSet = true;
}

bool V1PodSpec::ImagePullSecretsIsSet() const
{
    return m_imagePullSecretsIsSet;
}

void V1PodSpec::UnsetImagePullSecrets()
{
    m_imagePullSecretsIsSet = false;
}

std::vector<std::shared_ptr<V1Container>> &V1PodSpec::GetInitContainers()
{
    return m_initContainers;
}

void V1PodSpec::SetInitContainers(const std::vector<std::shared_ptr<V1Container>> &value)
{
    m_initContainers = value;
    m_initContainersIsSet = true;
}

bool V1PodSpec::InitContainersIsSet() const
{
    return m_initContainersIsSet;
}

void V1PodSpec::UnsetInitContainers()
{
    m_initContainersIsSet = false;
}

std::map<std::string, std::string> &V1PodSpec::GetNodeSelector()
{
    return m_nodeSelector;
}

void V1PodSpec::SetNodeSelector(const std::map<std::string, std::string> &value)
{
    m_nodeSelector = value;
    m_nodeSelectorIsSet = true;
}

bool V1PodSpec::NodeSelectorIsSet() const
{
    return m_nodeSelectorIsSet;
}

void V1PodSpec::UnsetNodeSelector()
{
    m_nodeSelectorIsSet = false;
}

std::string V1PodSpec::GetPriorityClassName() const
{
    return m_priorityClassName;
}

void V1PodSpec::SetPriorityClassName(const std::string &value)
{
    m_priorityClassName = value;
    m_priorityClassNameIsSet = true;
}

bool V1PodSpec::PriorityClassNameIsSet() const
{
    return m_priorityClassNameIsSet;
}

void V1PodSpec::UnsetPriorityClassName()
{
    m_priorityClassNameIsSet = false;
}
std::string V1PodSpec::GetRestartPolicy() const
{
    return m_restartPolicy;
}

void V1PodSpec::SetRestartPolicy(const std::string &value)
{
    m_restartPolicy = value;
    m_restartPolicyIsSet = true;
}

bool V1PodSpec::RestartPolicyIsSet() const
{
    return m_restartPolicyIsSet;
}

void V1PodSpec::UnsetRestartPolicy()
{
    m_restartPolicyIsSet = false;
}

std::string V1PodSpec::GetSchedulerName() const
{
    return m_schedulerName;
}

void V1PodSpec::SetSchedulerName(const std::string &value)
{
    m_schedulerName = value;
    m_schedulerNameIsSet = true;
}

bool V1PodSpec::SchedulerNameIsSet() const
{
    return m_schedulerNameIsSet;
}

void V1PodSpec::UnsetSchedulerName()
{
    m_schedulerNameIsSet = false;
}
std::shared_ptr<V1PodSecurityContext> V1PodSpec::GetSecurityContext() const
{
    return m_securityContext;
}

void V1PodSpec::SetSecurityContext(const std::shared_ptr<V1PodSecurityContext> &value)
{
    m_securityContext = value;
    m_securityContextIsSet = true;
}

bool V1PodSpec::SecurityContextIsSet() const
{
    return m_securityContextIsSet;
}

void V1PodSpec::UnSetSecurityContext()
{
    m_securityContextIsSet = false;
}

std::string V1PodSpec::GetServiceAccountName() const
{
    return m_serviceAccountName;
}

void V1PodSpec::SetServiceAccountName(const std::string &value)
{
    m_serviceAccountName = value;
    m_serviceAccountNameIsSet = true;
}

bool V1PodSpec::ServiceAccountNameIsSet() const
{
    return m_serviceAccountNameIsSet;
}

void V1PodSpec::UnSetServiceAccountName()
{
    m_serviceAccountNameIsSet = false;
}

int64_t V1PodSpec::GetTerminationGracePeriodSeconds() const
{
    return m_terminationGracePeriodSeconds;
}

void V1PodSpec::SetTerminationGracePeriodSeconds(int64_t value)
{
    m_terminationGracePeriodSeconds = value;
    m_terminationGracePeriodSecondsIsSet = true;
}

bool V1PodSpec::TerminationGracePeriodSecondsIsSet() const
{
    return m_terminationGracePeriodSecondsIsSet;
}

void V1PodSpec::UnsetTerminationGracePeriodSeconds()
{
    m_terminationGracePeriodSecondsIsSet = false;
}
std::vector<std::shared_ptr<V1Volume>> &V1PodSpec::GetVolumes()
{
    return m_volumes;
}

void V1PodSpec::SetVolumes(const std::vector<std::shared_ptr<V1Volume>> &value)
{
    m_volumes = value;
    m_volumesIsSet = true;
}

bool V1PodSpec::VolumesIsSet() const
{
    return m_volumesIsSet;
}

void V1PodSpec::UnsetVolumes()
{
    m_volumesIsSet = false;
}

std::vector<std::shared_ptr<V1Toleration>> &V1PodSpec::GetTolerations()
{
    return m_tolerations;
}

void V1PodSpec::SetTolerations(const std::vector<std::shared_ptr<V1Toleration>> &value)
{
    m_tolerations = value;
    m_tolerationsIsSet = true;
}

bool V1PodSpec::TolerationsIsSet() const
{
    return m_tolerationsIsSet;
}

void V1PodSpec::UnsetTolerations()
{
    m_tolerationsIsSet = false;
}

std::string V1PodSpec::GetNodeName() const
{
    return m_nodeName;
}

void V1PodSpec::SetNodeName(const std::string& value)
{
    m_nodeName = value;
    m_nodeNameIsSet = true;
}

bool V1PodSpec::NodeNameIsSet() const
{
    return m_nodeNameIsSet;
}

void V1PodSpec::UnsetNodeName()
{
    m_nodeNameIsSet = false;
}
bool V1PodSpec::IsHostPID() const
{
    return m_hostPID;
}

void V1PodSpec::SetHostPID(bool value)
{
    m_hostPID = value;
    m_hostPIDIsSet = true;
}

bool V1PodSpec::HostPIDIsSet() const
{
    return m_hostPIDIsSet;
}

void V1PodSpec::UnsetHostPID()
{
    m_hostPIDIsSet = false;
}
std::string V1PodSpec::GetRuntimeClassName() const
{
    return m_runtimeClassName;
}

void V1PodSpec::SetRuntimeClassName(const std::string& value)
{
    m_runtimeClassName = value;
    m_runtimeClassNameIsSet = true;
}

bool V1PodSpec::RuntimeClassNameIsSet() const
{
    return m_runtimeClassNameIsSet;
}
void V1PodSpec::UnsetRuntimeClassName()
{
    m_runtimeClassNameIsSet = false;
}
std::vector<std::shared_ptr<V1TopologySpreadConstraint>> &V1PodSpec::GetTopologySpreadConstraints()
{
    return m_topologySpreadConstraints;
}
void V1PodSpec::SetTopologySpreadConstraints(const std::vector<std::shared_ptr<V1TopologySpreadConstraint>> &value)
{
    m_topologySpreadConstraints = value;
    m_topologySpreadConstraintsIsSet = true;
}

bool V1PodSpec::TopologySpreadConstraintsIsSet() const
{
    return m_topologySpreadConstraintsIsSet;
}

void V1PodSpec::UnsetTopologySpreadConstraints()
{
    m_topologySpreadConstraintsIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
