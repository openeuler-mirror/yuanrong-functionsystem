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

#include "v1_node_affinity.h"

namespace functionsystem::kube_client {
namespace model {
REGISTER_MODEL_OBJECT(V1NodeAffinity);

V1NodeAffinity::V1NodeAffinity()
{
    m_requiredDuringSchedulingIgnoredDuringExecutionIsSet = false;
    m_preferredDuringSchedulingIgnoredDuringExecutionIsSet = false;
}

V1NodeAffinity::~V1NodeAffinity()
{
}

nlohmann::json V1NodeAffinity::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_requiredDuringSchedulingIgnoredDuringExecutionIsSet) {
        val["requiredDuringSchedulingIgnoredDuringExecution"] =
            ModelUtils::ToJson(m_requiredDuringSchedulingIgnoredDuringExecution);
    }

    if (m_preferredDuringSchedulingIgnoredDuringExecutionIsSet) {
        val["preferredDuringSchedulingIgnoredDuringExecution"] =
            ModelUtils::ToJson(m_preferredDuringSchedulingIgnoredDuringExecution);
    }
    return val;
}

bool V1NodeAffinity::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    if (val.contains("requiredDuringSchedulingIgnoredDuringExecution")) {
        const nlohmann::json &fieldValue = val.at("requiredDuringSchedulingIgnoredDuringExecution");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1NodeSelector> refValSetRequiredDuringSchedulingIgnoredDuringExecution;
            ok &= ModelUtils::FromJson(fieldValue, refValSetRequiredDuringSchedulingIgnoredDuringExecution);
            SetRequiredDuringSchedulingIgnoredDuringExecution(refValSetRequiredDuringSchedulingIgnoredDuringExecution);
        }
    }

    if (val.contains("preferredDuringSchedulingIgnoredDuringExecution")) {
        const nlohmann::json &fieldValue = val.at("preferredDuringSchedulingIgnoredDuringExecution");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1PreferredSchedulingTerm>> refValSetPreferredExecution;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPreferredExecution);
            SetPreferredDuringSchedulingIgnoredDuringExecution(refValSetPreferredExecution);
        }
    }
    return ok;
}

std::shared_ptr<V1NodeSelector> V1NodeAffinity::GetRequiredDuringSchedulingIgnoredDuringExecution() const
{
    return m_requiredDuringSchedulingIgnoredDuringExecution;
}

void V1NodeAffinity::SetRequiredDuringSchedulingIgnoredDuringExecution(const std::shared_ptr<V1NodeSelector>& value)
{
    m_requiredDuringSchedulingIgnoredDuringExecution = value;
    m_requiredDuringSchedulingIgnoredDuringExecutionIsSet = true;
}

bool V1NodeAffinity::RequiredDuringSchedulingIgnoredDuringExecutionIsSet() const
{
    return m_requiredDuringSchedulingIgnoredDuringExecutionIsSet;
}

void V1NodeAffinity::UnsetRequiredDuringSchedulingIgnoredDuringExecution()
{
    m_requiredDuringSchedulingIgnoredDuringExecutionIsSet = false;
}

void V1NodeAffinity::SetPreferredDuringSchedulingIgnoredDuringExecution(
    const std::vector<std::shared_ptr<V1PreferredSchedulingTerm>>& value)
{
    m_preferredDuringSchedulingIgnoredDuringExecution = value;
    m_preferredDuringSchedulingIgnoredDuringExecutionIsSet = true;
}

bool V1NodeAffinity::PreferredDuringSchedulingIgnoredDuringExecutionIsSet() const
{
    return m_preferredDuringSchedulingIgnoredDuringExecutionIsSet;
}

void V1NodeAffinity::UnsetPreferredDuringSchedulingIgnoredDuringExecution()
{
    m_preferredDuringSchedulingIgnoredDuringExecutionIsSet = false;
}
}
}
