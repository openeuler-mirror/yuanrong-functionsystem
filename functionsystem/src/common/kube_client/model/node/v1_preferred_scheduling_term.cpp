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

#include "v1_preferred_scheduling_term.h"

namespace functionsystem::kube_client {
namespace model {
REGISTER_MODEL_OBJECT(V1PreferredSchedulingTerm);

V1PreferredSchedulingTerm::V1PreferredSchedulingTerm()
{
}

V1PreferredSchedulingTerm::~V1PreferredSchedulingTerm()
{
}

nlohmann::json V1PreferredSchedulingTerm::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    if (m_preferenceIsSet) {
        val["preference"] = ModelUtils::ToJson(m_preference);
    }
    if (m_weightIsSet) {
        val["weight"] = ModelUtils::ToJson(m_weight);
    }
    return val;
}

bool V1PreferredSchedulingTerm::FromJson(const nlohmann::json& val)
{
    bool ok = true;

    if (val.contains("preference")) {
        const nlohmann::json &fieldValue = val.at("preference");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1NodeSelectorTerm> refValSetPreference;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPreference);
            SetPreference(refValSetPreference);
        }
    }
    if (val.contains("weight")) {
        const nlohmann::json &fieldValue = val.at("weight");
        if (!fieldValue.is_null()) {
            int32_t refValSetWeight;
            ok &= ModelUtils::FromJson(fieldValue, refValSetWeight);
            SetWeight(refValSetWeight);
        }
    }
    return ok;
}

std::shared_ptr<V1NodeSelectorTerm> V1PreferredSchedulingTerm::GetPreference() const
{
    return m_preference;
}

void V1PreferredSchedulingTerm::SetPreference(const std::shared_ptr<V1NodeSelectorTerm> &value)
{
    m_preference = value;
    m_preferenceIsSet = true;
}

bool V1PreferredSchedulingTerm::PreferenceIsSet() const
{
    return m_preferenceIsSet;
}

void V1PreferredSchedulingTerm::UnsetPreference()
{
    m_preferenceIsSet = false;
}

int32_t V1PreferredSchedulingTerm::GetWeight() const
{
    return m_weight;
}

void V1PreferredSchedulingTerm::SetWeight(int32_t value)
{
    m_weight = value;
    m_weightIsSet = true;
}

bool V1PreferredSchedulingTerm::WeightIsSet() const
{
    return m_weightIsSet;
}

void V1PreferredSchedulingTerm::UnsetWeight()
{
    m_weightIsSet = false;
}

}
}
