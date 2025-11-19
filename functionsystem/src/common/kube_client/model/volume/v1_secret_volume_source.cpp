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

#include "v1_secret_volume_source.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1SecretVolumeSource);

V1SecretVolumeSource::V1SecretVolumeSource()
{
    m_defaultMode = 0;
    m_defaultModeIsSet = false;
    m_itemsIsSet = false;
    m_optional = false;
    m_optionalIsSet = false;
    m_secretName = std::string("");
    m_secretNameIsSet = false;
}

V1SecretVolumeSource::~V1SecretVolumeSource()
{
}

nlohmann::json V1SecretVolumeSource::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_defaultModeIsSet) {
        val["defaultMode"] = ModelUtils::ToJson(m_defaultMode);
    }
    if (m_itemsIsSet) {
        val["items"] = ModelUtils::ToJson(m_items);
    }
    if (m_optionalIsSet) {
        val["optional"] = ModelUtils::ToJson(m_optional);
    }
    if (m_secretNameIsSet) {
        val["secretName"] = ModelUtils::ToJson(m_secretName);
    }

    return val;
}

bool V1SecretVolumeSource::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (ModelUtils::CheckHasKey(val, "defaultMode")) {
        int64_t refValSetDefaultMode;
        ok &= ModelUtils::FromJson(val.at("defaultMode"), refValSetDefaultMode);
        SetDefaultMode(refValSetDefaultMode);
    }
    if (ModelUtils::CheckHasKey(val, "items")) {
        std::vector<std::shared_ptr<V1KeyToPath>> refValSetItems;
        ok &= ModelUtils::FromJson(val.at("items"), refValSetItems);
        SetItems(refValSetItems);
    }
    if (ModelUtils::CheckHasKey(val, "optional")) {
        bool refValSetOptional;
        ok &= ModelUtils::FromJson(val.at("optional"), refValSetOptional);
        SetOptional(refValSetOptional);
    }
    if (ModelUtils::CheckHasKey(val, "secretName")) {
        std::string refValSetSecretName;
        ok &= ModelUtils::FromJson(val.at("secretName"), refValSetSecretName);
        SetSecretName(refValSetSecretName);
    }
    return ok;
}

int64_t V1SecretVolumeSource::GetDefaultMode() const
{
    return m_defaultMode;
}

void V1SecretVolumeSource::SetDefaultMode(int64_t value)
{
    m_defaultMode = value;
    m_defaultModeIsSet = true;
}

bool V1SecretVolumeSource::DefaultModeIsSet() const
{
    return m_defaultModeIsSet;
}

void V1SecretVolumeSource::UnsetDefaultMode()
{
    m_defaultModeIsSet = false;
}
std::vector<std::shared_ptr<V1KeyToPath>> &V1SecretVolumeSource::GetItems()
{
    return m_items;
}

void V1SecretVolumeSource::SetItems(const std::vector<std::shared_ptr<V1KeyToPath>> &value)
{
    m_items = value;
    m_itemsIsSet = true;
}

bool V1SecretVolumeSource::ItemsIsSet() const
{
    return m_itemsIsSet;
}

void V1SecretVolumeSource::UnsetItems()
{
    m_itemsIsSet = false;
}
bool V1SecretVolumeSource::IsOptional() const
{
    return m_optional;
}

void V1SecretVolumeSource::SetOptional(bool value)
{
    m_optional = value;
    m_optionalIsSet = true;
}

bool V1SecretVolumeSource::OptionalIsSet() const
{
    return m_optionalIsSet;
}

void V1SecretVolumeSource::UnsetOptional()
{
    m_optionalIsSet = false;
}
std::string V1SecretVolumeSource::GetSecretName() const
{
    return m_secretName;
}

void V1SecretVolumeSource::SetSecretName(const std::string &value)
{
    m_secretName = value;
    m_secretNameIsSet = true;
}

bool V1SecretVolumeSource::SecretNameIsSet() const
{
    return m_secretNameIsSet;
}

void V1SecretVolumeSource::UnSetSecretName()
{
    m_secretNameIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
