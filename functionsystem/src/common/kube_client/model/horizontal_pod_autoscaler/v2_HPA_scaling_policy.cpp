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

#include "v2_HPA_scaling_policy.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2HPAScalingPolicy);

V2HPAScalingPolicy::V2HPAScalingPolicy() : m_periodSeconds(0),
    m_periodSecondsIsSet(false),
    m_type(std::string("")),
    m_typeIsSet(false),
    m_value(0),
    m_valueIsSet(false)
{}

V2HPAScalingPolicy::~V2HPAScalingPolicy()
{
}

nlohmann::json V2HPAScalingPolicy::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_periodSecondsIsSet) {
        val[std::string(("periodSeconds"))] = ModelUtils::ToJson(m_periodSeconds);
    }
    if (m_typeIsSet) {
        val[std::string(("type"))] = ModelUtils::ToJson(m_type);
    }
    if (m_valueIsSet) {
        val[std::string(("value"))] = ModelUtils::ToJson(m_value);
    }

    return val;
}

bool V2HPAScalingPolicy::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    
    if (val.contains(std::string(("periodSeconds")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("periodSeconds")));
        if (!fieldValue.is_null()) {
            int32_t refValSetPeriodSeconds;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPeriodSeconds);
            SetPeriodSeconds(refValSetPeriodSeconds);
        }
    }
    if (val.contains(std::string(("type")))) {
        const nlohmann::json& fieldValue = val.at(std::string("type"));
        if (!fieldValue.is_null()) {
            std::string refValSetType;
            ok &= ModelUtils::FromJson(fieldValue, refValSetType);
            SetType(refValSetType);
        }
    }
    if (val.contains(std::string("value"))) {
        const nlohmann::json& fieldValue = val.at(std::string("value"));
        if (!fieldValue.is_null()) {
            int32_t refValSetValue;
            ok &= ModelUtils::FromJson(fieldValue, refValSetValue);
            SetValue(refValSetValue);
        }
    }
    return ok;
}

int32_t V2HPAScalingPolicy::GetPeriodSeconds() const
{
    return m_periodSeconds;
}

void V2HPAScalingPolicy::SetPeriodSeconds(int32_t value)
{
    m_periodSeconds = value;
    m_periodSecondsIsSet = true;
}

bool V2HPAScalingPolicy::PeriodSecondsIsSet() const
{
    return m_periodSecondsIsSet;
}

void V2HPAScalingPolicy::UnsetPeriodSeconds()
{
    m_periodSecondsIsSet = false;
}
std::string V2HPAScalingPolicy::GetType() const
{
    return m_type;
}

void V2HPAScalingPolicy::SetType(const std::string& value)
{
    m_type = value;
    m_typeIsSet = true;
}

bool V2HPAScalingPolicy::TypeIsSet() const
{
    return m_typeIsSet;
}

void V2HPAScalingPolicy::UnsetType()
{
    m_typeIsSet = false;
}
int32_t V2HPAScalingPolicy::GetValue() const
{
    return m_value;
}

void V2HPAScalingPolicy::SetValue(int32_t value)
{
    m_value = value;
    m_valueIsSet = true;
}

bool V2HPAScalingPolicy::ValueIsSet() const
{
    return m_valueIsSet;
}

void V2HPAScalingPolicy::UnsetValue()
{
    m_valueIsSet = false;
}
}
}