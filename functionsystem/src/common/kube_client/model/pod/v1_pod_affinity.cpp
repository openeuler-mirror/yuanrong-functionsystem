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

#include "v1_pod_affinity.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1PodAffinity);

V1PodAffinity::V1PodAffinity()
{
    m_preferredDuringSchedulingIgnoredDuringExecutionIsSet = false;
    m_requiredDuringSchedulingIgnoredDuringExecutionIsSet = false;
}

V1PodAffinity::~V1PodAffinity()
{
}

nlohmann::json V1PodAffinity::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_preferredDuringSchedulingIgnoredDuringExecutionIsSet) {
        val["preferredDuringSchedulingIgnoredDuringExecution"] =
            ModelUtils::ToJson(m_preferredDuringSchedulingIgnoredDuringExecution);
    }
    if (m_requiredDuringSchedulingIgnoredDuringExecutionIsSet) {
        val["requiredDuringSchedulingIgnoredDuringExecution"] =
            ModelUtils::ToJson(m_requiredDuringSchedulingIgnoredDuringExecution);
    }

    return val;
}

bool V1PodAffinity::FromJson(const nlohmann::json &val)
{
    bool ok = true;
    if (val.contains("requiredDuringSchedulingIgnoredDuringExecution")) {
        const nlohmann::json &fieldValue = val.at("requiredDuringSchedulingIgnoredDuringExecution");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1PodAffinityTerm>> refValSetRequiredDuringSchedulingIgnoredDuringExecution;
            ok &= ModelUtils::FromJson(fieldValue, refValSetRequiredDuringSchedulingIgnoredDuringExecution);
            SetRequiredDuringSchedulingIgnoredDuringExecution(refValSetRequiredDuringSchedulingIgnoredDuringExecution);
        }
    }
    if (val.contains("preferredDuringSchedulingIgnoredDuringExecution")) {
        const nlohmann::json &fieldValue = val.at("preferredDuringSchedulingIgnoredDuringExecution");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1WeightedPodAffinityTerm>>
                refValSetPreferredDuringSchedulingIgnoredDuringExecution;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPreferredDuringSchedulingIgnoredDuringExecution);
            SetPreferredDuringSchedulingIgnoredDuringExecution(
                refValSetPreferredDuringSchedulingIgnoredDuringExecution);
        }
    }
    return ok;
}

void V1PodAffinity::SetPreferredDuringSchedulingIgnoredDuringExecution(
    const std::vector<std::shared_ptr<V1WeightedPodAffinityTerm>> &value)
{
    m_preferredDuringSchedulingIgnoredDuringExecution = value;
    m_preferredDuringSchedulingIgnoredDuringExecutionIsSet = true;
}

bool V1PodAffinity::PreferredDuringSchedulingIgnoredDuringExecutionIsSet() const
{
    return m_preferredDuringSchedulingIgnoredDuringExecutionIsSet;
}

void V1PodAffinity::UnsetPreferredDuringSchedulingIgnoredDuringExecution()
{
    m_preferredDuringSchedulingIgnoredDuringExecutionIsSet = false;
}

std::vector<std::shared_ptr<V1PodAffinityTerm>> &V1PodAffinity::GetRequiredDuringSchedulingIgnoredDuringExecution()
{
    return m_requiredDuringSchedulingIgnoredDuringExecution;
}

void V1PodAffinity::SetRequiredDuringSchedulingIgnoredDuringExecution(
    const std::vector<std::shared_ptr<V1PodAffinityTerm>> &value)
{
    m_requiredDuringSchedulingIgnoredDuringExecution = value;
    m_requiredDuringSchedulingIgnoredDuringExecutionIsSet = true;
}

bool V1PodAffinity::RequiredDuringSchedulingIgnoredDuringExecutionIsSet() const
{
    return m_requiredDuringSchedulingIgnoredDuringExecutionIsSet;
}

void V1PodAffinity::UnsetRequiredDuringSchedulingIgnoredDuringExecution()
{
    m_requiredDuringSchedulingIgnoredDuringExecutionIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
