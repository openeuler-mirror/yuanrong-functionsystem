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

#include "v1_node_selector.h"

namespace functionsystem::kube_client {
namespace model {
REGISTER_MODEL_OBJECT(V1NodeSelector);

V1NodeSelector::V1NodeSelector()
{
    m_nodeSelectorTermsIsSet = false;
}

V1NodeSelector::~V1NodeSelector()
{
}

nlohmann::json V1NodeSelector::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    if (m_nodeSelectorTermsIsSet) {
        val["nodeSelectorTerms"] = ModelUtils::ToJson(m_nodeSelectorTerms);
    }
    return val;
}

bool V1NodeSelector::FromJson(const nlohmann::json& val)
{
    bool ok = true;

    if (val.contains("nodeSelectorTerms")) {
        const nlohmann::json &fieldValue = val.at("nodeSelectorTerms");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1NodeSelectorTerm>> refValSetNodeSelectorTerms;
            ok &= ModelUtils::FromJson(fieldValue, refValSetNodeSelectorTerms);
            SetNodeSelectorTerms(refValSetNodeSelectorTerms);
        }
    }
    return ok;
}

std::vector<std::shared_ptr<V1NodeSelectorTerm>>& V1NodeSelector::GetNodeSelectorTerms()
{
    return m_nodeSelectorTerms;
}

void V1NodeSelector::SetNodeSelectorTerms(const std::vector<std::shared_ptr<V1NodeSelectorTerm>>& value)
{
    m_nodeSelectorTerms = value;
    m_nodeSelectorTermsIsSet = true;
}

bool V1NodeSelector::NodeSelectorTermsIsSet() const
{
    return m_nodeSelectorTermsIsSet;
}

void V1NodeSelector::UnsetNodeSelectorTerms()
{
    m_nodeSelectorTermsIsSet = false;
}
}
}
