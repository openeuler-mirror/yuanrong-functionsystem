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

#include "v1_config_map_volume_source.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1ConfigMapVolumeSource);

V1ConfigMapVolumeSource::V1ConfigMapVolumeSource()
{
    m_defaultMode = 0;
    m_defaultModeIsSet = false;
    m_itemsIsSet = false;
    m_name = std::string("");
    m_nameIsSet = false;
    m_optional = false;
    m_optionalIsSet = false;
}

V1ConfigMapVolumeSource::~V1ConfigMapVolumeSource()
{
}

nlohmann::json V1ConfigMapVolumeSource::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_defaultModeIsSet) {
        val["defaultMode"] = ModelUtils::ToJson(m_defaultMode);
    }
    if (m_itemsIsSet) {
        val["items"] = ModelUtils::ToJson(m_items);
    }
    if (m_nameIsSet) {
        val["name"] = ModelUtils::ToJson(m_name);
    }
    if (m_optionalIsSet) {
        val["optional"] = ModelUtils::ToJson(m_optional);
    }

    return val;
}

bool V1ConfigMapVolumeSource::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("defaultMode")) {
        const nlohmann::json &fieldValue = val.at("defaultMode");
        if (!fieldValue.is_null()) {
            int64_t refValSetDefaultMode;
            ok &= ModelUtils::FromJson(fieldValue, refValSetDefaultMode);
            SetDefaultMode(refValSetDefaultMode);
        }
    }
    if (val.contains("items")) {
        const nlohmann::json &fieldValue = val.at("items");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1KeyToPath>> refValSetItems;
            ok &= ModelUtils::FromJson(fieldValue, refValSetItems);
            SetItems(refValSetItems);
        }
    }
    if (ModelUtils::CheckHasKey(val, "name")) {
        std::string refValSetName;
        ok &= ModelUtils::FromJson(val.at("name"), refValSetName);
        SetName(refValSetName);
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

int64_t V1ConfigMapVolumeSource::GetDefaultMode() const
{
    return m_defaultMode;
}

void V1ConfigMapVolumeSource::SetDefaultMode(int64_t value)
{
    m_defaultMode = value;
    m_defaultModeIsSet = true;
}

bool V1ConfigMapVolumeSource::DefaultModeIsSet() const
{
    return m_defaultModeIsSet;
}

void V1ConfigMapVolumeSource::UnsetDefaultMode()
{
    m_defaultModeIsSet = false;
}
std::vector<std::shared_ptr<V1KeyToPath>> &V1ConfigMapVolumeSource::GetItems()
{
    return m_items;
}

void V1ConfigMapVolumeSource::SetItems(const std::vector<std::shared_ptr<V1KeyToPath>> &value)
{
    m_items = value;
    m_itemsIsSet = true;
}

bool V1ConfigMapVolumeSource::ItemsIsSet() const
{
    return m_itemsIsSet;
}

void V1ConfigMapVolumeSource::UnsetItems()
{
    m_itemsIsSet = false;
}
std::string V1ConfigMapVolumeSource::GetName() const
{
    return m_name;
}

void V1ConfigMapVolumeSource::SetName(const std::string &value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V1ConfigMapVolumeSource::NameIsSet() const
{
    return m_nameIsSet;
}

void V1ConfigMapVolumeSource::UnsetName()
{
    m_nameIsSet = false;
}
bool V1ConfigMapVolumeSource::IsOptional() const
{
    return m_optional;
}

void V1ConfigMapVolumeSource::SetOptional(bool value)
{
    m_optional = value;
    m_optionalIsSet = true;
}

bool V1ConfigMapVolumeSource::OptionalIsSet() const
{
    return m_optionalIsSet;
}

void V1ConfigMapVolumeSource::UnsetOptional()
{
    m_optionalIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
