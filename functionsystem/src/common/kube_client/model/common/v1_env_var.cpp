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

#include "v1_env_var.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1EnvVar);

V1EnvVar::V1EnvVar()
{
    m_name = std::string("");
    m_nameIsSet = false;
    m_value = std::string("");
    m_valueIsSet = false;
    m_valueFromIsSet = false;
}

V1EnvVar::~V1EnvVar()
{
}

nlohmann::json V1EnvVar::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_nameIsSet) {
        val["name"] = ModelUtils::ToJson(m_name);
    }
    if (m_valueIsSet) {
        val["value"] = ModelUtils::ToJson(m_value);
    }
    if (m_valueFromIsSet) {
        val["valueFrom"] = ModelUtils::ToJson(m_valueFrom);
    }

    return val;
}

bool V1EnvVar::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("name")) {
        const nlohmann::json &fieldValue = val.at("name");
        if (!fieldValue.is_null()) {
            std::string refValSetName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetName);
            SetName(refValSetName);
        }
    }
    if (val.contains("value")) {
        const nlohmann::json &fieldValue = val.at("value");
        if (!fieldValue.is_null()) {
            std::string refValSetValue;
            ok &= ModelUtils::FromJson(fieldValue, refValSetValue);
            SetValue(refValSetValue);
        }
    }
    if (val.contains("valueFrom")) {
        const nlohmann::json &fieldValue = val.at("valueFrom");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1EnvVarSource> refValSetValueFrom;
            ok &= ModelUtils::FromJson(fieldValue, refValSetValueFrom);
            SetValueFrom(refValSetValueFrom);
        }
    }
    return ok;
}

std::string V1EnvVar::GetName() const
{
    return m_name;
}

void V1EnvVar::SetName(const std::string &value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V1EnvVar::NameIsSet() const
{
    return m_nameIsSet;
}

void V1EnvVar::UnsetName()
{
    m_nameIsSet = false;
}
std::string V1EnvVar::GetValue() const
{
    return m_value;
}

void V1EnvVar::SetValue(const std::string &value)
{
    m_value = value;
    m_valueIsSet = true;
}

bool V1EnvVar::ValueIsSet() const
{
    return m_valueIsSet;
}

void V1EnvVar::UnsetValue()
{
    m_valueIsSet = false;
}
std::shared_ptr<V1EnvVarSource> V1EnvVar::GetValueFrom() const
{
    return m_valueFrom;
}

void V1EnvVar::SetValueFrom(const std::shared_ptr<V1EnvVarSource> &value)
{
    m_valueFrom = value;
    m_valueFromIsSet = true;
}

bool V1EnvVar::ValueFromIsSet() const
{
    return m_valueFromIsSet;
}

void V1EnvVar::UnsetValueFrom()
{
    m_valueFromIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
