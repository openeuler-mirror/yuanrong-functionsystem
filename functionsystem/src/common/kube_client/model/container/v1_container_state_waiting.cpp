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

#include "v1_container_state_waiting.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1ContainerStateWaiting);

V1ContainerStateWaiting::V1ContainerStateWaiting()
{
    m_message = std::string("");
    m_messageIsSet = false;
    m_reason = std::string("");
    m_reasonIsSet = false;
}

V1ContainerStateWaiting::~V1ContainerStateWaiting()
{
}

nlohmann::json V1ContainerStateWaiting::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_messageIsSet) {
        val["message"] = ModelUtils::ToJson(m_message);
    }
    if (m_reasonIsSet) {
        val["reason"] = ModelUtils::ToJson(m_reason);
    }

    return val;
}

bool V1ContainerStateWaiting::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("message")) {
        const nlohmann::json &fieldValue = val.at("message");
        if (!fieldValue.is_null()) {
            std::string refValSetMessage;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMessage);
            SetMessage(refValSetMessage);
        }
    }
    if (ModelUtils::CheckHasKey(val, "reason")) {
        const nlohmann::json &fieldValue = val.at("reason");
        std::string refValSetReason;
        ok &= ModelUtils::FromJson(fieldValue, refValSetReason);
        SetReason(refValSetReason);
    }
    return ok;
}

std::string V1ContainerStateWaiting::GetMessage() const
{
    return m_message;
}

void V1ContainerStateWaiting::SetMessage(const std::string &value)
{
    m_message = value;
    m_messageIsSet = true;
}

bool V1ContainerStateWaiting::MessageIsSet() const
{
    return m_messageIsSet;
}

void V1ContainerStateWaiting::UnsetMessage()
{
    m_messageIsSet = false;
}
std::string V1ContainerStateWaiting::GetReason() const
{
    return m_reason;
}

void V1ContainerStateWaiting::SetReason(const std::string &value)
{
    m_reason = value;
    m_reasonIsSet = true;
}

bool V1ContainerStateWaiting::ReasonIsSet() const
{
    return m_reasonIsSet;
}

void V1ContainerStateWaiting::UnsetReason()
{
    m_reasonIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
