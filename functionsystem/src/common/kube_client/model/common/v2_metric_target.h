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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2METRICS_TARGET_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2METRICS_TARGET_H

#include <string>

#include "common/kube_client/model/common/model_base.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V2_METRIC_TARGET = "V2MetricTarget";
class V2MetricTarget : public ModelBase {
public:
    V2MetricTarget();
    ~V2MetricTarget() override;

    bool FromJson(const nlohmann::json& val) override;
    nlohmann::json ToJson() const override;

    int32_t GetAverageUtilization() const;
    bool AverageUtilizationIsSet() const;
    void UnsetAverageUtilization();
    void SetAverageUtilization(int32_t value);

    std::string GetAverageValue() const;
    bool AverageValueIsSet() const;
    void UnsetAverageValue();
    void SetAverageValue(const std::string& value);

    std::string GetType() const;
    bool TypeIsSet() const;
    void UnsetType();
    void SetType(const std::string& value);

    std::string GetValue() const;
    bool ValueIsSet() const;
    void UnsetValue();
    void SetValue(const std::string& value);

protected:
    int32_t m_averageUtilization;
    bool m_averageUtilizationIsSet;
    std::string m_averageValue;
    bool m_averageValueIsSet;
    std::string m_type;
    bool m_typeIsSet;
    std::string m_value;
    bool m_valueIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2METRICS_TARGET_H */
