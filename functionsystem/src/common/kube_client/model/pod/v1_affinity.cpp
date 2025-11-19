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

#include "v1_affinity.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1Affinity);

V1Affinity::V1Affinity()
{
    m_podAffinityIsSet = false;
    m_podAntiAffinityIsSet = false;
    m_nodeAffinityIsSet = false;
}

V1Affinity::~V1Affinity()
{
}

nlohmann::json V1Affinity::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    if (m_podAffinityIsSet) {
        val["podAffinity"] = ModelUtils::ToJson(m_podAffinity);
    }
    if (m_podAntiAffinityIsSet) {
        val["podAntiAffinity"] = ModelUtils::ToJson(m_podAntiAffinity);
    }
    if (m_nodeAffinityIsSet) {
        val["nodeAffinity"] = ModelUtils::ToJson(m_nodeAffinity);
    }
    return val;
}

bool V1Affinity::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("podAffinity")) {
        const nlohmann::json &fieldValue = val.at("podAffinity");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1PodAffinity> refValSetPodAffinity;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPodAffinity);
            SetPodAffinity(refValSetPodAffinity);
        }
    }
    if (val.contains("podAntiAffinity")) {
        const nlohmann::json &fieldValue = val.at("podAntiAffinity");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1PodAntiAffinity> refValSetPodAntiAffinity;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPodAntiAffinity);
            SetPodAntiAffinity(refValSetPodAntiAffinity);
        }
    }
    if (val.contains("nodeAffinity")) {
        const nlohmann::json &fieldValue = val.at("nodeAffinity");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1NodeAffinity> refValSetNodeAffinity;
            ok &= ModelUtils::FromJson(fieldValue, refValSetNodeAffinity);
            SetNodeAffinity(refValSetNodeAffinity);
        }
    }
    return ok;
}

std::shared_ptr<V1PodAffinity> V1Affinity::GetPodAffinity() const
{
    return m_podAffinity;
}

void V1Affinity::SetPodAffinity(const std::shared_ptr<V1PodAffinity> &value)
{
    m_podAffinity = value;
    m_podAffinityIsSet = true;
}

bool V1Affinity::PodAffinityIsSet() const
{
    return m_podAffinityIsSet;
}

void V1Affinity::UnSetPodAffinity()
{
    m_podAffinityIsSet = false;
}
std::shared_ptr<V1PodAntiAffinity> V1Affinity::GetPodAntiAffinity() const
{
    return m_podAntiAffinity;
}

void V1Affinity::SetPodAntiAffinity(const std::shared_ptr<V1PodAntiAffinity> &value)
{
    m_podAntiAffinity = value;
    m_podAntiAffinityIsSet = true;
}

bool V1Affinity::PodAntiAffinityIsSet() const
{
    return m_podAntiAffinityIsSet;
}

void V1Affinity::UnSetPodAntiAffinity()
{
    m_podAntiAffinityIsSet = false;
}

std::shared_ptr<V1NodeAffinity> V1Affinity::GetNodeAffinity() const
{
    return m_nodeAffinity;
}

void V1Affinity::SetNodeAffinity(const std::shared_ptr<V1NodeAffinity>& value)
{
    m_nodeAffinity = value;
    m_nodeAffinityIsSet = true;
}

bool V1Affinity::NodeAffinityIsSet() const
{
    return m_nodeAffinityIsSet;
}

void V1Affinity::UnsetNodeAffinity()
{
    m_nodeAffinityIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
