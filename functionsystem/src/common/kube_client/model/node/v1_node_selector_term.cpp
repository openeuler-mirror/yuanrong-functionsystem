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

#include "v1_node_selector_term.h"

namespace functionsystem::kube_client {
namespace model {
REGISTER_MODEL_OBJECT(V1NodeSelectorTerm);

V1NodeSelectorTerm::V1NodeSelectorTerm()
{
    m_matchExpressionsIsSet = false;
    m_matchFieldsIsSet = false;
}

V1NodeSelectorTerm::~V1NodeSelectorTerm()
{
}

nlohmann::json V1NodeSelectorTerm::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    if (m_matchExpressionsIsSet) {
        val["matchExpressions"] = ModelUtils::ToJson(m_matchExpressions);
    }
    if (m_matchFieldsIsSet) {
        val["matchFields"] = ModelUtils::ToJson(m_matchFields);
    }
    return val;
}

bool V1NodeSelectorTerm::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    if (val.contains("matchExpressions")) {
        const nlohmann::json &fieldValue = val.at("matchExpressions");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1NodeSelectorRequirement>> refValSetMatchExpressions;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMatchExpressions);
            SetMatchExpressions(refValSetMatchExpressions);
        }
    }
    if (val.contains("matchFields")) {
        const nlohmann::json &fieldValue = val.at("matchFields");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1NodeSelectorRequirement>> refValSetMatchFields;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMatchFields);
            SetMatchFields(refValSetMatchFields);
        }
    }
    return ok;
}

std::vector<std::shared_ptr<V1NodeSelectorRequirement>>& V1NodeSelectorTerm::GetMatchExpressions()
{
    return m_matchExpressions;
}

void V1NodeSelectorTerm::SetMatchExpressions(const std::vector<std::shared_ptr<V1NodeSelectorRequirement>>& value)
{
    m_matchExpressions = value;
    m_matchExpressionsIsSet = true;
}

bool V1NodeSelectorTerm::MatchExpressionsIsSet() const
{
    return m_matchExpressionsIsSet;
}

void V1NodeSelectorTerm::UnsetMatchExpressions()
{
    m_matchExpressionsIsSet = false;
}
std::vector<std::shared_ptr<V1NodeSelectorRequirement>>& V1NodeSelectorTerm::GetMatchFields()
{
    return m_matchFields;
}

void V1NodeSelectorTerm::SetMatchFields(const std::vector<std::shared_ptr<V1NodeSelectorRequirement>>& value)
{
    m_matchFields = value;
    m_matchFieldsIsSet = true;
}

bool V1NodeSelectorTerm::MatchFieldsIsSet() const
{
    return m_matchFieldsIsSet;
}

void V1NodeSelectorTerm::UnsetMatchFields()
{
    m_matchFieldsIsSet = false;
}
}
}