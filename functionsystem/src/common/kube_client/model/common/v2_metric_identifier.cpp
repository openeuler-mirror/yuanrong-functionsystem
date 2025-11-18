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

#include "v2_metric_identifier.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2MetricIdentifier);

V2MetricIdentifier::V2MetricIdentifier() noexcept : m_name(std::string("")),
    m_nameIsSet(false),
    m_selectorIsSet(false)
{}

V2MetricIdentifier::~V2MetricIdentifier()
{
}

nlohmann::json V2MetricIdentifier::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_nameIsSet) {
        val[std::string(("name"))] = ModelUtils::ToJson(m_name);
    }
    if (m_selectorIsSet) {
        val[std::string(("selector"))] = ModelUtils::ToJson(m_selector);
    }

    return val;
}

bool V2MetricIdentifier::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    
    if (val.contains(std::string(("name")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("name")));
        if (!fieldValue.is_null()) {
            std::string refValSetName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetName);
            SetName(refValSetName);
        }
    }
    if (val.contains(std::string(("selector")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("selector")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1LabelSelector> refValSetSelector;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSelector);
            SetSelector(refValSetSelector);
        }
    }
    return ok;
}

std::string V2MetricIdentifier::GetName() const
{
    return m_name;
}

void V2MetricIdentifier::SetName(const std::string& value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V2MetricIdentifier::NameIsSet() const
{
    return m_nameIsSet;
}

void V2MetricIdentifier::UnsetName()
{
    m_nameIsSet = false;
}
std::shared_ptr<V1LabelSelector> V2MetricIdentifier::GetSelector() const
{
    return m_selector;
}

void V2MetricIdentifier::SetSelector(const std::shared_ptr<V1LabelSelector>& value)
{
    m_selector = value;
    m_selectorIsSet = true;
}

bool V2MetricIdentifier::SelectorIsSet() const
{
    return m_selectorIsSet;
}

void V2MetricIdentifier::UnsetSelector()
{
    m_selectorIsSet = false;
}
}
}
