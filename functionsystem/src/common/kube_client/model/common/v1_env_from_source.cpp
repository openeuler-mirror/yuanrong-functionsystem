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

#include "v1_env_from_source.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1EnvFromSource);

V1EnvFromSource::V1EnvFromSource()
    : m_configMapRef(nullptr),
      m_configMapRefIsSet(false),
      m_prefix(std::string("")),
      m_prefixIsSet(false),
      m_secretRef(nullptr),
      m_secretRefIsSet(false)
{
}

V1EnvFromSource::~V1EnvFromSource()
{
}

nlohmann::json V1EnvFromSource::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_configMapRefIsSet) {
        val["configMapRef"] = ModelUtils::ToJson(m_configMapRef);
    }
    if (m_prefixIsSet) {
        val["prefix"] = ModelUtils::ToJson(m_prefix);
    }
    if (m_secretRefIsSet) {
        val["secretRef"] = ModelUtils::ToJson(m_secretRef);
    }

    return val;
}

bool V1EnvFromSource::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("configMapRef")) {
        const nlohmann::json &fieldValue = val.at("configMapRef");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ConfigMapEnvSource> refVal_setConfigMapRef;
            ok &= ModelUtils::FromJson(fieldValue, refVal_setConfigMapRef);
            SetConfigMapRef(refVal_setConfigMapRef);
        }
    }
    if (val.contains("prefix")) {
        const nlohmann::json &fieldValue = val.at("prefix");
        if (!fieldValue.is_null()) {
            std::string refValSetPrefix;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPrefix);
            SetPrefix(refValSetPrefix);
        }
    }
    if (val.contains("secretRef")) {
        const nlohmann::json &fieldValue = val.at("secretRef");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1SecretEnvSource> refVal_setSecretRef;
            ok &= ModelUtils::FromJson(fieldValue, refVal_setSecretRef);
            SetSecretRef(refVal_setSecretRef);
        }
    }
    return ok;
}

std::shared_ptr<V1ConfigMapEnvSource> V1EnvFromSource::GetConfigMapRef() const
{
    return m_configMapRef;
}

void V1EnvFromSource::SetConfigMapRef(const std::shared_ptr<V1ConfigMapEnvSource> &value)
{
    m_configMapRef = value;
    m_configMapRefIsSet = true;
}
bool V1EnvFromSource::ConfigMapRefIsSet() const
{
    return m_configMapRefIsSet;
}

void V1EnvFromSource::UnsetConfigMapRef()
{
    m_configMapRefIsSet = false;
}

std::string V1EnvFromSource::GetPrefix() const
{
    return m_prefix;
}

void V1EnvFromSource::SetPrefix(const std::string &value)
{
    m_prefix = value;
    m_prefixIsSet = true;
}

bool V1EnvFromSource::PrefixIsSet() const
{
    return m_prefixIsSet;
}

void V1EnvFromSource::UnsetPrefix()
{
    m_prefixIsSet = false;
}

std::shared_ptr<V1SecretEnvSource> V1EnvFromSource::GetSecretRef() const
{
    return m_secretRef;
}

void V1EnvFromSource::SetSecretRef(const std::shared_ptr<V1SecretEnvSource> &value)
{
    m_secretRef = value;
    m_secretRefIsSet = true;
}

bool V1EnvFromSource::SecretRefIsSet() const
{
    return m_secretRefIsSet;
}

void V1EnvFromSource::UnsetSecretRef()
{
    m_secretRefIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
