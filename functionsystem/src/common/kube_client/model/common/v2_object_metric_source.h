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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2OBJECT_METRICS_SOURCE_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2OBJECT_METRICS_SOURCE_H

#include <string>

#include "common/kube_client/model/common/model_base.h"
#include "v2_cross_version_object_reference.h"
#include "v2_metric_identifier.h"
#include "v2_metric_target.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V2_OBJECT_METRIC_SOURCE = "V2ObjectMetricSource";
class V2ObjectMetricSource : public ModelBase {
public:
    V2ObjectMetricSource() noexcept;
    ~V2ObjectMetricSource() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& val) override;

    std::shared_ptr<V2CrossVersionObjectReference> GetDescribedObject() const;
    bool DescribedObjectIsSet() const;
    void UnsetDescribedObject();
    void SetDescribedObject(const std::shared_ptr<V2CrossVersionObjectReference>& value);

    std::shared_ptr<V2MetricIdentifier> GetMetric() const;
    bool MetricIsSet() const;
    void UnsetMetric();
    void SetMetric(const std::shared_ptr<V2MetricIdentifier>& value);

    std::shared_ptr<V2MetricTarget> GetTarget() const;
    bool TargetIsSet() const;
    void UnsetTarget();
    void SetTarget(const std::shared_ptr<V2MetricTarget>& value);

protected:
    std::shared_ptr<V2CrossVersionObjectReference> m_describedObject;
    bool m_describedObjectIsSet;
    std::shared_ptr<V2MetricIdentifier> m_metric;
    bool m_metricIsSet;
    std::shared_ptr<V2MetricTarget> m_target;
    bool m_targetIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2OBJECT_METRICS_SOURCE_H */
