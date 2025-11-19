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

#include "v1_node_address.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1NodeAddress);

V1NodeAddress::V1NodeAddress()
{
    m_address = std::string("");
    m_addressIsSet = false;
    m_type = std::string("");
    m_typeIsSet = false;
}

V1NodeAddress::~V1NodeAddress()
{
}

nlohmann::json V1NodeAddress::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_addressIsSet) {
        val["address"] = ModelUtils::ToJson(m_address);
    }
    if (m_typeIsSet) {
        val["type"] = ModelUtils::ToJson(m_type);
    }

    return val;
}

bool V1NodeAddress::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("address")) {
        const nlohmann::json &fieldValue = val.at("address");
        if (!fieldValue.is_null()) {
            std::string refValSetAddress;
            ok &= ModelUtils::FromJson(fieldValue, refValSetAddress);
            SetAddress(refValSetAddress);
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

std::string V1NodeAddress::GetAddress() const
{
    return m_address;
}

void V1NodeAddress::SetAddress(const std::string &value)
{
    m_address = value;
    m_addressIsSet = true;
}

bool V1NodeAddress::AddressIsSet() const
{
    return m_addressIsSet;
}

void V1NodeAddress::UnsetAddress()
{
    m_addressIsSet = false;
}
std::string V1NodeAddress::GetType() const
{
    return m_type;
}

void V1NodeAddress::SetType(const std::string &value)
{
    m_type = value;
    m_typeIsSet = true;
}

bool V1NodeAddress::TypeIsSet() const
{
    return m_typeIsSet;
}

void V1NodeAddress::UnsetType()
{
    m_typeIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
