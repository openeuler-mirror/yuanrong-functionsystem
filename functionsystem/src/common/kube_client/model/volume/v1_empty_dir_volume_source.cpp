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

#include "v1_empty_dir_volume_source.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1EmptyDirVolumeSource);

V1EmptyDirVolumeSource::V1EmptyDirVolumeSource()
{
    m_medium = std::string("");
    m_mediumIsSet = false;
    m_sizeLimit = std::string("");
    m_sizeLimitIsSet = false;
}

V1EmptyDirVolumeSource::~V1EmptyDirVolumeSource()
{
}

nlohmann::json V1EmptyDirVolumeSource::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_mediumIsSet) {
        val["medium"] = ModelUtils::ToJson(m_medium);
    }
    if (m_sizeLimitIsSet) {
        val["sizeLimit"] = ModelUtils::ToJson(m_sizeLimit);
    }

    return val;
}

bool V1EmptyDirVolumeSource::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("medium")) {
        const nlohmann::json &fieldValue = val.at("medium");
        if (!fieldValue.is_null()) {
            std::string refValSetMedium;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMedium);
            SetMedium(refValSetMedium);
        }
    }
    if (val.contains("sizeLimit")) {
        const nlohmann::json &fieldValue = val.at("sizeLimit");
        if (!fieldValue.is_null()) {
            std::string refValSetSizeLimit;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSizeLimit);
            SetSizeLimit(refValSetSizeLimit);
        }
    }
    return ok;
}

std::string V1EmptyDirVolumeSource::GetMedium() const
{
    return m_medium;
}

void V1EmptyDirVolumeSource::SetMedium(const std::string &value)
{
    m_medium = value;
    m_mediumIsSet = true;
}

bool V1EmptyDirVolumeSource::MediumIsSet() const
{
    return m_mediumIsSet;
}

void V1EmptyDirVolumeSource::UnsetMedium()
{
    m_mediumIsSet = false;
}
std::string V1EmptyDirVolumeSource::GetSizeLimit() const
{
    return m_sizeLimit;
}

void V1EmptyDirVolumeSource::SetSizeLimit(const std::string &value)
{
    m_sizeLimit = value;
    m_sizeLimitIsSet = true;
}

bool V1EmptyDirVolumeSource::SizeLimitIsSet() const
{
    return m_sizeLimitIsSet;
}

void V1EmptyDirVolumeSource::UnsetSizeLimit()
{
    m_sizeLimitIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
