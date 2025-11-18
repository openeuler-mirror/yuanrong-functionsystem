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

#include "v1_weighted_pod_affinity_term.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1WeightedPodAffinityTerm);

V1WeightedPodAffinityTerm::V1WeightedPodAffinityTerm()
{
    m_podAffinityTermIsSet = false;
    m_weight = 0;
    m_weightIsSet = false;
}

V1WeightedPodAffinityTerm::~V1WeightedPodAffinityTerm()
{
}

nlohmann::json V1WeightedPodAffinityTerm::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_podAffinityTermIsSet) {
        val["podAffinityTerm"] = ModelUtils::ToJson(m_podAffinityTerm);
    }
    if (m_weightIsSet) {
        val["weight"] = ModelUtils::ToJson(m_weight);
    }

    return val;
}

bool V1WeightedPodAffinityTerm::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("podAffinityTerm")) {
        const nlohmann::json &fieldValue = val.at("podAffinityTerm");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1PodAffinityTerm> refValSetPodAffinityTerm;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPodAffinityTerm);
            SetPodAffinityTerm(refValSetPodAffinityTerm);
        }
    }
    if (val.contains("weight")) {
        const nlohmann::json &fieldValue = val.at("weight");
        if (!fieldValue.is_null()) {
            int64_t refValSetWeight;
            ok &= ModelUtils::FromJson(fieldValue, refValSetWeight);
            SetWeight(refValSetWeight);
        }
    }
    return ok;
}

std::shared_ptr<V1PodAffinityTerm> V1WeightedPodAffinityTerm::GetPodAffinityTerm() const
{
    return m_podAffinityTerm;
}

void V1WeightedPodAffinityTerm::SetPodAffinityTerm(const std::shared_ptr<V1PodAffinityTerm> &value)
{
    m_podAffinityTerm = value;
    m_podAffinityTermIsSet = true;
}

bool V1WeightedPodAffinityTerm::PodAffinityTermIsSet() const
{
    return m_podAffinityTermIsSet;
}

void V1WeightedPodAffinityTerm::UnSetPodAffinityTerm()
{
    m_podAffinityTermIsSet = false;
}
int64_t V1WeightedPodAffinityTerm::GetWeight() const
{
    return m_weight;
}

void V1WeightedPodAffinityTerm::SetWeight(int64_t value)
{
    m_weight = value;
    m_weightIsSet = true;
}

bool V1WeightedPodAffinityTerm::WeightIsSet() const
{
    return m_weightIsSet;
}

void V1WeightedPodAffinityTerm::UnsetWeight()
{
    m_weightIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
