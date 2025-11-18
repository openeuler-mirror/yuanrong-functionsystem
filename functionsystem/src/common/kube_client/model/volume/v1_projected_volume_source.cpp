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

#include "v1_projected_volume_source.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1ProjectedVolumeSource);

V1ProjectedVolumeSource::V1ProjectedVolumeSource()
{
    m_defaultMode = 0;
    m_defaultModeIsSet = false;
    m_sourcesIsSet = false;
}

V1ProjectedVolumeSource::~V1ProjectedVolumeSource()
{
}

nlohmann::json V1ProjectedVolumeSource::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_defaultModeIsSet) {
        val["defaultMode"] = ModelUtils::ToJson(m_defaultMode);
    }
    if (m_sourcesIsSet) {
        val["sources"] = ModelUtils::ToJson(m_sources);
    }

    return val;
}

bool V1ProjectedVolumeSource::FromJson(const nlohmann::json &val)
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
    if (val.contains("sources")) {
        const nlohmann::json &fieldValue = val.at("sources");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1VolumeProjection>> refValSetSources;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSources);
            SetSources(refValSetSources);
        }
    }
    return ok;
}

int32_t V1ProjectedVolumeSource::GetDefaultMode() const
{
    return m_defaultMode;
}

void V1ProjectedVolumeSource::SetDefaultMode(int32_t value)
{
    m_defaultMode = value;
    m_defaultModeIsSet = true;
}

bool V1ProjectedVolumeSource::DefaultModeIsSet() const
{
    return m_defaultModeIsSet;
}

void V1ProjectedVolumeSource::UnsetDefaultMode()
{
    m_defaultModeIsSet = false;
}
std::vector<std::shared_ptr<V1VolumeProjection>> &V1ProjectedVolumeSource::GetSources()
{
    return m_sources;
}

void V1ProjectedVolumeSource::SetSources(const std::vector<std::shared_ptr<V1VolumeProjection>> &value)
{
    m_sources = value;
    m_sourcesIsSet = true;
}

bool V1ProjectedVolumeSource::SourcesIsSet() const
{
    return m_sourcesIsSet;
}

void V1ProjectedVolumeSource::UnsetSources()
{
    m_sourcesIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
