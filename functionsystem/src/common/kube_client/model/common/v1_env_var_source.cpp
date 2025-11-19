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

#include "v1_env_var_source.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1EnvVarSource);

V1EnvVarSource::V1EnvVarSource()
{
    m_fieldRefIsSet = false;
    m_resourceFieldRefIsSet = false;
    m_secretKeyRefIsSet = false;
}

V1EnvVarSource::~V1EnvVarSource()
{
}

nlohmann::json V1EnvVarSource::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_fieldRefIsSet) {
        val["fieldRef"] = ModelUtils::ToJson(m_fieldRef);
    }
    if (m_resourceFieldRefIsSet) {
        val["resourceFieldRef"] = ModelUtils::ToJson(m_resourceFieldRef);
    }
    if (m_secretKeyRefIsSet) {
        val["secretKeyRef"] = ModelUtils::ToJson(m_secretKeyRef);
    }
    return val;
}

bool V1EnvVarSource::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("fieldRef")) {
        const nlohmann::json &fieldValue = val.at("fieldRef");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ObjectFieldSelector> refValSetFieldRef;
            ok &= ModelUtils::FromJson(fieldValue, refValSetFieldRef);
            SetFieldRef(refValSetFieldRef);
        }
    }
    if (val.contains("resourceFieldRef")) {
        const nlohmann::json &fieldValue = val.at("resourceFieldRef");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ResourceFieldSelector> refValSetResourceFieldRef;
            ok &= ModelUtils::FromJson(fieldValue, refValSetResourceFieldRef);
            SetResourceFieldRef(refValSetResourceFieldRef);
        }
    }
    if (val.contains("secretKeyRef")) {
        const nlohmann::json &fieldValue = val.at("secretKeyRef");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1SecretKeySelector> refValSetSecretKeyRef;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSecretKeyRef);
            SetSecretKeyRef(refValSetSecretKeyRef);
        }
    }
    return ok;
}

std::shared_ptr<V1ObjectFieldSelector> V1EnvVarSource::GetFieldRef() const
{
    return m_fieldRef;
}

void V1EnvVarSource::SetFieldRef(const std::shared_ptr<V1ObjectFieldSelector> &value)
{
    m_fieldRef = value;
    m_fieldRefIsSet = true;
}

bool V1EnvVarSource::FieldRefIsSet() const
{
    return m_fieldRefIsSet;
}

void V1EnvVarSource::UnsetFieldRef()
{
    m_fieldRefIsSet = false;
}
std::shared_ptr<V1ResourceFieldSelector> V1EnvVarSource::GetResourceFieldRef() const
{
    return m_resourceFieldRef;
}

void V1EnvVarSource::SetResourceFieldRef(const std::shared_ptr<V1ResourceFieldSelector> &value)
{
    m_resourceFieldRef = value;
    m_resourceFieldRefIsSet = true;
}

bool V1EnvVarSource::ResourceFieldRefIsSet() const
{
    return m_resourceFieldRefIsSet;
}

void V1EnvVarSource::UnsetResourceFieldRef()
{
    m_resourceFieldRefIsSet = false;
}

std::shared_ptr<V1SecretKeySelector> V1EnvVarSource::GetSecretKeyRef() const
{
    return m_secretKeyRef;
}

void V1EnvVarSource::SetSecretKeyRef(const std::shared_ptr<V1SecretKeySelector>& value)
{
    m_secretKeyRef = value;
    m_secretKeyRefIsSet = true;
}

bool V1EnvVarSource::SecretKeyRefIsSet() const
{
    return m_secretKeyRefIsSet;
}

void V1EnvVarSource::UnsetSecretKeyRef()
{
    m_secretKeyRefIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
