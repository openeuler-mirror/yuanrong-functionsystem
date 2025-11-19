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

#include "v1_downward_api_volume_source.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1DownwardAPIVolumeSource);

V1DownwardAPIVolumeSource::V1DownwardAPIVolumeSource()
{
    m_defaultMode = 0;
    m_defaultModeIsSet = false;
    m_itemsIsSet = false;
}

V1DownwardAPIVolumeSource::~V1DownwardAPIVolumeSource()
{
}

nlohmann::json V1DownwardAPIVolumeSource::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_defaultModeIsSet) {
        val["defaultMode"] = ModelUtils::ToJson(m_defaultMode);
    }
    if (m_itemsIsSet) {
        val["items"] = ModelUtils::ToJson(m_items);
    }

    return val;
}

bool V1DownwardAPIVolumeSource::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("defaultMode")) {
        const nlohmann::json &fieldValue = val.at("defaultMode");
        if (!fieldValue.is_null()) {
            int32_t refValSetDefaultMode;
            ok &= ModelUtils::FromJson(fieldValue, refValSetDefaultMode);
            SetDefaultMode(refValSetDefaultMode);
        }
    }
    if (val.contains("items")) {
        const nlohmann::json &fieldValue = val.at("items");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1DownwardAPIVolumeFile>> refValSetItems;
            ok &= ModelUtils::FromJson(fieldValue, refValSetItems);
            SetItems(refValSetItems);
        }
    }
    return ok;
}

int32_t V1DownwardAPIVolumeSource::GetDefaultMode() const
{
    return m_defaultMode;
}

void V1DownwardAPIVolumeSource::SetDefaultMode(int32_t value)
{
    m_defaultMode = value;
    m_defaultModeIsSet = true;
}

bool V1DownwardAPIVolumeSource::DefaultModeIsSet() const
{
    return m_defaultModeIsSet;
}

void V1DownwardAPIVolumeSource::UnsetDefaultMode()
{
    m_defaultModeIsSet = false;
}
std::vector<std::shared_ptr<V1DownwardAPIVolumeFile>> &V1DownwardAPIVolumeSource::GetItems()
{
    return m_items;
}

void V1DownwardAPIVolumeSource::SetItems(const std::vector<std::shared_ptr<V1DownwardAPIVolumeFile>> &value)
{
    m_items = value;
    m_itemsIsSet = true;
}

bool V1DownwardAPIVolumeSource::ItemsIsSet() const
{
    return m_itemsIsSet;
}

void V1DownwardAPIVolumeSource::UnsetItems()
{
    m_itemsIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
