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

/*
 * V1ResourceFieldSelector.h
 *
 * ResourceFieldSelector represents container resources (cpu, memory) and their output format
 */

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_RESOURCE_FIELD_SELECTOR_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_RESOURCE_FIELD_SELECTOR_H_

#include <string>

#include "model_base.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_RESOURCE_FIELD_SELECTOR = "V1ResourceFieldSelector";

class V1ResourceFieldSelector : public ModelBase {
public:
    V1ResourceFieldSelector();
    ~V1ResourceFieldSelector() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::string GetContainerName() const;
    bool ContainerNameIsSet() const;
    void UnsetContainerName();
    void SetContainerName(const std::string &value);

    std::string GetDivisor() const;
    bool DivisorIsSet() const;
    void UnsetDivisor();
    void SetDivisor(const std::string &value);

    std::string GetResource() const;
    bool ResourceIsSet() const;
    void UnsetResource();
    void SetResource(const std::string &value);

protected:
    std::string m_containerName;
    bool m_containerNameIsSet;
    std::string m_divisor;
    bool m_divisorIsSet;
    std::string m_resource;
    bool m_resourceIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_RESOURCE_FIELD_SELECTOR_H_ */
