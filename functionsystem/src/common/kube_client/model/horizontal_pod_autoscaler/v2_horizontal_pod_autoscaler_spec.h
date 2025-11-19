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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_SPEC_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_SPEC_H

#include "common/kube_client/model/common/model_base.h"
#include "v2_horizontal_pod_autoscaler_behavior.h"
#include "common/kube_client/model/common/v2_metric_spec.h"
#include "common/kube_client/model/common/v2_cross_version_object_reference.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V2_HORIZONTAL_POD_AUTOSCALER_SPEC = "V2HorizontalPodAutoscalerSpec";

class V2HorizontalPodAutoscalerSpec : public ModelBase {
public:
    V2HorizontalPodAutoscalerSpec();
    ~V2HorizontalPodAutoscalerSpec() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& val) override;

    std::shared_ptr<V2HorizontalPodAutoscalerBehavior> GetBehavior() const;
    bool BehaviorIsSet() const;
    void UnsetBehavior();
    void SetBehavior(const std::shared_ptr<V2HorizontalPodAutoscalerBehavior>& value);

    int32_t GetMaxReplicas() const;
    bool MaxReplicasIsSet() const;
    void UnsetMaxReplicas();
    void SetMaxReplicas(int32_t value);

    int32_t GetMinReplicas() const;
    bool MinReplicasIsSet() const;
    void UnsetMinReplicas();
    void SetMinReplicas(int32_t value);

    std::vector<std::shared_ptr<V2MetricSpec>>& GetMetrics();
    bool MetricsIsSet() const;
    void UnsetMetrics();
    void SetMetrics(const std::vector<std::shared_ptr<V2MetricSpec>>& value);

    std::shared_ptr<V2CrossVersionObjectReference> GetScaleTargetRef() const;
    bool ScaleTargetRefIsSet() const;
    void UnsetScaleTargetRef();
    void SetScaleTargetRef(const std::shared_ptr<V2CrossVersionObjectReference>& value);

protected:
    std::shared_ptr<V2HorizontalPodAutoscalerBehavior> m_behavior;
    bool m_behaviorIsSet;
    int32_t m_maxReplicas;
    bool m_maxReplicasIsSet;
    std::vector<std::shared_ptr<V2MetricSpec>> m_metrics;
    bool m_metricsIsSet;
    int32_t m_minReplicas;
    bool m_minReplicasIsSet;
    std::shared_ptr<V2CrossVersionObjectReference> m_scaleTargetRef;
    bool m_scaleTargetRefIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_SPEC_H */
