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

#include "v1_node_status.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1NodeStatus);

V1NodeStatus::V1NodeStatus()
{
    m_addressesIsSet = false;
}

V1NodeStatus::~V1NodeStatus()
{
}

nlohmann::json V1NodeStatus::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_addressesIsSet) {
        val["addresses"] = ModelUtils::ToJson(m_addresses);
    }

    return val;
}

bool V1NodeStatus::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("addresses")) {
        const nlohmann::json &fieldValue = val.at("addresses");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1NodeAddress>> refValSetAddresses;
            ok &= ModelUtils::FromJson(fieldValue, refValSetAddresses);
            SetAddresses(refValSetAddresses);
        }
    }
    return ok;
}

std::vector<std::shared_ptr<V1NodeAddress>> &V1NodeStatus::GetAddresses()
{
    return m_addresses;
}

void V1NodeStatus::SetAddresses(const std::vector<std::shared_ptr<V1NodeAddress>> &value)
{
    m_addresses = value;
    m_addressesIsSet = true;
}

bool V1NodeStatus::AddressesIsSet() const
{
    return m_addressesIsSet;
}

void V1NodeStatus::UnsetAddresses()
{
    m_addressesIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
