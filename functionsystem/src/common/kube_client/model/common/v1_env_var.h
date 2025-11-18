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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_ENV_VAR_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_ENV_VAR_H_

#include <string>

#include "model_base.h"
#include "v1_env_var_source.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_ENV_VAR = "V1EnvVar";

class V1EnvVar : public ModelBase {
public:
    V1EnvVar();
    ~V1EnvVar() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::string GetName() const;
    bool NameIsSet() const;
    void UnsetName();
    void SetName(const std::string &value);

    std::string GetValue() const;
    bool ValueIsSet() const;
    void UnsetValue();
    void SetValue(const std::string &value);

    std::shared_ptr<V1EnvVarSource> GetValueFrom() const;
    bool ValueFromIsSet() const;
    void UnsetValueFrom();
    void SetValueFrom(const std::shared_ptr<V1EnvVarSource> &value);

protected:
    std::string m_name;
    bool m_nameIsSet;
    std::string m_value;
    bool m_valueIsSet;
    std::shared_ptr<V1EnvVarSource> m_valueFrom;
    bool m_valueFromIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1EnvVar_H_ */
