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

#include "v1_capabilities.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1Capabilities);

V1Capabilities::V1Capabilities()
{
    m_addIsSet = false;
    m_dropIsSet = false;
}

V1Capabilities::~V1Capabilities()
{
}

nlohmann::json V1Capabilities::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_addIsSet) {
        val["add"] = ModelUtils::ToJson(m_add);
    }
    if (m_dropIsSet) {
        val["drop"] = ModelUtils::ToJson(m_drop);
    }

    return val;
}

bool V1Capabilities::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("add")) {
        const nlohmann::json &fieldValue = val.at("add");
        if (!fieldValue.is_null()) {
            std::vector<std::string> refValSetAdd;
            ok &= ModelUtils::FromJson(fieldValue, refValSetAdd);
            SetAdd(refValSetAdd);
        }
    }
    if (val.contains("drop")) {
        const nlohmann::json &fieldValue = val.at("drop");
        if (!fieldValue.is_null()) {
            std::vector<std::string> refValSetDrop;
            ok &= ModelUtils::FromJson(fieldValue, refValSetDrop);
            SetDrop(refValSetDrop);
        }
    }
    return ok;
}

void V1Capabilities::SetAdd(const std::vector<std::string> &value)
{
    m_add = value;
    m_addIsSet = true;
}

bool V1Capabilities::AddIsSet() const
{
    return m_addIsSet;
}

void V1Capabilities::UnsetAdd()
{
    m_addIsSet = false;
}

void V1Capabilities::SetDrop(const std::vector<std::string> &value)
{
    m_drop = value;
    m_dropIsSet = true;
}

bool V1Capabilities::DropIsSet() const
{
    return m_dropIsSet;
}

void V1Capabilities::UnsetDrop()
{
    m_dropIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
