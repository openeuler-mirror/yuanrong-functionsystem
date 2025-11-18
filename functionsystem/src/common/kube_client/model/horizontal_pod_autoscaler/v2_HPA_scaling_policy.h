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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HPASCALINGPOLICY_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HPASCALINGPOLICY_H

#include <string>

#include "common/kube_client/model/common/model_base.h"
#include "v2_HPA_scaling_policy.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V2_HPA_SCALING_POLICY = "V2HPAScalingPolicy";

class V2HPAScalingPolicy : public ModelBase {
public:
    V2HPAScalingPolicy();
    ~V2HPAScalingPolicy() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& val) override;

    int32_t GetPeriodSeconds() const;
    bool PeriodSecondsIsSet() const;
    void UnsetPeriodSeconds();
    void SetPeriodSeconds(int32_t value);

    std::string GetType() const;
    bool TypeIsSet() const;
    void UnsetType();
    void SetType(const std::string& value);

    int32_t GetValue() const;
    bool ValueIsSet() const;
    void UnsetValue();
    void SetValue(int32_t value);

protected:
    int32_t m_periodSeconds;
    bool m_periodSecondsIsSet;
    std::string m_type;
    bool m_typeIsSet;
    int32_t m_value;
    bool m_valueIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HPASCALINGPOLICY_H */
