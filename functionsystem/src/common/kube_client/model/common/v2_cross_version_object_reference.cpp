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

#include "v2_cross_version_object_reference.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2CrossVersionObjectReference);

V2CrossVersionObjectReference::V2CrossVersionObjectReference() : m_apiVersion(std::string("")),
    m_apiVersionIsSet(false),
    m_kind(std::string("")),
    m_kindIsSet(false),
    m_name(std::string("")),
    m_nameIsSet(false)
{}

V2CrossVersionObjectReference::~V2CrossVersionObjectReference()
{
}

nlohmann::json V2CrossVersionObjectReference::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    
    if (m_apiVersionIsSet) {
        val[std::string(("apiVersion"))] = ModelUtils::ToJson(m_apiVersion);
    }
    if (m_kindIsSet) {
        val[std::string(("kind"))] = ModelUtils::ToJson(m_kind);
    }
    if (m_nameIsSet) {
        val[std::string(("name"))] = ModelUtils::ToJson(m_name);
    }

    return val;
}

bool V2CrossVersionObjectReference::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    if (val.contains(std::string(("name")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("name")));
        if (!fieldValue.is_null()) {
            std::string refValSetName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetName);
            SetName(refValSetName);
        }
    }
    if (val.contains(std::string(("apiVersion")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("apiVersion")));
        if (!fieldValue.is_null()) {
            std::string refValSetApiVersion;
            ok &= ModelUtils::FromJson(fieldValue, refValSetApiVersion);
            SetApiVersion(refValSetApiVersion);
        }
    }
    if (val.contains(std::string(("kind")))) {
        const nlohmann::json& fieldValue = val.at(std::string("kind"));
        if (!fieldValue.is_null()) {
            std::string refValSetKind;
            ok &= ModelUtils::FromJson(fieldValue, refValSetKind);
            SetKind(refValSetKind);
        }
    }
    return ok;
}

std::string V2CrossVersionObjectReference::GetApiVersion() const
{
    return m_apiVersion;
}

void V2CrossVersionObjectReference::SetApiVersion(const std::string& value)
{
    m_apiVersion = value;
    m_apiVersionIsSet = true;
}

bool V2CrossVersionObjectReference::ApiVersionIsSet() const
{
    return m_apiVersionIsSet;
}

void V2CrossVersionObjectReference::UnsetApiVersion()
{
    m_apiVersionIsSet = false;
}
std::string V2CrossVersionObjectReference::GetKind() const
{
    return m_kind;
}

void V2CrossVersionObjectReference::SetKind(const std::string& value)
{
    m_kind = value;
    m_kindIsSet = true;
}

bool V2CrossVersionObjectReference::KindIsSet() const
{
    return m_kindIsSet;
}

void V2CrossVersionObjectReference::UnsetKind()
{
    m_kindIsSet = false;
}
std::string V2CrossVersionObjectReference::GetName() const
{
    return m_name;
}

void V2CrossVersionObjectReference::SetName(const std::string& value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V2CrossVersionObjectReference::NameIsSet() const
{
    return m_nameIsSet;
}

void V2CrossVersionObjectReference::UnsetName()
{
    m_nameIsSet = false;
}
}
}
