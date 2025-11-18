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

#include "v1_persistent_volume_claim_volume_source.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1PersistentVolumeClaimVolumeSource);

V1PersistentVolumeClaimVolumeSource::V1PersistentVolumeClaimVolumeSource()
{
    m_claimName = "";
    m_claimNameIsSet = false;
    m_readOnly = false;
    m_readOnlyIsSet = false;
}

V1PersistentVolumeClaimVolumeSource::~V1PersistentVolumeClaimVolumeSource()
{
}

nlohmann::json V1PersistentVolumeClaimVolumeSource::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    if (m_claimNameIsSet) {
        val["claimName"] = ModelUtils::ToJson(m_claimName);
    }
    if (m_readOnlyIsSet) {
        val["readOnly"] = ModelUtils::ToJson(m_readOnly);
    }
    return val;
}

bool V1PersistentVolumeClaimVolumeSource::FromJson(const nlohmann::json &val)
{
    bool ok = true;
    if (val.contains("claimName")) {
        const nlohmann::json &fieldValue = val.at("claimName");
        if (!fieldValue.is_null()) {
            std::string refValSetClaimName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetClaimName);
            SetClaimName(refValSetClaimName);
        }
    }
    if (val.contains("readOnly")) {
        const nlohmann::json &fieldValue = val.at("readOnly");
        if (!fieldValue.is_null()) {
            bool refValSetReadOnly;
            ok &= ModelUtils::FromJson(fieldValue, refValSetReadOnly);
            SetReadOnly(refValSetReadOnly);
        }
    }
    return ok;
}

std::string V1PersistentVolumeClaimVolumeSource::GetClaimName() const
{
    return m_claimName;
}

void V1PersistentVolumeClaimVolumeSource::SetClaimName(const std::string &value)
{
    m_claimName = value;
    m_claimNameIsSet = true;
}

bool V1PersistentVolumeClaimVolumeSource::ClaimNameIsSet() const
{
    return m_claimNameIsSet;
}

void V1PersistentVolumeClaimVolumeSource::UnsetClaimName()
{
    m_claimNameIsSet = false;
}
bool V1PersistentVolumeClaimVolumeSource::IsReadOnly() const
{
    return m_readOnly;
}

void V1PersistentVolumeClaimVolumeSource::SetReadOnly(bool value)
{
    m_readOnly = value;
    m_readOnlyIsSet = true;
}

bool V1PersistentVolumeClaimVolumeSource::ReadOnlyIsSet() const
{
    return m_readOnlyIsSet;
}

void V1PersistentVolumeClaimVolumeSource::UnsetReadOnly()
{
    m_readOnlyIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client