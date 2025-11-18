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

#include "v1_toleration.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1Toleration);

V1Toleration::V1Toleration()
{
    m_effect = std::string("");
    m_effectIsSet = false;
    m_key = std::string("");
    m_keyIsSet = false;
    m_rOperator = std::string("");
    m_rOperatorIsSet = false;
    m_tolerationSeconds = 0L;
    m_tolerationSecondsIsSet = false;
    m_value = std::string("");
    m_valueIsSet = false;
}

V1Toleration::~V1Toleration()
{
}

nlohmann::json V1Toleration::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_effectIsSet) {
        val["effect"] = ModelUtils::ToJson(m_effect);
    }
    if (m_keyIsSet) {
        val["key"] = ModelUtils::ToJson(m_key);
    }
    if (m_rOperatorIsSet) {
        val["operator"] = ModelUtils::ToJson(m_rOperator);
    }
    if (m_tolerationSecondsIsSet) {
        val["tolerationSeconds"] = ModelUtils::ToJson(m_tolerationSeconds);
    }
    if (m_valueIsSet) {
        val["value"] = ModelUtils::ToJson(m_value);
    }

    return val;
}

bool V1Toleration::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("effect")) {
        const nlohmann::json &fieldValue = val.at("effect");
        if (!fieldValue.is_null()) {
            std::string refValSetEffect;
            ok &= ModelUtils::FromJson(fieldValue, refValSetEffect);
            SetEffect(refValSetEffect);
        }
    }
    if (val.contains("key")) {
        const nlohmann::json &fieldValue = val.at("key");
        if (!fieldValue.is_null()) {
            std::string refValSetKey;
            ok &= ModelUtils::FromJson(fieldValue, refValSetKey);
            SetKey(refValSetKey);
        }
    }
    if (val.contains("operator")) {
        const nlohmann::json &fieldValue = val.at("operator");
        if (!fieldValue.is_null()) {
            std::string refValSetROperator;
            ok &= ModelUtils::FromJson(fieldValue, refValSetROperator);
            SetROperator(refValSetROperator);
        }
    }
    if (val.contains("tolerationSeconds")) {
        const nlohmann::json &fieldValue = val.at("tolerationSeconds");
        if (!fieldValue.is_null()) {
            int64_t refValSetTolerationSeconds;
            ok &= ModelUtils::FromJson(fieldValue, refValSetTolerationSeconds);
            SetTolerationSeconds(refValSetTolerationSeconds);
        }
    }
    if (ModelUtils::CheckHasKey(val, "value")) {
        const nlohmann::json &fieldValue = val.at("value");
        std::string refValSetValue;
        ok &= ModelUtils::FromJson(fieldValue, refValSetValue);
        SetValue(refValSetValue);
    }
    return ok;
}

std::string V1Toleration::GetEffect() const
{
    return m_effect;
}

void V1Toleration::SetEffect(const std::string &value)
{
    m_effect = value;
    m_effectIsSet = true;
}

bool V1Toleration::EffectIsSet() const
{
    return m_effectIsSet;
}

void V1Toleration::UnSetEffect()
{
    m_effectIsSet = false;
}
std::string V1Toleration::GetKey() const
{
    return m_key;
}

void V1Toleration::SetKey(const std::string &value)
{
    m_key = value;
    m_keyIsSet = true;
}

bool V1Toleration::KeyIsSet() const
{
    return m_keyIsSet;
}

void V1Toleration::UnsetKey()
{
    m_keyIsSet = false;
}
std::string V1Toleration::GetROperator() const
{
    return m_rOperator;
}

void V1Toleration::SetROperator(const std::string &value)
{
    m_rOperator = value;
    m_rOperatorIsSet = true;
}

bool V1Toleration::ROperatorIsSet() const
{
    return m_rOperatorIsSet;
}

void V1Toleration::UnsetROperator()
{
    m_rOperatorIsSet = false;
}
int64_t V1Toleration::GetTolerationSeconds() const
{
    return m_tolerationSeconds;
}

void V1Toleration::SetTolerationSeconds(int64_t value)
{
    m_tolerationSeconds = value;
    m_tolerationSecondsIsSet = true;
}

bool V1Toleration::TolerationSecondsIsSet() const
{
    return m_tolerationSecondsIsSet;
}

void V1Toleration::UnsetTolerationSeconds()
{
    m_tolerationSecondsIsSet = false;
}
std::string V1Toleration::GetValue() const
{
    return m_value;
}

void V1Toleration::SetValue(const std::string &value)
{
    m_value = value;
    m_valueIsSet = true;
}

bool V1Toleration::ValueIsSet() const
{
    return m_valueIsSet;
}

void V1Toleration::UnsetValue()
{
    m_valueIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
