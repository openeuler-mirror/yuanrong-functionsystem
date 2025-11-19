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

#include "v1_status_details.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1StatusDetails);

V1StatusDetails::V1StatusDetails() noexcept : m_causesIsSet(false),
    m_group(std::string("")),
    m_groupIsSet(false),
    m_kind(std::string("")),
    m_kindIsSet(false),
    m_name(std::string("")),
    m_nameIsSet(false),
    m_retryAfterSeconds(0),
    m_retryAfterSecondsIsSet(false),
    m_uid(std::string("")),
    m_uidIsSet(false)
{}

V1StatusDetails::~V1StatusDetails()
{
}

nlohmann::json V1StatusDetails::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    
    if (m_causesIsSet) {
        val[std::string(("causes"))] = ModelUtils::ToJson(m_causes);
    }
    if (m_groupIsSet) {
        val[std::string(("group"))] = ModelUtils::ToJson(m_group);
    }
    if (m_kindIsSet) {
        val[std::string(("kind"))] = ModelUtils::ToJson(m_kind);
    }
    if (m_nameIsSet) {
        val[std::string(("name"))] = ModelUtils::ToJson(m_name);
    }
    if (m_retryAfterSecondsIsSet) {
        val[std::string(("retryAfterSeconds"))] = ModelUtils::ToJson(m_retryAfterSeconds);
    }
    if (m_uidIsSet) {
        val[std::string(("uid"))] = ModelUtils::ToJson(m_uid);
    }

    return val;
}

bool V1StatusDetails::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    
    if (val.contains(std::string(("causes")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("causes")));
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1StatusCause>> refValSetCauses;
            ok &= ModelUtils::FromJson(fieldValue, refValSetCauses);
            SetCauses(refValSetCauses);
        }
    }
    if (val.contains(std::string(("group")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("group")));
        if (!fieldValue.is_null()) {
            std::string refValSetGroup;
            ok &= ModelUtils::FromJson(fieldValue, refValSetGroup);
            SetGroup(refValSetGroup);
        }
    }
    if (val.contains(std::string("kind"))) {
        const nlohmann::json& fieldValue = val.at(std::string(("kind")));
        if (!fieldValue.is_null()) {
            std::string refValSetKind;
            ok &= ModelUtils::FromJson(fieldValue, refValSetKind);
            SetKind(refValSetKind);
        }
    }
    if (val.contains(std::string(("retryAfterSeconds")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("retryAfterSeconds")));
        if (!fieldValue.is_null()) {
            int32_t refValSetRetryAfterSeconds;
            ok &= ModelUtils::FromJson(fieldValue, refValSetRetryAfterSeconds);
            SetRetryAfterSeconds(refValSetRetryAfterSeconds);
        }
    }
    if (val.contains(std::string("name"))) {
        const nlohmann::json& fieldValue = val.at(std::string(("name")));
        if (!fieldValue.is_null()) {
            std::string refValSetName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetName);
            SetName(refValSetName);
        }
    }
    if (val.contains(std::string(("uid")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("uid")));
        if (!fieldValue.is_null()) {
            std::string refValSetUid;
            ok &= ModelUtils::FromJson(fieldValue, refValSetUid);
            SetUid(refValSetUid);
        }
    }
    return ok;
}

std::vector<std::shared_ptr<V1StatusCause>>& V1StatusDetails::GetCauses()
{
    return m_causes;
}

void V1StatusDetails::SetCauses(const std::vector<std::shared_ptr<V1StatusCause>>& value)
{
    m_causes = value;
    m_causesIsSet = true;
}

bool V1StatusDetails::CausesIsSet() const
{
    return m_causesIsSet;
}

void V1StatusDetails::UnsetCauses()
{
    m_causesIsSet = false;
}
std::string V1StatusDetails::GetGroup() const
{
    return m_group;
}

void V1StatusDetails::SetGroup(const std::string& value)
{
    m_group = value;
    m_groupIsSet = true;
}

bool V1StatusDetails::GroupIsSet() const
{
    return m_groupIsSet;
}

void V1StatusDetails::UnsetGroup()
{
    m_groupIsSet = false;
}
std::string V1StatusDetails::GetKind() const
{
    return m_kind;
}

void V1StatusDetails::SetKind(const std::string& value)
{
    m_kind = value;
    m_kindIsSet = true;
}

bool V1StatusDetails::KindIsSet() const
{
    return m_kindIsSet;
}

void V1StatusDetails::UnsetKind()
{
    m_kindIsSet = false;
}
std::string V1StatusDetails::GetName() const
{
    return m_name;
}

void V1StatusDetails::SetName(const std::string& value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V1StatusDetails::NameIsSet() const
{
    return m_nameIsSet;
}

void V1StatusDetails::UnsetName()
{
    m_nameIsSet = false;
}
int32_t V1StatusDetails::GetRetryAfterSeconds() const
{
    return m_retryAfterSeconds;
}

void V1StatusDetails::SetRetryAfterSeconds(int32_t value)
{
    m_retryAfterSeconds = value;
    m_retryAfterSecondsIsSet = true;
}

bool V1StatusDetails::RetryAfterSecondsIsSet() const
{
    return m_retryAfterSecondsIsSet;
}

void V1StatusDetails::UnsetRetryAfterSeconds()
{
    m_retryAfterSecondsIsSet = false;
}
std::string V1StatusDetails::GetUid() const
{
    return m_uid;
}

void V1StatusDetails::SetUid(const std::string& value)
{
    m_uid = value;
    m_uidIsSet = true;
}

bool V1StatusDetails::UidIsSet() const
{
    return m_uidIsSet;
}

void V1StatusDetails::UnsetUid()
{
    m_uidIsSet = false;
}
}
}
