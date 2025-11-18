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

#include "v1_owner_reference.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1OwnerReference);

V1OwnerReference::V1OwnerReference()
{
    m_apiVersion = std::string("");
    m_apiVersionIsSet = false;
    m_blockOwnerDeletion = false;
    m_blockOwnerDeletionIsSet = false;
    m_controller = false;
    m_controllerIsSet = false;
    m_kind = std::string("");
    m_kindIsSet = false;
    m_name = std::string("");
    m_nameIsSet = false;
    m_uid = std::string("");
    m_uidIsSet = false;
}

V1OwnerReference::~V1OwnerReference()
{
}

nlohmann::json V1OwnerReference::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_apiVersionIsSet) {
        val["apiVersion"] = ModelUtils::ToJson(m_apiVersion);
    }
    if (m_blockOwnerDeletionIsSet) {
        val["blockOwnerDeletion"] = ModelUtils::ToJson(m_blockOwnerDeletion);
    }
    if (m_controllerIsSet) {
        val["controller"] = ModelUtils::ToJson(m_controller);
    }
    if (m_kindIsSet) {
        val["kind"] = ModelUtils::ToJson(m_kind);
    }
    if (m_nameIsSet) {
        val["name"] = ModelUtils::ToJson(m_name);
    }
    if (m_uidIsSet) {
        val["uid"] = ModelUtils::ToJson(m_uid);
    }

    return val;
}

bool V1OwnerReference::FromJson(const nlohmann::json &val)
{
    bool ok = true;
    if (val.contains("blockOwnerDeletion")) {
        const nlohmann::json &fieldValue = val.at("blockOwnerDeletion");
        if (!fieldValue.is_null()) {
            bool refValSetBlockOwnerDeletion;
            ok &= ModelUtils::FromJson(fieldValue, refValSetBlockOwnerDeletion);
            SetBlockOwnerDeletion(refValSetBlockOwnerDeletion);
        }
    }
    if (val.contains("apiVersion")) {
        const nlohmann::json &fieldValue = val.at("apiVersion");
        if (!fieldValue.is_null()) {
            std::string refValSetApiVersion;
            ok &= ModelUtils::FromJson(fieldValue, refValSetApiVersion);
            SetApiVersion(refValSetApiVersion);
        }
    }
    if (val.contains("controller")) {
        const nlohmann::json &fieldValue = val.at("controller");
        if (!fieldValue.is_null()) {
            bool refValSetController;
            ok &= ModelUtils::FromJson(fieldValue, refValSetController);
            SetController(refValSetController);
        }
    }
    if (val.contains("kind")) {
        const nlohmann::json &fieldValue = val.at("kind");
        if (!fieldValue.is_null()) {
            std::string refValSetKind;
            ok &= ModelUtils::FromJson(fieldValue, refValSetKind);
            SetKind(refValSetKind);
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
    if (ModelUtils::CheckHasKey(val, "uid")) {
        const nlohmann::json &fieldValue = val.at("uid");
        std::string refValSetUid;
        ok &= ModelUtils::FromJson(fieldValue, refValSetUid);
        SetUid(refValSetUid);
    }
    return ok;
}

std::string V1OwnerReference::GetApiVersion() const
{
    return m_apiVersion;
}

void V1OwnerReference::SetApiVersion(const std::string &value)
{
    m_apiVersion = value;
    m_apiVersionIsSet = true;
}

bool V1OwnerReference::ApiVersionIsSet() const
{
    return m_apiVersionIsSet;
}

void V1OwnerReference::UnsetApiVersion()
{
    m_apiVersionIsSet = false;
}
bool V1OwnerReference::IsBlockOwnerDeletion() const
{
    return m_blockOwnerDeletion;
}

void V1OwnerReference::SetBlockOwnerDeletion(bool value)
{
    m_blockOwnerDeletion = value;
    m_blockOwnerDeletionIsSet = true;
}

bool V1OwnerReference::BlockOwnerDeletionIsSet() const
{
    return m_blockOwnerDeletionIsSet;
}

void V1OwnerReference::UnsetBlockOwnerDeletion()
{
    m_blockOwnerDeletionIsSet = false;
}
bool V1OwnerReference::IsController() const
{
    return m_controller;
}

void V1OwnerReference::SetController(bool value)
{
    m_controller = value;
    m_controllerIsSet = true;
}

bool V1OwnerReference::ControllerIsSet() const
{
    return m_controllerIsSet;
}

void V1OwnerReference::UnsetController()
{
    m_controllerIsSet = false;
}
std::string V1OwnerReference::GetKind() const
{
    return m_kind;
}

void V1OwnerReference::SetKind(const std::string &value)
{
    m_kind = value;
    m_kindIsSet = true;
}

bool V1OwnerReference::KindIsSet() const
{
    return m_kindIsSet;
}

void V1OwnerReference::UnsetKind()
{
    m_kindIsSet = false;
}
std::string V1OwnerReference::GetName() const
{
    return m_name;
}

void V1OwnerReference::SetName(const std::string &value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V1OwnerReference::NameIsSet() const
{
    return m_nameIsSet;
}

void V1OwnerReference::UnsetName()
{
    m_nameIsSet = false;
}
std::string V1OwnerReference::GetUid() const
{
    return m_uid;
}

void V1OwnerReference::SetUid(const std::string &value)
{
    m_uid = value;
    m_uidIsSet = true;
}

bool V1OwnerReference::UidIsSet() const
{
    return m_uidIsSet;
}

void V1OwnerReference::UnsetUid()
{
    m_uidIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
