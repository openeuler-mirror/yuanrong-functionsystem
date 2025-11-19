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

#include "v1_secret_key_selector.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1SecretKeySelector);

V1SecretKeySelector::V1SecretKeySelector()
{
    m_key = std::string("");
    m_keyIsSet = false;
    m_name = std::string("");
    m_nameIsSet = false;
    m_optional = false;
    m_optionalIsSet = false;
}

V1SecretKeySelector::~V1SecretKeySelector()
{}

nlohmann::json V1SecretKeySelector::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_keyIsSet) {
        val["key"] = ModelUtils::ToJson(m_key);
    }
    if (m_nameIsSet) {
        val["name"] = ModelUtils::ToJson(m_name);
    }
    if (m_optionalIsSet) {
        val["optional"] = ModelUtils::ToJson(m_optional);
    }

    return val;
}

bool V1SecretKeySelector::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("key")) {
        const nlohmann::json &fieldValue = val.at("key");
        if (!fieldValue.is_null()) {
            std::string refValSetKey;
            ok &= ModelUtils::FromJson(fieldValue, refValSetKey);
            SetKey(refValSetKey);
        }
    }
    if (val.contains("name")) {
        const nlohmann::json &fieldValue = val.at("name");
        if (!fieldValue.is_null()) {
            std::string refValSetName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetName);
            SetName(refValSetName);
        }
    }
    if (val.contains("optional")) {
        const nlohmann::json &fieldValue = val.at("optional");
        if (!fieldValue.is_null()) {
            bool refValSetOptional;
            ok &= ModelUtils::FromJson(fieldValue, refValSetOptional);
            SetOptional(refValSetOptional);
        }
    }
    return ok;
}

std::string V1SecretKeySelector::GetKey() const
{
    return m_key;
}

void V1SecretKeySelector::SetKey(const std::string &value)
{
    m_key = value;
    m_keyIsSet = true;
}

bool V1SecretKeySelector::KeyIsSet() const
{
    return m_keyIsSet;
}

void V1SecretKeySelector::UnsetKey()
{
    m_keyIsSet = false;
}

std::string V1SecretKeySelector::GetName() const
{
    return m_name;
}

void V1SecretKeySelector::SetName(const std::string &value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V1SecretKeySelector::NameIsSet() const
{
    return m_nameIsSet;
}

void V1SecretKeySelector::UnsetName()
{
    m_nameIsSet = false;
}
bool V1SecretKeySelector::IsOptional() const
{
    return m_optional;
}

void V1SecretKeySelector::SetOptional(bool value)
{
    m_optional = value;
    m_optionalIsSet = true;
}

bool V1SecretKeySelector::OptionalIsSet() const
{
    return m_optionalIsSet;
}

void V1SecretKeySelector::UnsetOptional()
{
    m_optionalIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
