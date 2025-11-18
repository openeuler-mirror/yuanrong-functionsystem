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

#include "v1_volume_mount.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1VolumeMount);

V1VolumeMount::V1VolumeMount()
{
    m_mountPath = std::string("");
    m_mountPathIsSet = false;
    m_mountPropagation = std::string("");
    m_mountPropagationIsSet = false;
    m_name = std::string("");
    m_nameIsSet = false;
    m_readOnly = false;
    m_readOnlyIsSet = false;
    m_subPath = std::string("");
    m_subPathIsSet = false;
    m_subPathExpr = std::string("");
    m_subPathExprIsSet = false;
}

V1VolumeMount::~V1VolumeMount()
{
}

nlohmann::json V1VolumeMount::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_mountPathIsSet) {
        val["mountPath"] = ModelUtils::ToJson(m_mountPath);
    }
    if (m_mountPropagationIsSet) {
        val["mountPropagation"] = ModelUtils::ToJson(m_mountPropagation);
    }
    if (m_nameIsSet) {
        val["name"] = ModelUtils::ToJson(m_name);
    }
    if (m_readOnlyIsSet) {
        val["readOnly"] = ModelUtils::ToJson(m_readOnly);
    }
    if (m_subPathIsSet) {
        val["subPath"] = ModelUtils::ToJson(m_subPath);
    }
    if (m_subPathExprIsSet) {
        val["subPathExpr"] = ModelUtils::ToJson(m_subPathExpr);
    }

    return val;
}

bool V1VolumeMount::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("mountPath")) {
        const nlohmann::json &fieldValue = val.at("mountPath");
        if (!fieldValue.is_null()) {
            std::string refValSetMountPath;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMountPath);
            SetMountPath(refValSetMountPath);
        }
    }
    if (val.contains("mountPropagation")) {
        const nlohmann::json &fieldValue = val.at("mountPropagation");
        if (!fieldValue.is_null()) {
            std::string refValSetMountPropagation;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMountPropagation);
            SetMountPropagation(refValSetMountPropagation);
        }
    }
    if (ModelUtils::CheckHasKey(val, "name")) {
        const nlohmann::json &fieldValue = val.at("name");
        std::string refValSetName;
        ok &= ModelUtils::FromJson(fieldValue, refValSetName);
        SetName(refValSetName);
    }
    if (val.contains("readOnly")) {
        const nlohmann::json &fieldValue = val.at("readOnly");
        if (!fieldValue.is_null()) {
            bool refValSetReadOnly;
            ok &= ModelUtils::FromJson(fieldValue, refValSetReadOnly);
            SetReadOnly(refValSetReadOnly);
        }
    }
    if (val.contains("subPath")) {
        const nlohmann::json &fieldValue = val.at("subPath");
        if (!fieldValue.is_null()) {
            std::string refValSetSubPath;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSubPath);
            SetSubPath(refValSetSubPath);
        }
    }
    if (val.contains("subPathExpr")) {
        const nlohmann::json &fieldValue = val.at("subPathExpr");
        if (!fieldValue.is_null()) {
            std::string refValSetSubPathExpr;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSubPathExpr);
            SetSubPathExpr(refValSetSubPathExpr);
        }
    }
    return ok;
}

std::string V1VolumeMount::GetMountPath() const
{
    return m_mountPath;
}

void V1VolumeMount::SetMountPath(const std::string &value)
{
    m_mountPath = value;
    m_mountPathIsSet = true;
}

bool V1VolumeMount::MountPathIsSet() const
{
    return m_mountPathIsSet;
}

void V1VolumeMount::UnsetMountPath()
{
    m_mountPathIsSet = false;
}
std::string V1VolumeMount::GetMountPropagation() const
{
    return m_mountPropagation;
}

void V1VolumeMount::SetMountPropagation(const std::string &value)
{
    m_mountPropagation = value;
    m_mountPropagationIsSet = true;
}

bool V1VolumeMount::MountPropagationIsSet() const
{
    return m_mountPropagationIsSet;
}

void V1VolumeMount::UnsetMountPropagation()
{
    m_mountPropagationIsSet = false;
}
std::string V1VolumeMount::GetName() const
{
    return m_name;
}

void V1VolumeMount::SetName(const std::string &value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V1VolumeMount::NameIsSet() const
{
    return m_nameIsSet;
}

void V1VolumeMount::UnsetName()
{
    m_nameIsSet = false;
}
bool V1VolumeMount::IsReadOnly() const
{
    return m_readOnly;
}

void V1VolumeMount::SetReadOnly(bool value)
{
    m_readOnly = value;
    m_readOnlyIsSet = true;
}

bool V1VolumeMount::ReadOnlyIsSet() const
{
    return m_readOnlyIsSet;
}

void V1VolumeMount::UnsetReadOnly()
{
    m_readOnlyIsSet = false;
}
std::string V1VolumeMount::GetSubPath() const
{
    return m_subPath;
}

void V1VolumeMount::SetSubPath(const std::string &value)
{
    m_subPath = value;
    m_subPathIsSet = true;
}

bool V1VolumeMount::SubPathIsSet() const
{
    return m_subPathIsSet;
}

void V1VolumeMount::UnsetSubPath()
{
    m_subPathIsSet = false;
}
std::string V1VolumeMount::GetSubPathExpr() const
{
    return m_subPathExpr;
}

void V1VolumeMount::SetSubPathExpr(const std::string &value)
{
    m_subPathExpr = value;
    m_subPathExprIsSet = true;
}

bool V1VolumeMount::SubPathExprIsSet() const
{
    return m_subPathExprIsSet;
}

void V1VolumeMount::UnsetSubPathExpr()
{
    m_subPathExprIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
