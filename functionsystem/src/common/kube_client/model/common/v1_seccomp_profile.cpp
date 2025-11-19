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

#include "v1_seccomp_profile.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1SeccompProfile);

V1SeccompProfile::V1SeccompProfile()
{
    m_localhostProfile = "";
    m_localhostProfileIsSet = false;
    m_type = "";
    m_typeIsSet = false;
}

V1SeccompProfile::~V1SeccompProfile()
{
}

nlohmann::json V1SeccompProfile::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    if (m_localhostProfileIsSet) {
        val["localhostProfile"] = ModelUtils::ToJson(m_localhostProfile);
    }
    if (m_typeIsSet) {
        val["type"] = ModelUtils::ToJson(m_type);
    }
    return val;
}

bool V1SeccompProfile::FromJson(const nlohmann::json& val)
{
    bool ok = true;

    if (val.contains("localhostProfile")) {
        const nlohmann::json &fieldValue = val.at("localhostProfile");
        if (!fieldValue.is_null()) {
            std::string refValSetLocalhostProfile;
            ok &= ModelUtils::FromJson(fieldValue, refValSetLocalhostProfile);
            SetLocalhostProfile(refValSetLocalhostProfile);
        }
    }
    if (val.contains("type")) {
        const nlohmann::json &fieldValue = val.at("type");
        if (!fieldValue.is_null()) {
            std::string refValSetType;
            ok &= ModelUtils::FromJson(fieldValue, refValSetType);
            SetType(refValSetType);
        }
    }
    return ok;
}

std::string V1SeccompProfile::GetLocalhostProfile() const
{
    return m_localhostProfile;
}

void V1SeccompProfile::SetLocalhostProfile(const std::string& value)
{
    m_localhostProfile = value;
    m_localhostProfileIsSet = true;
}

bool V1SeccompProfile::LocalhostProfileIsSet() const
{
    return m_localhostProfileIsSet;
}

void V1SeccompProfile::UnsetLocalhostProfile()
{
    m_localhostProfileIsSet = false;
}
std::string V1SeccompProfile::GetType() const
{
    return m_type;
}

void V1SeccompProfile::SetType(const std::string& value)
{
    m_type = value;
    m_typeIsSet = true;
}

bool V1SeccompProfile::TypeIsSet() const
{
    return m_typeIsSet;
}

void V1SeccompProfile::UnsetType()
{
    m_typeIsSet = false;
}
}
}
