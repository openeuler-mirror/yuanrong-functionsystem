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

#include "common/kube_client/model/pod/v1_topology_spread_constraint.h"

namespace functionsystem::kube_client {
namespace model {
REGISTER_MODEL_OBJECT(V1TopologySpreadConstraint);

V1TopologySpreadConstraint::V1TopologySpreadConstraint()
{
    m_labelSelectorIsSet = false;
    m_matchLabelKeysIsSet = false;
    m_maxSkew = 0;
    m_maxSkewIsSet = false;
    m_minDomains = 0;
    m_minDomainsIsSet = false;
    m_nodeAffinityPolicy = std::string("");
    m_nodeAffinityPolicyIsSet = false;
    m_nodeTaintsPolicy = std::string("");
    m_nodeTaintsPolicyIsSet = false;
    m_topologyKey = std::string("");
    m_topologyKeyIsSet = false;
    m_whenUnsatisfiable = std::string("");
    m_whenUnsatisfiableIsSet = false;
}

V1TopologySpreadConstraint::~V1TopologySpreadConstraint()
{
}

nlohmann::json V1TopologySpreadConstraint::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    if (m_labelSelectorIsSet) {
        val["labelSelector"] = ModelUtils::ToJson(m_labelSelector);
    }
    if (m_matchLabelKeysIsSet) {
        val["matchLabelKeys"] = ModelUtils::ToJson(m_matchLabelKeys);
    }
    if (m_maxSkewIsSet) {
        val["maxSkew"] = ModelUtils::ToJson(m_maxSkew);
    }
    if (m_minDomainsIsSet) {
        val["minDomains"] = ModelUtils::ToJson(m_minDomains);
    }
    if (m_nodeAffinityPolicyIsSet) {
        val["nodeAffinityPolicy"] = ModelUtils::ToJson(m_nodeAffinityPolicy);
    }
    if (m_nodeTaintsPolicyIsSet) {
        val["nodeTaintsPolicy"] = ModelUtils::ToJson(m_nodeTaintsPolicy);
    }
    if (m_topologyKeyIsSet) {
        val["topologyKey"] = ModelUtils::ToJson(m_topologyKey);
    }
    if (m_whenUnsatisfiableIsSet) {
        val["whenUnsatisfiable"] = ModelUtils::ToJson(m_whenUnsatisfiable);
    }
    return val;
}

bool V1TopologySpreadConstraint::FromJson(const nlohmann::json &val)
{
    bool ok = true;
    if (ModelUtils::CheckHasKey(val, "labelSelector")) {
        std::shared_ptr<V1LabelSelector> refValSetLabelSelector;
        ok &= ModelUtils::FromJson(val.at("labelSelector"), refValSetLabelSelector);
        SetLabelSelector(refValSetLabelSelector);
    }
    if (ModelUtils::CheckHasKey(val, "matchLabelKeys")) {
        std::vector<std::string> refValSetMatchLabelKeys;
        ok &= ModelUtils::FromJson(val.at("matchLabelKeys"), refValSetMatchLabelKeys);
        SetMatchLabelKeys(refValSetMatchLabelKeys);
    }
    if (ModelUtils::CheckHasKey(val, "maxSkew")) {
        int32_t refValSetMaxSkew;
        ok &= ModelUtils::FromJson(val.at("maxSkew"), refValSetMaxSkew);
        SetMaxSkew(refValSetMaxSkew);
    }
    if (ModelUtils::CheckHasKey(val, "minDomains")) {
        int32_t refValSetMinDomains;
        ok &= ModelUtils::FromJson(val.at("minDomains"), refValSetMinDomains);
        SetMinDomains(refValSetMinDomains);
    }
    if (ModelUtils::CheckHasKey(val, "nodeAffinityPolicy")) {
        std::string refValSetNodeAffinityPolicy;
        ok &= ModelUtils::FromJson(val.at("nodeAffinityPolicy"), refValSetNodeAffinityPolicy);
        SetNodeAffinityPolicy(refValSetNodeAffinityPolicy);
    }
    if (ModelUtils::CheckHasKey(val, "nodeTaintsPolicy")) {
        std::string refValSetNodeTaintsPolicy;
        ok &= ModelUtils::FromJson(val.at("nodeTaintsPolicy"), refValSetNodeTaintsPolicy);
        SetNodeTaintsPolicy(refValSetNodeTaintsPolicy);
    }
    if (ModelUtils::CheckHasKey(val, "topologyKey")) {
        std::string refValSetTopologyKey;
        ok &= ModelUtils::FromJson(val.at("topologyKey"), refValSetTopologyKey);
        SetTopologyKey(refValSetTopologyKey);
    }
    if (ModelUtils::CheckHasKey(val, "whenUnsatisfiable")) {
        std::string refValSetWhenUnsatisfiable;
        ok &= ModelUtils::FromJson(val.at("whenUnsatisfiable"), refValSetWhenUnsatisfiable);
        SetWhenUnsatisfiable(refValSetWhenUnsatisfiable);
    }
    return ok;
}

std::shared_ptr<V1LabelSelector> V1TopologySpreadConstraint::GetLabelSelector() const
{
    return m_labelSelector;
}

void V1TopologySpreadConstraint::SetLabelSelector(const std::shared_ptr<V1LabelSelector> &value)
{
    m_labelSelector = value;
    m_labelSelectorIsSet = true;
}

bool V1TopologySpreadConstraint::LabelSelectorIsSet() const
{
    return m_labelSelectorIsSet;
}

void V1TopologySpreadConstraint::UnsetLabelSelector()
{
    m_labelSelectorIsSet = false;
}
std::vector<std::string>& V1TopologySpreadConstraint::GetMatchLabelKeys()
{
    return m_matchLabelKeys;
}

void V1TopologySpreadConstraint::SetMatchLabelKeys(const std::vector<std::string> &value)
{
    m_matchLabelKeys = value;
    m_matchLabelKeysIsSet = true;
}

bool V1TopologySpreadConstraint::MatchLabelKeysIsSet() const
{
    return m_matchLabelKeysIsSet;
}

void V1TopologySpreadConstraint::UnsetMatchLabelKeys()
{
    m_matchLabelKeysIsSet = false;
}
int32_t V1TopologySpreadConstraint::GetMaxSkew() const
{
    return m_maxSkew;
}

void V1TopologySpreadConstraint::SetMaxSkew(int32_t value)
{
    m_maxSkew = value;
    m_maxSkewIsSet = true;
}

bool V1TopologySpreadConstraint::MaxSkewIsSet() const
{
    return m_maxSkewIsSet;
}

void V1TopologySpreadConstraint::UnsetMaxSkew()
{
    m_maxSkewIsSet = false;
}
int32_t V1TopologySpreadConstraint::GetMinDomains() const
{
    return m_minDomains;
}

void V1TopologySpreadConstraint::SetMinDomains(int32_t value)
{
    m_minDomains = value;
    m_minDomainsIsSet = true;
}

bool V1TopologySpreadConstraint::MinDomainsIsSet() const
{
    return m_minDomainsIsSet;
}

void V1TopologySpreadConstraint::UnsetMinDomains()
{
    m_minDomainsIsSet = false;
}
std::string V1TopologySpreadConstraint::GetNodeAffinityPolicy() const
{
    return m_nodeAffinityPolicy;
}

void V1TopologySpreadConstraint::SetNodeAffinityPolicy(const std::string &value)
{
    m_nodeAffinityPolicy = value;
    m_nodeAffinityPolicyIsSet = true;
}

bool V1TopologySpreadConstraint::NodeAffinityPolicyIsSet() const
{
    return m_nodeAffinityPolicyIsSet;
}

void V1TopologySpreadConstraint::UnsetNodeAffinityPolicy()
{
    m_nodeAffinityPolicyIsSet = false;
}
std::string V1TopologySpreadConstraint::GetNodeTaintsPolicy() const
{
    return m_nodeTaintsPolicy;
}

void V1TopologySpreadConstraint::SetNodeTaintsPolicy(const std::string &value)
{
    m_nodeTaintsPolicy = value;
    m_nodeTaintsPolicyIsSet = true;
}

bool V1TopologySpreadConstraint::NodeTaintsPolicyIsSet() const
{
    return m_nodeTaintsPolicyIsSet;
}

void V1TopologySpreadConstraint::UnsetNodeTaintsPolicy()
{
    m_nodeTaintsPolicyIsSet = false;
}
std::string V1TopologySpreadConstraint::GetTopologyKey() const
{
    return m_topologyKey;
}

void V1TopologySpreadConstraint::SetTopologyKey(const std::string &value)
{
    m_topologyKey = value;
    m_topologyKeyIsSet = true;
}

bool V1TopologySpreadConstraint::TopologyKeyIsSet() const
{
    return m_topologyKeyIsSet;
}

void V1TopologySpreadConstraint::UnsetTopologyKey()
{
    m_topologyKeyIsSet = false;
}
std::string V1TopologySpreadConstraint::GetWhenUnsatisfiable() const
{
    return m_whenUnsatisfiable;
}

void V1TopologySpreadConstraint::SetWhenUnsatisfiable(const std::string &value)
{
    m_whenUnsatisfiable = value;
    m_whenUnsatisfiableIsSet = true;
}

bool V1TopologySpreadConstraint::WhenUnsatisfiableIsSet() const
{
    return m_whenUnsatisfiableIsSet;
}

void V1TopologySpreadConstraint::UnsetWhenUnsatisfiable()
{
    m_whenUnsatisfiableIsSet = false;
}
}
}

