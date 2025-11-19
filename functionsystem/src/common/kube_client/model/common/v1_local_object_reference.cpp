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

#include "v1_local_object_reference.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1LocalObjectReference);

V1LocalObjectReference::V1LocalObjectReference()
{
    m_name = std::string("");
    m_nameIsSet = false;
}

V1LocalObjectReference::~V1LocalObjectReference()
{
}

nlohmann::json V1LocalObjectReference::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_nameIsSet) {
        val["name"] = ModelUtils::ToJson(m_name);
    }

    return val;
}

bool V1LocalObjectReference::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("name")) {
        const nlohmann::json &fieldValue = val.at("name");
        if (!fieldValue.is_null()) {
            std::string refValSetName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetName);
            SetName(refValSetName);
        }
    }
    return ok;
}

std::string V1LocalObjectReference::GetName() const
{
    return m_name;
}

void V1LocalObjectReference::SetName(const std::string &value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V1LocalObjectReference::NameIsSet() const
{
    return m_nameIsSet;
}

void V1LocalObjectReference::UnsetName()
{
    m_nameIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
