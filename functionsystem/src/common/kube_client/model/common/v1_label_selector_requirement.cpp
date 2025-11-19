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

#include "v1_label_selector_requirement.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1LabelSelectorRequirement);

V1LabelSelectorRequirement::V1LabelSelectorRequirement()
{
    m_key = std::string("");
    m_keyIsSet = false;
    m_rOperator = std::string("");
    m_rOperatorIsSet = false;
    m_valuesIsSet = false;
}

V1LabelSelectorRequirement::~V1LabelSelectorRequirement()
{
}

nlohmann::json V1LabelSelectorRequirement::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_keyIsSet) {
        val["key"] = ModelUtils::ToJson(m_key);
    }
    if (m_rOperatorIsSet) {
        val["operator"] = ModelUtils::ToJson(m_rOperator);
    }
    if (m_valuesIsSet) {
        val["values"] = ModelUtils::ToJson(m_values);
    }

    return val;
}

bool V1LabelSelectorRequirement::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (ModelUtils::CheckHasKey(val, "key")) {
        const nlohmann::json &fieldValue = val.at("key");
        std::string refValSetKey;
        ok &= ModelUtils::FromJson(fieldValue, refValSetKey);
        SetKey(refValSetKey);
    }
    if (val.contains("operator")) {
        const nlohmann::json &fieldValue = val.at("operator");
        if (!fieldValue.is_null()) {
            std::string refValSetROperator;
            ok &= ModelUtils::FromJson(fieldValue, refValSetROperator);
            SetROperator(refValSetROperator);
        }
    }
    if (val.contains("values")) {
        const nlohmann::json &fieldValue = val.at("values");
        if (!fieldValue.is_null()) {
            std::vector<std::string> refValSetValues;
            ok &= ModelUtils::FromJson(fieldValue, refValSetValues);
            SetValues(refValSetValues);
        }
    }
    return ok;
}

std::string V1LabelSelectorRequirement::GetKey() const
{
    return m_key;
}

void V1LabelSelectorRequirement::SetKey(const std::string &value)
{
    m_key = value;
    m_keyIsSet = true;
}

bool V1LabelSelectorRequirement::KeyIsSet() const
{
    return m_keyIsSet;
}

void V1LabelSelectorRequirement::UnsetKey()
{
    m_keyIsSet = false;
}
std::string V1LabelSelectorRequirement::GetROperator() const
{
    return m_rOperator;
}

void V1LabelSelectorRequirement::SetROperator(const std::string &value)
{
    m_rOperator = value;
    m_rOperatorIsSet = true;
}

bool V1LabelSelectorRequirement::ROperatorIsSet() const
{
    return m_rOperatorIsSet;
}

void V1LabelSelectorRequirement::UnsetROperator()
{
    m_rOperatorIsSet = false;
}

std::vector<std::string> &V1LabelSelectorRequirement::GetValues()
{
    return m_values;
}

void V1LabelSelectorRequirement::SetValues(const std::vector<std::string> &value)
{
    m_values = value;
    m_valuesIsSet = true;
}

bool V1LabelSelectorRequirement::ValuesIsSet() const
{
    return m_valuesIsSet;
}

void V1LabelSelectorRequirement::UnsetValues()
{
    m_valuesIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
