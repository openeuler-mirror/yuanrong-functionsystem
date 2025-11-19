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

#include "v1_container_port.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1ContainerPort);

V1ContainerPort::V1ContainerPort()
{
    m_containerPort = 0;
    m_containerPortIsSet = false;
    m_hostIP = std::string("");
    m_hostIPIsSet = false;
    m_hostPort = 0;
    m_hostPortIsSet = false;
    m_name = std::string("");
    m_nameIsSet = false;
    m_protocol = std::string("");
    m_protocolIsSet = false;
}

V1ContainerPort::~V1ContainerPort()
{
}

nlohmann::json V1ContainerPort::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_containerPortIsSet) {
        val["containerPort"] = ModelUtils::ToJson(m_containerPort);
    }
    if (m_hostIPIsSet) {
        val["hostIP"] = ModelUtils::ToJson(m_hostIP);
    }
    if (m_hostPortIsSet) {
        val["hostPort"] = ModelUtils::ToJson(m_hostPort);
    }
    if (m_nameIsSet) {
        val["name"] = ModelUtils::ToJson(m_name);
    }
    if (m_protocolIsSet) {
        val["protocol"] = ModelUtils::ToJson(m_protocol);
    }

    return val;
}

bool V1ContainerPort::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("containerPort")) {
        const nlohmann::json &fieldValue = val.at("containerPort");
        if (!fieldValue.is_null()) {
            int64_t refValSetContainerPort;
            ok &= ModelUtils::FromJson(fieldValue, refValSetContainerPort);
            SetContainerPort(refValSetContainerPort);
        }
    }
    if (val.contains("hostIP")) {
        const nlohmann::json &fieldValue = val.at("hostIP");
        if (!fieldValue.is_null()) {
            std::string refValSetHostIP;
            ok &= ModelUtils::FromJson(fieldValue, refValSetHostIP);
            SetHostIP(refValSetHostIP);
        }
    }
    if (val.contains("hostPort")) {
        const nlohmann::json &fieldValue = val.at("hostPort");
        if (!fieldValue.is_null()) {
            int64_t refValSetHostPort;
            ok &= ModelUtils::FromJson(fieldValue, refValSetHostPort);
            SetHostPort(refValSetHostPort);
        }
    }
    if (val.contains("name")) {
        const nlohmann::json &fieldValue = val.at("name");
        if (!fieldValue.is_null()) {
            std::string refValSetName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetName);
            SetName(refValSetName);
        }
    }
    if (val.contains("protocol")) {
        const nlohmann::json &fieldValue = val.at("protocol");
        if (!fieldValue.is_null()) {
            std::string refValSetProtocol;
            ok &= ModelUtils::FromJson(fieldValue, refValSetProtocol);
            SetProtocol(refValSetProtocol);
        }
    }
    return ok;
}

int64_t V1ContainerPort::GetContainerPort() const
{
    return m_containerPort;
}

void V1ContainerPort::SetContainerPort(int64_t value)
{
    m_containerPort = value;
    m_containerPortIsSet = true;
}

bool V1ContainerPort::ContainerPortIsSet() const
{
    return m_containerPortIsSet;
}

void V1ContainerPort::UnsetContainerPort()
{
    m_containerPortIsSet = false;
}
std::string V1ContainerPort::GetHostIP() const
{
    return m_hostIP;
}

void V1ContainerPort::SetHostIP(const std::string &value)
{
    m_hostIP = value;
    m_hostIPIsSet = true;
}

bool V1ContainerPort::HostIPIsSet() const
{
    return m_hostIPIsSet;
}

void V1ContainerPort::UnsetHostIP()
{
    m_hostIPIsSet = false;
}
int64_t V1ContainerPort::GetHostPort() const
{
    return m_hostPort;
}

void V1ContainerPort::SetHostPort(int64_t value)
{
    m_hostPort = value;
    m_hostPortIsSet = true;
}

bool V1ContainerPort::HostPortIsSet() const
{
    return m_hostPortIsSet;
}

void V1ContainerPort::UnsetHostPort()
{
    m_hostPortIsSet = false;
}
std::string V1ContainerPort::GetName() const
{
    return m_name;
}

void V1ContainerPort::SetName(const std::string &value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V1ContainerPort::NameIsSet() const
{
    return m_nameIsSet;
}

void V1ContainerPort::UnsetName()
{
    m_nameIsSet = false;
}
std::string V1ContainerPort::GetProtocol() const
{
    return m_protocol;
}

void V1ContainerPort::SetProtocol(const std::string &value)
{
    m_protocol = value;
    m_protocolIsSet = true;
}

bool V1ContainerPort::ProtocolIsSet() const
{
    return m_protocolIsSet;
}

void V1ContainerPort::UnsetProtocol()
{
    m_protocolIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
