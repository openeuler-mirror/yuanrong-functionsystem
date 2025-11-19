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

#include "v1_volume.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1Volume);

V1Volume::V1Volume()
{
    m_configMapIsSet = false;
    m_emptyDirIsSet = false;
    m_hostPathIsSet = false;
    m_name = std::string("");
    m_nameIsSet = false;
    m_secretIsSet = false;
    m_projectedIsSet = false;
    m_downwardAPIIsSet = false;
    m_persistentVolumeClaimIsSet = false;
}

V1Volume::~V1Volume()
{
}

nlohmann::json V1Volume::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    if (m_configMapIsSet) {
        val["configMap"] = ModelUtils::ToJson(m_configMap);
    }
    if (m_emptyDirIsSet) {
        val["emptyDir"] = ModelUtils::ToJson(m_emptyDir);
    }
    if (m_hostPathIsSet) {
        val["hostPath"] = ModelUtils::ToJson(m_hostPath);
    }
    if (m_nameIsSet) {
        val["name"] = ModelUtils::ToJson(m_name);
    }
    if (m_secretIsSet) {
        val["secret"] = ModelUtils::ToJson(m_secret);
    }
    if (m_projectedIsSet) {
        val["projected"] = ModelUtils::ToJson(m_projected);
    }
    if (m_downwardAPIIsSet) {
        val["downwardAPI"] = ModelUtils::ToJson(m_downwardAPI);
    }
    if (m_persistentVolumeClaimIsSet) {
        val["persistentVolumeClaim"] = ModelUtils::ToJson(m_persistentVolumeClaim);
    }
    return val;
}

bool V1Volume::FromJson(const nlohmann::json &val)
{
    bool ok = true;
    if (ModelUtils::CheckHasKey(val, "configMap")) {
        std::shared_ptr<V1ConfigMapVolumeSource> refValSetConfigMap;
        ok &= ModelUtils::FromJson(val.at("configMap"), refValSetConfigMap);
        SetConfigMap(refValSetConfigMap);
    }
    if (ModelUtils::CheckHasKey(val, "emptyDir")) {
        std::shared_ptr<V1EmptyDirVolumeSource> refValSetEmptyDir;
        ok &= ModelUtils::FromJson(val.at("emptyDir"), refValSetEmptyDir);
        SetEmptyDir(refValSetEmptyDir);
    }
    if (ModelUtils::CheckHasKey(val, "hostPath")) {
        std::shared_ptr<V1HostPathVolumeSource> refValSetHostPath;
        ok &= ModelUtils::FromJson(val.at("hostPath"), refValSetHostPath);
        SetHostPath(refValSetHostPath);
    }
    if (ModelUtils::CheckHasKey(val, "name")) {
        std::string refValSetName;
        ok &= ModelUtils::FromJson(val.at("name"), refValSetName);
        SetName(refValSetName);
    }
    if (val.contains("secret")) {
        const nlohmann::json &fieldValue = val.at("secret");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1SecretVolumeSource> refValSetSecret;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSecret);
            SetSecret(refValSetSecret);
        }
    }
    if (val.contains("projected")) {
        const nlohmann::json &fieldValue = val.at("projected");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ProjectedVolumeSource> refValSetProjected;
            ok &= ModelUtils::FromJson(fieldValue, refValSetProjected);
            SetProjected(refValSetProjected);
        }
    }
    if (ModelUtils::CheckHasKey(val, "downwardAPI")) {
        std::shared_ptr<V1DownwardAPIVolumeSource> refValSetDownwardAPI;
        ok &= ModelUtils::FromJson(val.at("downwardAPI"), refValSetDownwardAPI);
        SetDownwardAPI(refValSetDownwardAPI);
    }
    if (ModelUtils::CheckHasKey(val, "persistentVolumeClaim")) {
        std::shared_ptr<V1PersistentVolumeClaimVolumeSource> refValSetPersistentVolumeClaim;
        ok &= ModelUtils::FromJson(val.at("persistentVolumeClaim"), refValSetPersistentVolumeClaim);
        SetPersistentVolumeClaim(refValSetPersistentVolumeClaim);
    }
    return ok;
}

std::shared_ptr<V1ConfigMapVolumeSource> V1Volume::GetConfigMap() const
{
    return m_configMap;
}

void V1Volume::SetConfigMap(const std::shared_ptr<V1ConfigMapVolumeSource> &value)
{
    m_configMap = value;
    m_configMapIsSet = true;
}

bool V1Volume::ConfigMapIsSet() const
{
    return m_configMapIsSet;
}

void V1Volume::UnsetConfigMap()
{
    m_configMapIsSet = false;
}
std::shared_ptr<V1EmptyDirVolumeSource> V1Volume::GetEmptyDir() const
{
    return m_emptyDir;
}

void V1Volume::SetEmptyDir(const std::shared_ptr<V1EmptyDirVolumeSource> &value)
{
    m_emptyDir = value;
    m_emptyDirIsSet = true;
}

bool V1Volume::EmptyDirIsSet() const
{
    return m_emptyDirIsSet;
}

void V1Volume::UnsetEmptyDir()
{
    m_emptyDirIsSet = false;
}
std::shared_ptr<V1HostPathVolumeSource> V1Volume::GetHostPath() const
{
    return m_hostPath;
}

void V1Volume::SetHostPath(const std::shared_ptr<V1HostPathVolumeSource> &value)
{
    m_hostPath = value;
    m_hostPathIsSet = true;
}

bool V1Volume::HostPathIsSet() const
{
    return m_hostPathIsSet;
}

void V1Volume::UnsetHostPath()
{
    m_hostPathIsSet = false;
}
std::string V1Volume::GetName() const
{
    return m_name;
}

void V1Volume::SetName(const std::string &value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V1Volume::NameIsSet() const
{
    return m_nameIsSet;
}

void V1Volume::UnsetName()
{
    m_nameIsSet = false;
}
std::shared_ptr<V1SecretVolumeSource> V1Volume::GetSecret() const
{
    return m_secret;
}

void V1Volume::SetSecret(const std::shared_ptr<V1SecretVolumeSource> &value)
{
    m_secret = value;
    m_secretIsSet = true;
}

bool V1Volume::SecretIsSet() const
{
    return m_secretIsSet;
}

void V1Volume::UnSetSecret()
{
    m_secretIsSet = false;
}

std::shared_ptr<V1ProjectedVolumeSource> V1Volume::GetProjected() const
{
    return m_projected;
}

void V1Volume::SetProjected(const std::shared_ptr<V1ProjectedVolumeSource> &value)
{
    m_projected = value;
    m_projectedIsSet = true;
}

bool V1Volume::ProjectedIsSet() const
{
    return m_projectedIsSet;
}

void V1Volume::UnsetProjected()
{
    m_projectedIsSet = false;
}

std::shared_ptr<V1DownwardAPIVolumeSource> V1Volume::GetDownwardAPI() const
{
    return m_downwardAPI;
}

void V1Volume::SetDownwardAPI(const std::shared_ptr<V1DownwardAPIVolumeSource> &value)
{
    m_downwardAPI = value;
    m_downwardAPIIsSet = true;
}

bool V1Volume::DownwardAPIIsSet() const
{
    return m_downwardAPIIsSet;
}

void V1Volume::UnsetDownwardAPI()
{
    m_downwardAPIIsSet = false;
}

std::shared_ptr<V1PersistentVolumeClaimVolumeSource> V1Volume::GetPersistentVolumeClaim() const
{
    return m_persistentVolumeClaim;
}

void V1Volume::SetPersistentVolumeClaim(const std::shared_ptr<V1PersistentVolumeClaimVolumeSource>& value)
{
    m_persistentVolumeClaim = value;
    m_persistentVolumeClaimIsSet = true;
}

bool V1Volume::PersistentVolumeClaimIsSet() const
{
    return m_persistentVolumeClaimIsSet;
}

void V1Volume::UnsetPersistentVolumeClaim()
{
    m_persistentVolumeClaimIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
