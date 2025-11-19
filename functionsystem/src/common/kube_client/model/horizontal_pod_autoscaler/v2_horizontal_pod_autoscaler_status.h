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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_STATUS_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_STATUS_H

#include "common/kube_client/model/common/model_base.h"
#include "common/kube_client/api/api_exception.h"
#include "v2_horizontal_pod_autoscaler_condition.h"
#include "common/kube_client/model/common/v2_metric_status.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V2_HORIZONTAL_POD_AUTOSCALER_STATUS = "V2HorizontalPodAutoscalerStatus";

class V2HorizontalPodAutoscalerStatus : public ModelBase {
public:
    V2HorizontalPodAutoscalerStatus();
    ~V2HorizontalPodAutoscalerStatus() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& val) override;

    std::vector<std::shared_ptr<V2HorizontalPodAutoscalerCondition>>& GetConditions();
    bool ConditionsIsSet() const;
    void UnsetConditions();
    void SetConditions(const std::vector<std::shared_ptr<V2HorizontalPodAutoscalerCondition>>& value);

    std::vector<std::shared_ptr<V2MetricStatus>>& GetCurrentMetrics();
    bool CurrentMetricsIsSet() const;
    void UnsetCurrentMetrics();
    void SetCurrentMetrics(const std::vector<std::shared_ptr<V2MetricStatus>>& value);

    int32_t GetCurrentReplicas() const;
    bool CurrentReplicasIsSet() const;
    void UnsetCurrentReplicas();
    void SetCurrentReplicas(int32_t value);

    int32_t GetDesiredReplicas() const;
    bool DesiredReplicasIsSet() const;
    void UnsetDesiredReplicas();
    void SetDesiredReplicas(int32_t value);

    utility::Datetime GetLastScaleTime() const;
    bool LastScaleTimeIsSet() const;
    void UnsetLastScaleTime();
    void SetLastScaleTime(const utility::Datetime& value);

    int64_t GetObservedGeneration() const;
    bool ObservedGenerationIsSet() const;
    void UnsetObservedGeneration();
    void SetObservedGeneration(int64_t value);

protected:
    std::vector<std::shared_ptr<V2HorizontalPodAutoscalerCondition>> m_conditions;
    bool m_conditionsIsSet;
    std::vector<std::shared_ptr<V2MetricStatus>> m_currentMetrics;
    bool m_currentMetricsIsSet;
    int32_t m_currentReplicas;
    bool m_currentReplicasIsSet;
    int32_t m_desiredReplicas;
    bool m_desiredReplicasIsSet;
    utility::Datetime m_lastScaleTime;
    bool m_lastScaleTimeIsSet;
    int64_t m_observedGeneration;
    bool m_observedGenerationIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_STATUS_H */
