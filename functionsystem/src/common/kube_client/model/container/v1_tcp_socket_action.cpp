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

#include "v1_tcp_socket_action.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1TCPSocketAction);

V1TCPSocketAction::V1TCPSocketAction()
{
    m_host = std::string("");
    m_hostIsSet = false;
    m_port = 0;
    m_portIsSet = false;
}

V1TCPSocketAction::~V1TCPSocketAction()
{
}

nlohmann::json V1TCPSocketAction::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_hostIsSet) {
        val["host"] = ModelUtils::ToJson(m_host);
    }
    if (m_portIsSet) {
        val["port"] = ModelUtils::ToJson(m_port);
    }

    return val;
}

bool V1TCPSocketAction::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("host")) {
        const nlohmann::json &fieldValue = val.at("host");
        if (!fieldValue.is_null()) {
            std::string refValSetHost;
            ok &= ModelUtils::FromJson(fieldValue, refValSetHost);
            SetHost(refValSetHost);
        }
    }
    if (val.contains("port")) {
        const nlohmann::json &fieldValue = val.at("port");
        if (!fieldValue.is_null()) {
            int64_t refValSetPort;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPort);
            SetPort(refValSetPort);
        }
    }
    return ok;
}

std::string V1TCPSocketAction::GetHost() const
{
    return m_host;
}

void V1TCPSocketAction::SetHost(const std::string &value)
{
    m_host = value;
    m_hostIsSet = true;
}

bool V1TCPSocketAction::HostIsSet() const
{
    return m_hostIsSet;
}

void V1TCPSocketAction::UnsetHost()
{
    m_hostIsSet = false;
}

int64_t V1TCPSocketAction::GetPort() const
{
    return m_port;
}

void V1TCPSocketAction::SetPort(int64_t value)
{
    m_port = value;
    m_portIsSet = true;
}

bool V1TCPSocketAction::PortIsSet() const
{
    return m_portIsSet;
}

void V1TCPSocketAction::UnsetPort()
{
    m_portIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
