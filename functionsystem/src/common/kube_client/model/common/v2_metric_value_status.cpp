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

#include "v2_metric_value_status.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2MetricValueStatus);

V2MetricValueStatus::V2MetricValueStatus() : m_averageUtilization(0),
    m_averageUtilizationIsSet(false),
    m_averageValue(std::string("")),
    m_averageValueIsSet(false),
    m_value(std::string("")),
    m_valueIsSet(false)
{}

V2MetricValueStatus::~V2MetricValueStatus()
{
}

nlohmann::json V2MetricValueStatus::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_averageUtilizationIsSet) {
        val[std::string(("averageUtilization"))] = ModelUtils::ToJson(m_averageUtilization);
    }
    if (m_averageValueIsSet) {
        val[std::string(("averageValue"))] = ModelUtils::ToJson(m_averageValue);
    }
    if (m_valueIsSet) {
        val[std::string(("value"))] = ModelUtils::ToJson(m_value);
    }

    return val;
}

bool V2MetricValueStatus::FromJson(const nlohmann::json& val)
{
    bool ok = true;

    if (val.contains(std::string(("averageUtilization")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("averageUtilization")));
        if (!fieldValue.is_null()) {
            int32_t refValSetAverageUtilization;
            ok &= ModelUtils::FromJson(fieldValue, refValSetAverageUtilization);
            SetAverageUtilization(refValSetAverageUtilization);
        }
    }
    if (val.contains(std::string(("averageValue")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("averageValue")));
        if (!fieldValue.is_null()) {
            std::string refValSetAverageValue;
            ok &= ModelUtils::FromJson(fieldValue, refValSetAverageValue);
            SetAverageValue(refValSetAverageValue);
        }
    }
    if (val.contains(std::string(("value")))) {
        const nlohmann::json& fieldValue = val.at(std::string("value"));
        if (!fieldValue.is_null()) {
            std::string refValSetValue;
            ok &= ModelUtils::FromJson(fieldValue, refValSetValue);
            SetValue(refValSetValue);
        }
    }
    return ok;
}

int32_t V2MetricValueStatus::GetAverageUtilization() const
{
    return m_averageUtilization;
}

void V2MetricValueStatus::SetAverageUtilization(int32_t value)
{
    m_averageUtilization = value;
    m_averageUtilizationIsSet = true;
}

bool V2MetricValueStatus::AverageUtilizationIsSet() const
{
    return m_averageUtilizationIsSet;
}

void V2MetricValueStatus::UnsetAverageUtilization()
{
    m_averageUtilizationIsSet = false;
}
std::string V2MetricValueStatus::GetAverageValue() const
{
    return m_averageValue;
}

void V2MetricValueStatus::SetAverageValue(const std::string& value)
{
    m_averageValue = value;
    m_averageValueIsSet = true;
}

bool V2MetricValueStatus::AverageValueIsSet() const
{
    return m_averageValueIsSet;
}

void V2MetricValueStatus::UnsetAverageValue()
{
    m_averageValueIsSet = false;
}
std::string V2MetricValueStatus::GetValue() const
{
    return m_value;
}

void V2MetricValueStatus::SetValue(const std::string& value)
{
    m_value = value;
    m_valueIsSet = true;
}

bool V2MetricValueStatus::ValueIsSet() const
{
    return m_valueIsSet;
}

void V2MetricValueStatus::UnsetValue()
{
    m_valueIsSet = false;
}
}
}
