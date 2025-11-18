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

#include "v1_status.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1Status);

V1Status::V1Status() noexcept : m_apiVersion(std::string("")),
    m_apiVersionIsSet(false),
    m_code(0),
    m_codeIsSet(false),
    m_detailsIsSet(false),
    m_kind(std::string("")),
    m_kindIsSet(false),
    m_message(std::string("")),
    m_messageIsSet(false),
    m_metadataIsSet(false),
    m_reason(std::string("")),
    m_reasonIsSet(false),
    m_status(std::string("")),
    m_statusIsSet(false)
{}

V1Status::~V1Status()
{
}

nlohmann::json V1Status::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    
    if (m_apiVersionIsSet) {
        val[std::string(("apiVersion"))] = ModelUtils::ToJson(m_apiVersion);
    }
    if (m_codeIsSet) {
        val[std::string(("code"))] = ModelUtils::ToJson(m_code);
    }
    if (m_detailsIsSet) {
        val[std::string(("details"))] = ModelUtils::ToJson(m_details);
    }
    if (m_kindIsSet) {
        val[std::string(("kind"))] = ModelUtils::ToJson(m_kind);
    }
    if (m_messageIsSet) {
        val[std::string(("message"))] = ModelUtils::ToJson(m_message);
    }
    if (m_metadataIsSet) {
        val[std::string(("metadata"))] = ModelUtils::ToJson(m_metadata);
    }
    if (m_reasonIsSet) {
        val[std::string(("reason"))] = ModelUtils::ToJson(m_reason);
    }
    if (m_statusIsSet) {
        val[std::string(("status"))] = ModelUtils::ToJson(m_status);
    }

    return val;
}

bool V1Status::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    
    if (val.contains(std::string(("apiVersion")))) {
        const nlohmann::json& fieldValue = val.at(std::string("apiVersion"));
        if (!fieldValue.is_null()) {
            std::string refValSetApiVersion;
            ok &= ModelUtils::FromJson(fieldValue, refValSetApiVersion);
            SetApiVersion(refValSetApiVersion);
        }
    }
    if (val.contains(std::string(("code")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("code")));
        if (!fieldValue.is_null()) {
            int32_t refValSetCode;
            ok &= ModelUtils::FromJson(fieldValue, refValSetCode);
            SetCode(refValSetCode);
        }
    }
    if (val.contains(std::string(("details")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("details")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1StatusDetails> refValSetDetails;
            ok &= ModelUtils::FromJson(fieldValue, refValSetDetails);
            SetDetails(refValSetDetails);
        }
    }
    if (val.contains(std::string(("message")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("message")));
        if (!fieldValue.is_null()) {
            std::string refValSetMessage;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMessage);
            SetMessage(refValSetMessage);
        }
    }
    if (val.contains(std::string(("kind")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("kind")));
        if (!fieldValue.is_null()) {
            std::string refValSetKind;
            ok &= ModelUtils::FromJson(fieldValue, refValSetKind);
            SetKind(refValSetKind);
        }
    }
    if (val.contains(std::string(("metadata")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("metadata")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ListMeta> refValSetMetadata;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMetadata);
            SetMetadata(refValSetMetadata);
        }
    }
    if (val.contains(std::string(("reason")))) {
        const nlohmann::json& fieldValue = val.at(std::string("reason"));
        if (!fieldValue.is_null()) {
            std::string refValSetReason;
            ok &= ModelUtils::FromJson(fieldValue, refValSetReason);
            SetReason(refValSetReason);
        }
    }
    if (val.contains(std::string(("status")))) {
        const nlohmann::json& fieldValue = val.at(std::string("status"));
        if (!fieldValue.is_null()) {
            std::string refValSetStatus;
            ok &= ModelUtils::FromJson(fieldValue, refValSetStatus);
            SetStatus(refValSetStatus);
        }
    }
    return ok;
}

std::string V1Status::GetApiVersion() const
{
    return m_apiVersion;
}

void V1Status::SetApiVersion(const std::string& value)
{
    m_apiVersion = value;
    m_apiVersionIsSet = true;
}

bool V1Status::ApiVersionIsSet() const
{
    return m_apiVersionIsSet;
}

void V1Status::UnsetApiVersion()
{
    m_apiVersionIsSet = false;
}
int32_t V1Status::GetCode() const
{
    return m_code;
}

void V1Status::SetCode(int32_t value)
{
    m_code = value;
    m_codeIsSet = true;
}

bool V1Status::CodeIsSet() const
{
    return m_codeIsSet;
}

void V1Status::UnsetCode()
{
    m_codeIsSet = false;
}
std::shared_ptr<V1StatusDetails> V1Status::GetDetails() const
{
    return m_details;
}

void V1Status::SetDetails(const std::shared_ptr<V1StatusDetails>& value)
{
    m_details = value;
    m_detailsIsSet = true;
}

bool V1Status::DetailsIsSet() const
{
    return m_detailsIsSet;
}

void V1Status::UnsetDetails()
{
    m_detailsIsSet = false;
}
std::string V1Status::GetKind() const
{
    return m_kind;
}

void V1Status::SetKind(const std::string& value)
{
    m_kind = value;
    m_kindIsSet = true;
}

bool V1Status::KindIsSet() const
{
    return m_kindIsSet;
}

void V1Status::UnsetKind()
{
    m_kindIsSet = false;
}
std::string V1Status::GetMessage() const
{
    return m_message;
}

void V1Status::SetMessage(const std::string& value)
{
    m_message = value;
    m_messageIsSet = true;
}

bool V1Status::MessageIsSet() const
{
    return m_messageIsSet;
}

void V1Status::UnsetMessage()
{
    m_messageIsSet = false;
}
std::shared_ptr<V1ListMeta> V1Status::GetMetadata() const
{
    return m_metadata;
}

void V1Status::SetMetadata(const std::shared_ptr<V1ListMeta>& value)
{
    m_metadata = value;
    m_metadataIsSet = true;
}

bool V1Status::MetadataIsSet() const
{
    return m_metadataIsSet;
}

void V1Status::UnsetMetadata()
{
    m_metadataIsSet = false;
}
std::string V1Status::GetReason() const
{
    return m_reason;
}

void V1Status::SetReason(const std::string& value)
{
    m_reason = value;
    m_reasonIsSet = true;
}

bool V1Status::ReasonIsSet() const
{
    return m_reasonIsSet;
}

void V1Status::UnsetReason()
{
    m_reasonIsSet = false;
}
std::string V1Status::GetStatus() const
{
    return m_status;
}

void V1Status::SetStatus(const std::string& value)
{
    m_status = value;
    m_statusIsSet = true;
}

bool V1Status::StatusIsSet() const
{
    return m_statusIsSet;
}

void V1Status::UnsetStatus()
{
    m_statusIsSet = false;
}
}
}
