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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HPASCALINGRULES_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HPASCALINGRULES_H

#include <string>

#include "common/kube_client/model/common/model_base.h"
#include "v2_HPA_scaling_policy.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V2_HPA_SCALING_RULES = "V2HPAScalingRules";

class V2HPAScalingRules : public ModelBase {
public:
    V2HPAScalingRules();
    ~V2HPAScalingRules() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& val) override;

    std::vector<std::shared_ptr<V2HPAScalingPolicy>>& GetPolicies();
    bool PoliciesIsSet() const;
    void UnsetPolicies();
    void SetPolicies(const std::vector<std::shared_ptr<V2HPAScalingPolicy>>& value);

    std::string GetSelectPolicy() const;
    bool SelectPolicyIsSet() const;
    void UnsetSelectPolicy();
    void SetSelectPolicy(const std::string& value);

    int32_t GetStabilizationWindowSeconds() const;
    bool StabilizationWindowSecondsIsSet() const;
    void UnsetStabilizationWindowSeconds();
    void SetStabilizationWindowSeconds(int32_t value);

protected:
    std::vector<std::shared_ptr<V2HPAScalingPolicy>> m_policies;
    bool m_policiesIsSet;
    std::string m_selectPolicy;
    bool m_selectPolicyIsSet;
    int32_t m_stabilizationWindowSeconds;
    bool m_stabilizationWindowSecondsIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HPASCALINGRULES_H */
