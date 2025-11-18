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

#include "v1_host_alias.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1HostAlias);

V1HostAlias::V1HostAlias()
{
    m_hostnamesIsSet = false;
    m_ip = std::string("");
    m_ipIsSet = false;
}

V1HostAlias::~V1HostAlias()
{
}

nlohmann::json V1HostAlias::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_hostnamesIsSet) {
        val["hostnames"] = ModelUtils::ToJson(m_hostnames);
    }
    if (m_ipIsSet) {
        val["ip"] = ModelUtils::ToJson(m_ip);
    }

    return val;
}

bool V1HostAlias::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("hostnames")) {
        const nlohmann::json &fieldValue = val.at("hostnames");
        if (!fieldValue.is_null()) {
            std::vector<std::string> refValSetHostnames;
            ok &= ModelUtils::FromJson(fieldValue, refValSetHostnames);
            SetHostnames(refValSetHostnames);
        }
    }
    if (val.contains("ip")) {
        const nlohmann::json &fieldValue = val.at("ip");
        if (!fieldValue.is_null()) {
            std::string refValSetIp;
            ok &= ModelUtils::FromJson(fieldValue, refValSetIp);
            SetIp(refValSetIp);
        }
    }
    return ok;
}

std::vector<std::string> &V1HostAlias::GetHostnames()
{
    return m_hostnames;
}

void V1HostAlias::SetHostnames(const std::vector<std::string> &value)
{
    m_hostnames = value;
    m_hostnamesIsSet = true;
}

bool V1HostAlias::HostnamesIsSet() const
{
    return m_hostnamesIsSet;
}

void V1HostAlias::UnsetHostnames()
{
    m_hostnamesIsSet = false;
}
std::string V1HostAlias::GetIp() const
{
    return m_ip;
}

void V1HostAlias::SetIp(const std::string &value)
{
    m_ip = value;
    m_ipIsSet = true;
}

bool V1HostAlias::IpIsSet() const
{
    return m_ipIsSet;
}

void V1HostAlias::UnsetIp()
{
    m_ipIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
