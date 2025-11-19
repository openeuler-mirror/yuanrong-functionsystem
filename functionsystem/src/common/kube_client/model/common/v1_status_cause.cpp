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

#include "v1_status_cause.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1StatusCause);

V1StatusCause::V1StatusCause() noexcept : m_field(std::string("")),
    m_fieldIsSet(false),
    m_message(std::string("")),
    m_messageIsSet(false),
    m_reason(std::string("")),
    m_reasonIsSet(false)
{}

V1StatusCause::~V1StatusCause()
{
}

nlohmann::json V1StatusCause::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    
    if (m_fieldIsSet) {
        val[std::string(("field"))] = ModelUtils::ToJson(m_field);
    }
    if (m_messageIsSet) {
        val[std::string(("message"))] = ModelUtils::ToJson(m_message);
    }
    if (m_reasonIsSet) {
        val[std::string(("reason"))] = ModelUtils::ToJson(m_reason);
    }

    return val;
}

bool V1StatusCause::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    
    if (val.contains(std::string(("field")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("field")));
        if (!fieldValue.is_null()) {
            std::string refValSetField;
            ok &= ModelUtils::FromJson(fieldValue, refValSetField);
            SetField(refValSetField);
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
    if (val.contains(std::string("reason"))) {
        const nlohmann::json& fieldValue = val.at(std::string(("reason")));
        if (!fieldValue.is_null()) {
            std::string refValSetReason;
            ok &= ModelUtils::FromJson(fieldValue, refValSetReason);
            SetReason(refValSetReason);
        }
    }
    return ok;
}

std::string V1StatusCause::GetField() const
{
    return m_field;
}

void V1StatusCause::SetField(const std::string& value)
{
    m_field = value;
    m_fieldIsSet = true;
}

bool V1StatusCause::FieldIsSet() const
{
    return m_fieldIsSet;
}

void V1StatusCause::UnsetField()
{
    m_fieldIsSet = false;
}
std::string V1StatusCause::GetMessage() const
{
    return m_message;
}

void V1StatusCause::SetMessage(const std::string& value)
{
    m_message = value;
    m_messageIsSet = true;
}

bool V1StatusCause::MessageIsSet() const
{
    return m_messageIsSet;
}

void V1StatusCause::UnsetMessage()
{
    m_messageIsSet = false;
}
std::string V1StatusCause::GetReason() const
{
    return m_reason;
}

void V1StatusCause::SetReason(const std::string& value)
{
    m_reason = value;
    m_reasonIsSet = true;
}

bool V1StatusCause::ReasonIsSet() const
{
    return m_reasonIsSet;
}

void V1StatusCause::UnsetReason()
{
    m_reasonIsSet = false;
}
}
}
