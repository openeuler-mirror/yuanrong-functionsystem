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

#include "v1_downward_api_volume_file.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1DownwardAPIVolumeFile);

V1DownwardAPIVolumeFile::V1DownwardAPIVolumeFile()
{
    m_fieldRefIsSet = false;
    m_mode = 0;
    m_modeIsSet = false;
    m_path = std::string("");
    m_pathIsSet = false;
    m_resourceFieldRefIsSet = false;
    m_secretKeyRefIsSet = false;
}

V1DownwardAPIVolumeFile::~V1DownwardAPIVolumeFile()
{
}

nlohmann::json V1DownwardAPIVolumeFile::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_fieldRefIsSet) {
        val["fieldRef"] = ModelUtils::ToJson(m_fieldRef);
    }
    if (m_modeIsSet) {
        val["mode"] = ModelUtils::ToJson(m_mode);
    }
    if (m_pathIsSet) {
        val["path"] = ModelUtils::ToJson(m_path);
    }
    if (m_resourceFieldRefIsSet) {
        val["resourceFieldRef"] = ModelUtils::ToJson(m_resourceFieldRef);
    }
    if (m_secretKeyRefIsSet) {
        val["secretKeyRef"] = ModelUtils::ToJson(m_secretKeyRef);
    }
    return val;
}

bool V1DownwardAPIVolumeFile::FromJson(const nlohmann::json &val)
{
    bool ok = true;
    if (val.contains("fieldRef")) {
        const nlohmann::json &fieldValue = val.at("fieldRef");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ObjectFieldSelector> refValSetFieldRef;
            ok &= ModelUtils::FromJson(fieldValue, refValSetFieldRef);
            SetFieldRef(refValSetFieldRef);
        }
    }
    if (val.contains("mode")) {
        const nlohmann::json &fieldValue = val.at("mode");
        if (!fieldValue.is_null()) {
            int32_t refValSetMode;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMode);
            SetMode(refValSetMode);
        }
    }
    if (val.contains("path")) {
        const nlohmann::json &fieldValue = val.at("path");
        if (!fieldValue.is_null()) {
            std::string refValSetPath;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPath);
            SetPath(refValSetPath);
        }
    }
    if (val.contains("resourceFieldRef")) {
        const nlohmann::json &fieldValue = val.at("resourceFieldRef");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ResourceFieldSelector> refValSetResourceFieldRef;
            ok &= ModelUtils::FromJson(fieldValue, refValSetResourceFieldRef);
            SetResourceFieldRef(refValSetResourceFieldRef);
        }
    }
    if (val.contains("secretKeyRef")) {
        const nlohmann::json &fieldValue = val.at("secretKeyRef");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1SecretKeySelector> refValSetSecretKeyRef;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSecretKeyRef);
            SetSecretKeyRef(refValSetSecretKeyRef);
        }
    }
    return ok;
}

std::shared_ptr<V1ObjectFieldSelector> V1DownwardAPIVolumeFile::GetFieldRef() const
{
    return m_fieldRef;
}

void V1DownwardAPIVolumeFile::SetFieldRef(const std::shared_ptr<V1ObjectFieldSelector> &value)
{
    m_fieldRef = value;
    m_fieldRefIsSet = true;
}

bool V1DownwardAPIVolumeFile::FieldRefIsSet() const
{
    return m_fieldRefIsSet;
}

void V1DownwardAPIVolumeFile::UnsetFieldRef()
{
    m_fieldRefIsSet = false;
}
int32_t V1DownwardAPIVolumeFile::GetMode() const
{
    return m_mode;
}

void V1DownwardAPIVolumeFile::SetMode(int32_t value)
{
    m_mode = value;
    m_modeIsSet = true;
}

bool V1DownwardAPIVolumeFile::ModeIsSet() const
{
    return m_modeIsSet;
}

void V1DownwardAPIVolumeFile::UnsetMode()
{
    m_modeIsSet = false;
}
std::string V1DownwardAPIVolumeFile::GetPath() const
{
    return m_path;
}

void V1DownwardAPIVolumeFile::SetPath(const std::string &value)
{
    m_path = value;
    m_pathIsSet = true;
}

bool V1DownwardAPIVolumeFile::PathIsSet() const
{
    return m_pathIsSet;
}

void V1DownwardAPIVolumeFile::UnsetPath()
{
    m_pathIsSet = false;
}
std::shared_ptr<V1ResourceFieldSelector> V1DownwardAPIVolumeFile::GetResourceFieldRef() const
{
    return m_resourceFieldRef;
}

void V1DownwardAPIVolumeFile::SetResourceFieldRef(const std::shared_ptr<V1ResourceFieldSelector> &value)
{
    m_resourceFieldRef = value;
    m_resourceFieldRefIsSet = true;
}

bool V1DownwardAPIVolumeFile::ResourceFieldRefIsSet() const
{
    return m_resourceFieldRefIsSet;
}

void V1DownwardAPIVolumeFile::UnsetResourceFieldRef()
{
    m_resourceFieldRefIsSet = false;
}

std::shared_ptr<V1SecretKeySelector> V1DownwardAPIVolumeFile::GetSecretKeyRef() const
{
    return m_secretKeyRef;
}

bool V1DownwardAPIVolumeFile::SecretKeyRefIsSet() const
{
    return m_secretKeyRefIsSet;
}

void V1DownwardAPIVolumeFile::UnsetSecretKeyRef()
{
    m_secretKeyRefIsSet = false;
}

void V1DownwardAPIVolumeFile::SetSecretKeyRef(const std::shared_ptr<V1SecretKeySelector> &value)
{
    m_secretKeyRef = value;
    m_secretKeyRefIsSet = true;
}
}  // namespace model
}  // namespace functionsystem::kube_client
