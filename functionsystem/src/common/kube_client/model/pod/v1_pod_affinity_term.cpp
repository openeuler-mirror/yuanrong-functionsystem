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

#include "v1_pod_affinity_term.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1PodAffinityTerm);

V1PodAffinityTerm::V1PodAffinityTerm()
{
    m_labelSelectorIsSet = false;
    m_namespaceSelectorIsSet = false;
    m_namespacesIsSet = false;
    m_topologyKey = std::string("");
    m_topologyKeyIsSet = false;
}

V1PodAffinityTerm::~V1PodAffinityTerm()
{
}

nlohmann::json V1PodAffinityTerm::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_labelSelectorIsSet) {
        val["labelSelector"] = ModelUtils::ToJson(m_labelSelector);
    }
    if (m_namespaceSelectorIsSet) {
        val["namespaceSelector"] = ModelUtils::ToJson(m_namespaceSelector);
    }
    if (m_namespacesIsSet) {
        val["namespaces"] = ModelUtils::ToJson(m_namespaces);
    }
    if (m_topologyKeyIsSet) {
        val["topologyKey"] = ModelUtils::ToJson(m_topologyKey);
    }

    return val;
}

bool V1PodAffinityTerm::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("labelSelector")) {
        const nlohmann::json &fieldValue = val.at("labelSelector");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1LabelSelector> refValSetLabelSelector;
            ok &= ModelUtils::FromJson(fieldValue, refValSetLabelSelector);
            SetLabelSelector(refValSetLabelSelector);
        }
    }
    if (val.contains("namespaceSelector")) {
        const nlohmann::json &fieldValue = val.at("namespaceSelector");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1LabelSelector> refValSetNamespaceSelector;
            ok &= ModelUtils::FromJson(fieldValue, refValSetNamespaceSelector);
            SetNamespaceSelector(refValSetNamespaceSelector);
        }
    }
    if (val.contains("namespaces")) {
        const nlohmann::json &fieldValue = val.at("namespaces");
        if (!fieldValue.is_null()) {
            std::vector<std::string> refValSetNamespaces;
            ok &= ModelUtils::FromJson(fieldValue, refValSetNamespaces);
            SetNamespaces(refValSetNamespaces);
        }
    }
    if (val.contains("topologyKey")) {
        const nlohmann::json &fieldValue = val.at("topologyKey");
        if (!fieldValue.is_null()) {
            std::string refValSetTopologyKey;
            ok &= ModelUtils::FromJson(fieldValue, refValSetTopologyKey);
            SetTopologyKey(refValSetTopologyKey);
        }
    }
    return ok;
}

std::shared_ptr<V1LabelSelector> V1PodAffinityTerm::GetLabelSelector() const
{
    return m_labelSelector;
}

void V1PodAffinityTerm::SetLabelSelector(const std::shared_ptr<V1LabelSelector> &value)
{
    m_labelSelector = value;
    m_labelSelectorIsSet = true;
}

bool V1PodAffinityTerm::LabelSelectorIsSet() const
{
    return m_labelSelectorIsSet;
}

void V1PodAffinityTerm::UnsetLabelSelector()
{
    m_labelSelectorIsSet = false;
}
std::shared_ptr<V1LabelSelector> V1PodAffinityTerm::GetNamespaceSelector() const
{
    return m_namespaceSelector;
}

void V1PodAffinityTerm::SetNamespaceSelector(const std::shared_ptr<V1LabelSelector> &value)
{
    m_namespaceSelector = value;
    m_namespaceSelectorIsSet = true;
}

bool V1PodAffinityTerm::NamespaceSelectorIsSet() const
{
    return m_namespaceSelectorIsSet;
}

void V1PodAffinityTerm::UnsetNamespaceSelector()
{
    m_namespaceSelectorIsSet = false;
}

std::vector<std::string> &V1PodAffinityTerm::GetNamespaces()
{
    return m_namespaces;
}

void V1PodAffinityTerm::SetNamespaces(const std::vector<std::string> &value)
{
    m_namespaces = value;
    m_namespacesIsSet = true;
}

bool V1PodAffinityTerm::NamespacesIsSet() const
{
    return m_namespacesIsSet;
}

void V1PodAffinityTerm::UnsetNamespaces()
{
    m_namespacesIsSet = false;
}
std::string V1PodAffinityTerm::GetTopologyKey() const
{
    return m_topologyKey;
}

void V1PodAffinityTerm::SetTopologyKey(const std::string &value)
{
    m_topologyKey = value;
    m_topologyKeyIsSet = true;
}

bool V1PodAffinityTerm::TopologyKeyIsSet() const
{
    return m_topologyKeyIsSet;
}

void V1PodAffinityTerm::UnsetTopologyKey()
{
    m_topologyKeyIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
