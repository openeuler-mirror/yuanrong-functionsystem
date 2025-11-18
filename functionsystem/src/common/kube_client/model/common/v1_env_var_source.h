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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_ENV_VAR_SOURCE_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_ENV_VAR_SOURCE_H_

#include "model_base.h"
#include "v1_object_field_selector.h"
#include "v1_resource_field_selector.h"
#include "v1_secret_key_selector.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_ENV_VAR_SOURCE = "V1EnvVarSource";

class V1EnvVarSource : public ModelBase {
public:
    V1EnvVarSource();
    ~V1EnvVarSource() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::shared_ptr<V1ObjectFieldSelector> GetFieldRef() const;
    bool FieldRefIsSet() const;
    void UnsetFieldRef();
    void SetFieldRef(const std::shared_ptr<V1ObjectFieldSelector> &value);

    std::shared_ptr<V1ResourceFieldSelector> GetResourceFieldRef() const;
    bool ResourceFieldRefIsSet() const;
    void UnsetResourceFieldRef();
    void SetResourceFieldRef(const std::shared_ptr<V1ResourceFieldSelector> &value);

    std::shared_ptr<V1SecretKeySelector> GetSecretKeyRef() const;
    bool SecretKeyRefIsSet() const;
    void UnsetSecretKeyRef();

    void SetSecretKeyRef(const std::shared_ptr<V1SecretKeySelector>& value);

protected:
    std::shared_ptr<V1ObjectFieldSelector> m_fieldRef;
    bool m_fieldRefIsSet;
    std::shared_ptr<V1ResourceFieldSelector> m_resourceFieldRef;
    bool m_resourceFieldRefIsSet;
    std::shared_ptr<V1SecretKeySelector> m_secretKeyRef;
    bool m_secretKeyRefIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_ENV_VAR_SOURCE_H_ */
