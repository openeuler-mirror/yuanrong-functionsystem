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

#include "v1_pod_anti_affinity.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1PodAntiAffinity);

V1PodAntiAffinity::V1PodAntiAffinity()
{
    m_preferredDuringSchedulingIgnoredDuringExecutionIsSet = false;
    m_requiredDuringSchedulingIgnoredDuringExecutionIsSet = false;
}

V1PodAntiAffinity::~V1PodAntiAffinity()
{
}

nlohmann::json V1PodAntiAffinity::ToJson() const
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

bool V1PodAntiAffinity::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (ModelUtils::CheckHasKey(val, "preferredDuringSchedulingIgnoredDuringExecution")) {
        const nlohmann::json &fieldValue = val.at("preferredDuringSchedulingIgnoredDuringExecution");
        std::vector<std::shared_ptr<V1WeightedPodAffinityTerm>>
            refValSetPreferredDuringSchedulingIgnoredDuringExecution;
        ok &= ModelUtils::FromJson(fieldValue, refValSetPreferredDuringSchedulingIgnoredDuringExecution);
        SetPreferredDuringSchedulingIgnoredDuringExecution(refValSetPreferredDuringSchedulingIgnoredDuringExecution);
    }
    if (val.contains("requiredDuringSchedulingIgnoredDuringExecution")) {
        const nlohmann::json &fieldValue = val.at("requiredDuringSchedulingIgnoredDuringExecution");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1PodAffinityTerm>> refValSetRequiredDuringSchedulingIgnoredDuringExecution;
            ok &= ModelUtils::FromJson(fieldValue, refValSetRequiredDuringSchedulingIgnoredDuringExecution);
            SetRequiredDuringSchedulingIgnoredDuringExecution(refValSetRequiredDuringSchedulingIgnoredDuringExecution);
        }
    }
    return ok;
}

void V1PodAntiAffinity::SetPreferredDuringSchedulingIgnoredDuringExecution(
    const std::vector<std::shared_ptr<V1WeightedPodAffinityTerm>> &value)
{
    m_preferredDuringSchedulingIgnoredDuringExecution = value;
    m_preferredDuringSchedulingIgnoredDuringExecutionIsSet = true;
}

bool V1PodAntiAffinity::PreferredDuringSchedulingIgnoredDuringExecutionIsSet() const
{
    return m_preferredDuringSchedulingIgnoredDuringExecutionIsSet;
}

void V1PodAntiAffinity::UnsetPreferredDuringSchedulingIgnoredDuringExecution()
{
    m_preferredDuringSchedulingIgnoredDuringExecutionIsSet = false;
}

void V1PodAntiAffinity::SetRequiredDuringSchedulingIgnoredDuringExecution(
    const std::vector<std::shared_ptr<V1PodAffinityTerm>> &value)
{
    m_requiredDuringSchedulingIgnoredDuringExecution = value;
    m_requiredDuringSchedulingIgnoredDuringExecutionIsSet = true;
}

bool V1PodAntiAffinity::RequiredDuringSchedulingIgnoredDuringExecutionIsSet() const
{
    return m_requiredDuringSchedulingIgnoredDuringExecutionIsSet;
}

void V1PodAntiAffinity::UnsetRequiredDuringSchedulingIgnoredDuringExecution()
{
    m_requiredDuringSchedulingIgnoredDuringExecutionIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
