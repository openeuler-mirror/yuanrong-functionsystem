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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_DOWNWARD_API_VOLUME_FILE_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_DOWNWARD_API_VOLUME_FILE_H_

#include <string>

#include "common/kube_client/model/common/model_base.h"
#include "common/kube_client/model/common/v1_object_field_selector.h"
#include "common/kube_client/model/common/v1_resource_field_selector.h"
#include "common/kube_client/model/common/v1_secret_key_selector.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_DOWNWARD_API_VOLUME_FILE = "V1DownwardAPIVolumeFile";

class V1DownwardAPIVolumeFile : public ModelBase {
public:
    V1DownwardAPIVolumeFile();
    ~V1DownwardAPIVolumeFile() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::shared_ptr<V1ObjectFieldSelector> GetFieldRef() const;
    bool FieldRefIsSet() const;
    void UnsetFieldRef();
    void SetFieldRef(const std::shared_ptr<V1ObjectFieldSelector> &value);

    int32_t GetMode() const;
    bool ModeIsSet() const;
    void UnsetMode();
    void SetMode(int32_t value);

    std::string GetPath() const;
    bool PathIsSet() const;
    void UnsetPath();
    void SetPath(const std::string &value);

    std::shared_ptr<V1ResourceFieldSelector> GetResourceFieldRef() const;
    bool ResourceFieldRefIsSet() const;
    void UnsetResourceFieldRef();
    void SetResourceFieldRef(const std::shared_ptr<V1ResourceFieldSelector> &value);

    std::shared_ptr<V1SecretKeySelector> GetSecretKeyRef() const;
    bool SecretKeyRefIsSet() const;
    void UnsetSecretKeyRef();
    void SetSecretKeyRef(const std::shared_ptr<V1SecretKeySelector> &value);

protected:
    std::shared_ptr<V1ObjectFieldSelector> m_fieldRef;
    bool m_fieldRefIsSet;
    int32_t m_mode;
    bool m_modeIsSet;
    std::string m_path;
    bool m_pathIsSet;
    std::shared_ptr<V1ResourceFieldSelector> m_resourceFieldRef;
    bool m_resourceFieldRefIsSet;
    std::shared_ptr<V1SecretKeySelector> m_secretKeyRef;
    bool m_secretKeyRefIsSet;
};
}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_DOWNWARD_API_VOLUME_FILE_H_ */
