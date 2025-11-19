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

#include "v2_horizontal_pod_autoscaler_condition.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2HorizontalPodAutoscalerCondition);

V2HorizontalPodAutoscalerCondition::V2HorizontalPodAutoscalerCondition() : m_lastTransitionTime(utility::Datetime()),
    m_lastTransitionTimeIsSet(false),
    m_message(std::string("")),
    m_messageIsSet(false),
    m_reason(std::string("")),
    m_reasonIsSet(false),
    m_status(std::string("")),
    m_statusIsSet(false),
    m_type(std::string("")),
    m_typeIsSet(false)
{}

V2HorizontalPodAutoscalerCondition::~V2HorizontalPodAutoscalerCondition()
{
}

nlohmann::json V2HorizontalPodAutoscalerCondition::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_lastTransitionTimeIsSet) {
        val[std::string(("lastTransitionTime"))] = ModelUtils::ToJson(m_lastTransitionTime);
    }
    if (m_messageIsSet) {
        val[std::string(("message"))] = ModelUtils::ToJson(m_message);
    }
    if (m_reasonIsSet) {
        val[std::string(("reason"))] = ModelUtils::ToJson(m_reason);
    }
    if (m_statusIsSet) {
        val[std::string(("status"))] = ModelUtils::ToJson(m_status);
    }
    if (m_typeIsSet) {
        val[std::string(("type"))] = ModelUtils::ToJson(m_type);
    }

    return val;
}

bool V2HorizontalPodAutoscalerCondition::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    
    if (val.contains(std::string(("lastTransitionTime")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("lastTransitionTime")));
        if (!fieldValue.is_null()) {
            utility::Datetime refValSetLastTransitionTime;
            ok &= ModelUtils::FromJson(fieldValue, refValSetLastTransitionTime);
            SetLastTransitionTime(refValSetLastTransitionTime);
        }
    }
    if (val.contains(std::string("message"))) {
        const nlohmann::json& fieldValue = val.at(std::string(("message")));
        if (!fieldValue.is_null()) {
            std::string refValSetMessage;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMessage);
            SetMessage(refValSetMessage);
        }
    }
    if (val.contains(std::string("status"))) {
        const nlohmann::json& fieldValue = val.at(std::string(("status")));
        if (!fieldValue.is_null()) {
            std::string refValSetStatus;
            ok &= ModelUtils::FromJson(fieldValue, refValSetStatus);
            SetStatus(refValSetStatus);
        }
    }
    if (val.contains(std::string(("reason")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("reason")));
        if (!fieldValue.is_null()) {
            std::string refValSetReason;
            ok &= ModelUtils::FromJson(fieldValue, refValSetReason);
            SetReason(refValSetReason);
        }
    }
    if (val.contains(std::string("type"))) {
        const nlohmann::json& fieldValue = val.at(std::string(("type")));
        if (!fieldValue.is_null()) {
            std::string refValSetType;
            ok &= ModelUtils::FromJson(fieldValue, refValSetType);
            SetType(refValSetType);
        }
    }
    return ok;
}

utility::Datetime V2HorizontalPodAutoscalerCondition::GetLastTransitionTime() const
{
    return m_lastTransitionTime;
}

void V2HorizontalPodAutoscalerCondition::SetLastTransitionTime(const utility::Datetime& value)
{
    m_lastTransitionTime = value;
    m_lastTransitionTimeIsSet = true;
}

bool V2HorizontalPodAutoscalerCondition::LastTransitionTimeIsSet() const
{
    return m_lastTransitionTimeIsSet;
}

void V2HorizontalPodAutoscalerCondition::UnsetLastTransitionTime()
{
    m_lastTransitionTimeIsSet = false;
}
std::string V2HorizontalPodAutoscalerCondition::GetMessage() const
{
    return m_message;
}

void V2HorizontalPodAutoscalerCondition::SetMessage(const std::string& value)
{
    m_message = value;
    m_messageIsSet = true;
}

bool V2HorizontalPodAutoscalerCondition::MessageIsSet() const
{
    return m_messageIsSet;
}

void V2HorizontalPodAutoscalerCondition::UnsetMessage()
{
    m_messageIsSet = false;
}
std::string V2HorizontalPodAutoscalerCondition::GetReason() const
{
    return m_reason;
}

void V2HorizontalPodAutoscalerCondition::SetReason(const std::string& value)
{
    m_reason = value;
    m_reasonIsSet = true;
}

bool V2HorizontalPodAutoscalerCondition::ReasonIsSet() const
{
    return m_reasonIsSet;
}

void V2HorizontalPodAutoscalerCondition::UnsetReason()
{
    m_reasonIsSet = false;
}
std::string V2HorizontalPodAutoscalerCondition::GetStatus() const
{
    return m_status;
}

void V2HorizontalPodAutoscalerCondition::SetStatus(const std::string& value)
{
    m_status = value;
    m_statusIsSet = true;
}

bool V2HorizontalPodAutoscalerCondition::StatusIsSet() const
{
    return m_statusIsSet;
}

void V2HorizontalPodAutoscalerCondition::UnsetStatus()
{
    m_statusIsSet = false;
}
std::string V2HorizontalPodAutoscalerCondition::GetType() const
{
    return m_type;
}

void V2HorizontalPodAutoscalerCondition::SetType(const std::string& value)
{
    m_type = value;
    m_typeIsSet = true;
}

bool V2HorizontalPodAutoscalerCondition::TypeIsSet() const
{
    return m_typeIsSet;
}

void V2HorizontalPodAutoscalerCondition::UnsetType()
{
    m_typeIsSet = false;
}
}
}