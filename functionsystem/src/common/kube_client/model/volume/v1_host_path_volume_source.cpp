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

#include "v1_host_path_volume_source.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1HostPathVolumeSource);

V1HostPathVolumeSource::V1HostPathVolumeSource()
{
    m_path = std::string("");
    m_pathIsSet = false;
    m_type = std::string("");
    m_typeIsSet = false;
}

V1HostPathVolumeSource::~V1HostPathVolumeSource()
{
}

nlohmann::json V1HostPathVolumeSource::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_pathIsSet) {
        val["path"] = ModelUtils::ToJson(m_path);
    }
    if (m_typeIsSet) {
        val["type"] = ModelUtils::ToJson(m_type);
    }

    return val;
}

bool V1HostPathVolumeSource::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("path")) {
        const nlohmann::json &fieldValue = val.at("path");
        if (!fieldValue.is_null()) {
            std::string refValSetPath;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPath);
            SetPath(refValSetPath);
        }
    }
    if (ModelUtils::CheckHasKey(val, "type")) {
        std::string refValSetType;
        ok &= ModelUtils::FromJson(val.at("type"), refValSetType);
        SetType(refValSetType);
    }
    return ok;
}

std::string V1HostPathVolumeSource::GetPath() const
{
    return m_path;
}

void V1HostPathVolumeSource::SetPath(const std::string &value)
{
    m_path = value;
    m_pathIsSet = true;
}

bool V1HostPathVolumeSource::PathIsSet() const
{
    return m_pathIsSet;
}

void V1HostPathVolumeSource::UnsetPath()
{
    m_pathIsSet = false;
}
std::string V1HostPathVolumeSource::GetType() const
{
    return m_type;
}

void V1HostPathVolumeSource::SetType(const std::string &value)
{
    m_type = value;
    m_typeIsSet = true;
}

bool V1HostPathVolumeSource::TypeIsSet() const
{
    return m_typeIsSet;
}

void V1HostPathVolumeSource::UnsetType()
{
    m_typeIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
