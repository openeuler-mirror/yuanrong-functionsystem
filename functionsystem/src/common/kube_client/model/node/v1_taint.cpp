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

#include "v1_taint.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1Taint);

V1Taint::V1Taint()
{
    m_effect = std::string("");
    m_effectIsSet = false;
    m_key = std::string("");
    m_keyIsSet = false;
    m_timeAdded = utility::Datetime();
    m_timeAddedIsSet = false;
    m_value = std::string("");
    m_valueIsSet = false;
}

V1Taint::~V1Taint()
{
}

nlohmann::json V1Taint::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_effectIsSet) {
        val["effect"] = ModelUtils::ToJson(m_effect);
    }
    if (m_keyIsSet) {
        val["key"] = ModelUtils::ToJson(m_key);
    }
    if (m_timeAddedIsSet) {
        val["timeAdded"] = ModelUtils::ToJson(m_timeAdded);
    }
    if (m_valueIsSet) {
        val["value"] = ModelUtils::ToJson(m_value);
    }

    return val;
}

bool V1Taint::FromJson(const nlohmann::json &val)
{
    bool ok = true;
    if (ModelUtils::CheckHasKey(val, "effect")) {
        const nlohmann::json &fieldValue = val.at("effect");
        std::string refValSetEffect;
        ok &= ModelUtils::FromJson(fieldValue, refValSetEffect);
        SetEffect(refValSetEffect);
    }
    if (val.contains("key")) {
        const nlohmann::json &fieldValue = val.at("key");
        if (!fieldValue.is_null()) {
            std::string refValSetKey;
            ok &= ModelUtils::FromJson(fieldValue, refValSetKey);
            SetKey(refValSetKey);
        }
    }
    if (val.contains("timeAdded")) {
        const nlohmann::json& fieldValue = val.at("timeAdded");
        if (!fieldValue.is_null()) {
            utility::Datetime refValSetTimeAdded;
            ok &= ModelUtils::FromJson(fieldValue, refValSetTimeAdded);
            SetTimeAdded(refValSetTimeAdded);
        }
    }
    if (val.contains("value")) {
        const nlohmann::json& fieldValue = val.at("value");
        if (!fieldValue.is_null()) {
            std::string refValSetValue;
            ok &= ModelUtils::FromJson(fieldValue, refValSetValue);
            SetValue(refValSetValue);
        }
    }
    return ok;
}

std::string V1Taint::GetEffect() const
{
    return m_effect;
}

void V1Taint::SetEffect(const std::string &value)
{
    m_effect = value;
    m_effectIsSet = true;
}

bool V1Taint::EffectIsSet() const
{
    return m_effectIsSet;
}

void V1Taint::UnSetEffect()
{
    m_effectIsSet = false;
}
std::string V1Taint::GetKey() const
{
    return m_key;
}

void V1Taint::SetKey(const std::string &value)
{
    m_key = value;
    m_keyIsSet = true;
}

bool V1Taint::KeyIsSet() const
{
    return m_keyIsSet;
}

void V1Taint::UnsetKey()
{
    m_keyIsSet = false;
}
utility::Datetime V1Taint::GetTimeAdded() const
{
    return m_timeAdded;
}

void V1Taint::SetTimeAdded(const utility::Datetime &value)
{
    m_timeAdded = value;
    m_timeAddedIsSet = true;
}

bool V1Taint::TimeAddedIsSet() const
{
    return m_timeAddedIsSet;
}

void V1Taint::UnsetTimeAdded()
{
    m_timeAddedIsSet = false;
}
std::string V1Taint::GetValue() const
{
    return m_value;
}

void V1Taint::SetValue(const std::string &value)
{
    m_value = value;
    m_valueIsSet = true;
}

bool V1Taint::ValueIsSet() const
{
    return m_valueIsSet;
}

void V1Taint::UnsetValue()
{
    m_valueIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
