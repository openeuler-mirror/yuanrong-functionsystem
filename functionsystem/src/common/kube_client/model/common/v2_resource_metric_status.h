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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2RESOURCE_METRICS_STATUS_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2RESOURCE_METRICS_STATUS_H

#include <string>

#include "common/kube_client/model/common/model_base.h"
#include "v2_metric_value_status.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V2_RESOURCE_METRIC_STATUS = "V2ResourceMetricStatus";
class V2ResourceMetricStatus : public ModelBase {
public:
    V2ResourceMetricStatus();
    ~V2ResourceMetricStatus() override;;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& val) override;

    std::string GetName() const;
    bool NameIsSet() const;
    void UnsetName();
    void SetName(const std::string& value);

    std::shared_ptr<V2MetricValueStatus> GetCurrent() const;
    bool CurrentIsSet() const;
    void UnsetCurrent();
    void SetCurrent(const std::shared_ptr<V2MetricValueStatus>& value);

protected:
    std::shared_ptr<V2MetricValueStatus> m_current;
    bool m_currentIsSet;
    std::string m_name;
    bool m_nameIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2RESOURCE_METRICS_STATUS_H */
