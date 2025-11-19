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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_VOLUME_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_VOLUME_H_

#include <string>

#include "common/kube_client/model/common/model_base.h"
#include "v1_config_map_volume_source.h"
#include "v1_empty_dir_volume_source.h"
#include "v1_host_path_volume_source.h"
#include "v1_secret_volume_source.h"
#include "v1_projected_volume_source.h"
#include "v1_persistent_volume_claim_volume_source.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_VOLUME = "V1Volume";

class V1Volume : public ModelBase {
public:
    V1Volume();
    ~V1Volume() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::shared_ptr<V1ConfigMapVolumeSource> GetConfigMap() const;
    bool ConfigMapIsSet() const;
    void UnsetConfigMap();
    void SetConfigMap(const std::shared_ptr<V1ConfigMapVolumeSource> &value);

    std::shared_ptr<V1EmptyDirVolumeSource> GetEmptyDir() const;
    bool EmptyDirIsSet() const;
    void UnsetEmptyDir();
    void SetEmptyDir(const std::shared_ptr<V1EmptyDirVolumeSource> &value);

    std::shared_ptr<V1HostPathVolumeSource> GetHostPath() const;
    bool HostPathIsSet() const;
    void UnsetHostPath();
    void SetHostPath(const std::shared_ptr<V1HostPathVolumeSource> &value);

    std::string GetName() const;
    bool NameIsSet() const;
    void UnsetName();
    void SetName(const std::string &value);

    std::shared_ptr<V1SecretVolumeSource> GetSecret() const;
    bool SecretIsSet() const;
    void UnSetSecret();
    void SetSecret(const std::shared_ptr<V1SecretVolumeSource> &value);

    std::shared_ptr<V1ProjectedVolumeSource> GetProjected() const;
    bool ProjectedIsSet() const;
    void UnsetProjected();
    void SetProjected(const std::shared_ptr<V1ProjectedVolumeSource>& value);

    std::shared_ptr<V1DownwardAPIVolumeSource> GetDownwardAPI() const;
    bool DownwardAPIIsSet() const;
    void UnsetDownwardAPI();
    void SetDownwardAPI(const std::shared_ptr<V1DownwardAPIVolumeSource>& value);

    std::shared_ptr<V1PersistentVolumeClaimVolumeSource> GetPersistentVolumeClaim() const;
    bool PersistentVolumeClaimIsSet() const;
    void UnsetPersistentVolumeClaim();
    void SetPersistentVolumeClaim(const std::shared_ptr<V1PersistentVolumeClaimVolumeSource>& value);

protected:
    std::shared_ptr<V1ConfigMapVolumeSource> m_configMap;
    bool m_configMapIsSet;
    std::shared_ptr<V1EmptyDirVolumeSource> m_emptyDir;
    bool m_emptyDirIsSet;
    std::shared_ptr<V1HostPathVolumeSource> m_hostPath;
    bool m_hostPathIsSet;
    std::string m_name;
    bool m_nameIsSet;
    std::shared_ptr<V1SecretVolumeSource> m_secret;
    bool m_secretIsSet;
    std::shared_ptr<V1ProjectedVolumeSource> m_projected;
    bool m_projectedIsSet;
    std::shared_ptr<V1DownwardAPIVolumeSource> m_downwardAPI;
    bool m_downwardAPIIsSet;
    std::shared_ptr<V1PersistentVolumeClaimVolumeSource> m_persistentVolumeClaim;
    bool m_persistentVolumeClaimIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_VOLUME_H_ */
