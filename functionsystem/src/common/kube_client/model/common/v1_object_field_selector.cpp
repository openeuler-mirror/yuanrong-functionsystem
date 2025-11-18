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

#include "v1_object_field_selector.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1ObjectFieldSelector);

V1ObjectFieldSelector::V1ObjectFieldSelector()
{
    m_apiVersion = std::string("");
    m_apiVersionIsSet = false;
    m_fieldPath = std::string("");
    m_fieldPathIsSet = false;
}

V1ObjectFieldSelector::~V1ObjectFieldSelector()
{
}

nlohmann::json V1ObjectFieldSelector::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_apiVersionIsSet) {
        val["apiVersion"] = ModelUtils::ToJson(m_apiVersion);
    }
    if (m_fieldPathIsSet) {
        val["fieldPath"] = ModelUtils::ToJson(m_fieldPath);
    }

    return val;
}

bool V1ObjectFieldSelector::FromJson(const nlohmann::json &val)
{
    bool ok = true;
    if (val.contains("fieldPath")) {
        const nlohmann::json &fieldValue = val.at("fieldPath");
        if (!fieldValue.is_null()) {
            std::string refValSetFieldPath;
            ok &= ModelUtils::FromJson(fieldValue, refValSetFieldPath);
            SetFieldPath(refValSetFieldPath);
        }
    }
    if (val.contains("apiVersion")) {
        const nlohmann::json &fieldValue = val.at("apiVersion");
        if (!fieldValue.is_null()) {
            std::string refValSetApiVersion;
            ok &= ModelUtils::FromJson(fieldValue, refValSetApiVersion);
            SetApiVersion(refValSetApiVersion);
        }
    }
    return ok;
}

std::string V1ObjectFieldSelector::GetApiVersion() const
{
    return m_apiVersion;
}

void V1ObjectFieldSelector::SetApiVersion(const std::string &value)
{
    m_apiVersion = value;
    m_apiVersionIsSet = true;
}

bool V1ObjectFieldSelector::ApiVersionIsSet() const
{
    return m_apiVersionIsSet;
}

void V1ObjectFieldSelector::UnsetApiVersion()
{
    m_apiVersionIsSet = false;
}
std::string V1ObjectFieldSelector::GetFieldPath() const
{
    return m_fieldPath;
}

void V1ObjectFieldSelector::SetFieldPath(const std::string &value)
{
    m_fieldPath = value;
    m_fieldPathIsSet = true;
}

bool V1ObjectFieldSelector::FieldPathIsSet() const
{
    return m_fieldPathIsSet;
}

void V1ObjectFieldSelector::UnsetFieldPath()
{
    m_fieldPathIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
