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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_RESOURCE_REQUIREMENTS_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_RESOURCE_REQUIREMENTS_H_

#include <map>
#include <string>

#include "model_base.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_RESOURCE_REQUIREMENTS = "V1ResourceRequirements";

class V1ResourceRequirements : public ModelBase {
public:
    V1ResourceRequirements();
    ~V1ResourceRequirements() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::map<std::string, std::string> &GetLimits();
    bool LimitsIsSet() const;
    void UnsetLimits();
    void SetLimits(const std::map<std::string, std::string> &value);

    std::map<std::string, std::string> &GetRequests();
    bool RequestsIsSet() const;
    void UnsetRequests();
    void SetRequests(const std::map<std::string, std::string> &value);

protected:
    std::map<std::string, std::string> m_limits;
    bool m_limitsIsSet;
    std::map<std::string, std::string> m_requests;
    bool m_requestsIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_RESOURCE_REQUIREMENTS_H_ */
