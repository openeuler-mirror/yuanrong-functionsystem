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

#include "v2_resource_metric_status.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2ResourceMetricStatus);

V2ResourceMetricStatus::V2ResourceMetricStatus() : m_currentIsSet(false),
    m_name(std::string("")),
    m_nameIsSet(false)
{}

V2ResourceMetricStatus::~V2ResourceMetricStatus()
{
}

nlohmann::json V2ResourceMetricStatus::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_currentIsSet) {
        val[std::string(("current"))] = ModelUtils::ToJson(m_current);
    }
    if (m_nameIsSet) {
        val[std::string(("name"))] = ModelUtils::ToJson(m_name);
    }

    return val;
}

bool V2ResourceMetricStatus::FromJson(const nlohmann::json& val)
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
    if (val.contains(std::string(("current")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("current")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2MetricValueStatus> refValSetCurrent;
            ok &= ModelUtils::FromJson(fieldValue, refValSetCurrent);
            SetCurrent(refValSetCurrent);
        }
    }
    return ok;
}

std::shared_ptr<V2MetricValueStatus> V2ResourceMetricStatus::GetCurrent() const
{
    return m_current;
}

void V2ResourceMetricStatus::SetCurrent(const std::shared_ptr<V2MetricValueStatus>& value)
{
    m_current = value;
    m_currentIsSet = true;
}

bool V2ResourceMetricStatus::CurrentIsSet() const
{
    return m_currentIsSet;
}

void V2ResourceMetricStatus::UnsetCurrent()
{
    m_currentIsSet = false;
}
std::string V2ResourceMetricStatus::GetName() const
{
    return m_name;
}

void V2ResourceMetricStatus::SetName(const std::string& value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V2ResourceMetricStatus::NameIsSet() const
{
    return m_nameIsSet;
}

void V2ResourceMetricStatus::UnsetName()
{
    m_nameIsSet = false;
}
}
}
