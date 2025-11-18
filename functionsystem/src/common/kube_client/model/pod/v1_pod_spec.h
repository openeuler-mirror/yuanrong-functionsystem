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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1POD_SPEC_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1POD_SPEC_H

#include <map>
#include <string>
#include <vector>

#include "common/kube_client/model/common/model_base.h"
#include "common/kube_client/model/common/v1_host_alias.h"
#include "common/kube_client/model/common/v1_local_object_reference.h"
#include "common/kube_client/model/container/v1_container.h"
#include "v1_affinity.h"
#include "v1_pod_security_context.h"
#include "v1_toleration.h"
#include "v1_topology_spread_constraint.h"
#include "common/kube_client/model/volume/v1_volume.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_POD_SPEC = "V1PodSpec";

class V1PodSpec : public ModelBase {
public:
    V1PodSpec();
    ~V1PodSpec() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    bool ParseBaseFromJson(const nlohmann::json &json);
    bool ParseOtherFromJson(const nlohmann::json &json);

    std::shared_ptr<V1Affinity> GetAffinity() const;
    bool AffinityIsSet() const;
    void UnsetAffinity();
    void SetAffinity(const std::shared_ptr<V1Affinity> &value);

    bool IsAutomountServiceAccountToken() const;
    bool AutomountServiceAccountTokenIsSet() const;
    void UnsetAutomountServiceAccountToken();
    void SetAutomountServiceAccountToken(bool value);

    std::vector<std::shared_ptr<V1Container>> &GetContainers();
    bool ContainersIsSet() const;
    void UnsetContainers();
    void SetContainers(const std::vector<std::shared_ptr<V1Container>> &value);

    std::string GetDnsPolicy() const;
    bool DnsPolicyIsSet() const;
    void UnsetDnsPolicy();
    void SetDnsPolicy(const std::string &value);

    std::vector<std::shared_ptr<V1HostAlias>> &GetHostAliases();
    bool HostAliasesIsSet() const;
    void UnsetHostAliases();
    void SetHostAliases(const std::vector<std::shared_ptr<V1HostAlias>> &value);

    bool IsHostNetwork() const;
    bool HostNetworkIsSet() const;
    void UnsetHostNetwork();
    void SetHostNetwork(bool value);

    std::vector<std::shared_ptr<V1LocalObjectReference>> &GetImagePullSecrets();
    bool ImagePullSecretsIsSet() const;
    void UnsetImagePullSecrets();
    void SetImagePullSecrets(const std::vector<std::shared_ptr<V1LocalObjectReference>> &value);

    std::vector<std::shared_ptr<V1Container>> &GetInitContainers();
    bool InitContainersIsSet() const;
    void UnsetInitContainers();
    void SetInitContainers(const std::vector<std::shared_ptr<V1Container>> &value);

    std::map<std::string, std::string> &GetNodeSelector();
    bool NodeSelectorIsSet() const;
    void UnsetNodeSelector();
    void SetNodeSelector(const std::map<std::string, std::string> &value);

    std::string GetPriorityClassName() const;
    bool PriorityClassNameIsSet() const;
    void UnsetPriorityClassName();
    void SetPriorityClassName(const std::string &value);

    std::string GetRestartPolicy() const;
    bool RestartPolicyIsSet() const;
    void UnsetRestartPolicy();
    void SetRestartPolicy(const std::string &value);

    std::string GetSchedulerName() const;
    bool SchedulerNameIsSet() const;
    void UnsetSchedulerName();
    void SetSchedulerName(const std::string &value);

    std::shared_ptr<V1PodSecurityContext> GetSecurityContext() const;
    bool SecurityContextIsSet() const;
    void UnSetSecurityContext();
    void SetSecurityContext(const std::shared_ptr<V1PodSecurityContext> &value);

    std::string GetServiceAccountName() const;
    bool ServiceAccountNameIsSet() const;
    void UnSetServiceAccountName();
    void SetServiceAccountName(const std::string &value);

    int64_t GetTerminationGracePeriodSeconds() const;
    bool TerminationGracePeriodSecondsIsSet() const;
    void UnsetTerminationGracePeriodSeconds();
    void SetTerminationGracePeriodSeconds(int64_t value);

    std::vector<std::shared_ptr<V1Volume>> &GetVolumes();
    bool VolumesIsSet() const;
    void UnsetVolumes();
    void SetVolumes(const std::vector<std::shared_ptr<V1Volume>> &value);

    std::vector<std::shared_ptr<V1Toleration>> &GetTolerations();
    bool TolerationsIsSet() const;
    void UnsetTolerations();
    void SetTolerations(const std::vector<std::shared_ptr<V1Toleration>> &value);

    std::string GetNodeName() const;
    bool NodeNameIsSet() const;
    void UnsetNodeName();
    void SetNodeName(const std::string& value);

    bool IsHostPID() const;
    bool HostPIDIsSet() const;
    void UnsetHostPID();
    void SetHostPID(bool value);

    std::string GetRuntimeClassName() const;
    bool RuntimeClassNameIsSet() const;
    void UnsetRuntimeClassName();
    void SetRuntimeClassName(const std::string& value);

    std::vector<std::shared_ptr<V1TopologySpreadConstraint>> &GetTopologySpreadConstraints();
    bool TopologySpreadConstraintsIsSet() const;
    void UnsetTopologySpreadConstraints();
    void SetTopologySpreadConstraints(const std::vector<std::shared_ptr<V1TopologySpreadConstraint>> &value);

    nlohmann::json GetBasicJson() const;

protected:
    std::shared_ptr<V1Affinity> m_affinity;
    bool m_affinityIsSet;
    bool m_automountServiceAccountToken;
    bool m_automountServiceAccountTokenIsSet;
    std::vector<std::shared_ptr<V1Container>> m_containers;
    bool m_containersIsSet;
    std::string m_dnsPolicy;
    bool m_dnsPolicyIsSet;
    std::vector<std::shared_ptr<V1HostAlias>> m_hostAliases;
    bool m_hostAliasesIsSet;
    bool m_hostNetwork;
    bool m_hostNetworkIsSet;
    std::vector<std::shared_ptr<V1LocalObjectReference>> m_imagePullSecrets;
    bool m_imagePullSecretsIsSet;
    std::vector<std::shared_ptr<V1Container>> m_initContainers;
    bool m_initContainersIsSet;
    std::map<std::string, std::string> m_nodeSelector;
    bool m_nodeSelectorIsSet;
    std::string m_priorityClassName;
    bool m_priorityClassNameIsSet;
    std::string m_restartPolicy;
    bool m_restartPolicyIsSet;
    std::string m_schedulerName;
    bool m_schedulerNameIsSet;
    std::shared_ptr<V1PodSecurityContext> m_securityContext;
    bool m_securityContextIsSet;
    std::string m_serviceAccountName;
    bool m_serviceAccountNameIsSet;
    int64_t m_terminationGracePeriodSeconds;
    bool m_terminationGracePeriodSecondsIsSet;
    std::vector<std::shared_ptr<V1Volume>> m_volumes;
    bool m_volumesIsSet;
    std::vector<std::shared_ptr<V1Toleration>> m_tolerations;
    bool m_tolerationsIsSet;
    std::string m_nodeName;
    bool m_nodeNameIsSet;
    bool m_hostPID;
    bool m_hostPIDIsSet;
    std::string m_runtimeClassName;
    bool m_runtimeClassNameIsSet;
    std::vector<std::shared_ptr<V1TopologySpreadConstraint>> m_topologySpreadConstraints;
    bool m_topologySpreadConstraintsIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1POD_SPEC_H */
