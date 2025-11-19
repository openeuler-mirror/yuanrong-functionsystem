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

#include "v2_metric_target.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2MetricTarget);

V2MetricTarget::V2MetricTarget() : m_averageUtilization(0),
    m_averageUtilizationIsSet(false),
    m_averageValue(std::string("")),
    m_averageValueIsSet(false),
    m_type(std::string("")),
    m_typeIsSet(false),
    m_value(std::string("")),
    m_valueIsSet(false)
{}

V2MetricTarget::~V2MetricTarget()
{
}

nlohmann::json V2MetricTarget::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_averageUtilizationIsSet) {
        val[std::string(("averageUtilization"))] = ModelUtils::ToJson(m_averageUtilization);
    }
    if (m_averageValueIsSet) {
        val[std::string(("averageValue"))] = ModelUtils::ToJson(m_averageValue);
    }
    if (m_typeIsSet) {
        val[std::string(("type"))] = ModelUtils::ToJson(m_type);
    }
    if (m_valueIsSet) {
        val[std::string(("value"))] = ModelUtils::ToJson(m_value);
    }

    return val;
}

bool V2MetricTarget::FromJson(const nlohmann::json& val)
{
    bool ok = true;

    if (val.contains(std::string("averageUtilization"))) {
        const nlohmann::json& fieldValue = val.at(std::string("averageUtilization"));
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
    if (val.contains(std::string(("type")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("type")));
        if (!fieldValue.is_null()) {
            std::string refValSetType;
            ok &= ModelUtils::FromJson(fieldValue, refValSetType);
            SetType(refValSetType);
        }
    }
    if (val.contains(std::string("value"))) {
        const nlohmann::json& fieldValue = val.at(std::string(("value")));
        if (!fieldValue.is_null()) {
            std::string refValSetValue;
            ok &= ModelUtils::FromJson(fieldValue, refValSetValue);
            SetValue(refValSetValue);
        }
    }
    return ok;
}

int32_t V2MetricTarget::GetAverageUtilization() const
{
    return m_averageUtilization;
}

void V2MetricTarget::SetAverageUtilization(int32_t value)
{
    m_averageUtilization = value;
    m_averageUtilizationIsSet = true;
}

bool V2MetricTarget::AverageUtilizationIsSet() const
{
    return m_averageUtilizationIsSet;
}

void V2MetricTarget::UnsetAverageUtilization()
{
    m_averageUtilizationIsSet = false;
}
std::string V2MetricTarget::GetAverageValue() const
{
    return m_averageValue;
}

void V2MetricTarget::SetAverageValue(const std::string& value)
{
    m_averageValue = value;
    m_averageValueIsSet = true;
}

bool V2MetricTarget::AverageValueIsSet() const
{
    return m_averageValueIsSet;
}

void V2MetricTarget::UnsetAverageValue()
{
    m_averageValueIsSet = false;
}
std::string V2MetricTarget::GetType() const
{
    return m_type;
}

void V2MetricTarget::SetType(const std::string& value)
{
    m_type = value;
    m_typeIsSet = true;
}

bool V2MetricTarget::TypeIsSet() const
{
    return m_typeIsSet;
}

void V2MetricTarget::UnsetType()
{
    m_typeIsSet = false;
}
std::string V2MetricTarget::GetValue() const
{
    return m_value;
}

void V2MetricTarget::SetValue(const std::string& value)
{
    m_value = value;
    m_valueIsSet = true;
}

bool V2MetricTarget::ValueIsSet() const
{
    return m_valueIsSet;
}

void V2MetricTarget::UnsetValue()
{
    m_valueIsSet = false;
}
}
}
