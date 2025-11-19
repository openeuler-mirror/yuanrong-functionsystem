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

#include "v2_horizontal_pod_autoscaler_spec.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2HorizontalPodAutoscalerSpec);

V2HorizontalPodAutoscalerSpec::V2HorizontalPodAutoscalerSpec() : m_behaviorIsSet(false),
    m_maxReplicas(0),
    m_maxReplicasIsSet(false),
    m_metricsIsSet(false),
    m_minReplicas(0),
    m_minReplicasIsSet(false),
    m_scaleTargetRefIsSet(false)
{}

V2HorizontalPodAutoscalerSpec::~V2HorizontalPodAutoscalerSpec()
{
}

nlohmann::json V2HorizontalPodAutoscalerSpec::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_behaviorIsSet) {
        val[std::string(("behavior"))] = ModelUtils::ToJson(m_behavior);
    }
    if (m_maxReplicasIsSet) {
        val[std::string(("maxReplicas"))] = ModelUtils::ToJson(m_maxReplicas);
    }
    if (m_metricsIsSet) {
        val[std::string(("metrics"))] = ModelUtils::ToJson(m_metrics);
    }
    if (m_minReplicasIsSet) {
        val[std::string(("minReplicas"))] = ModelUtils::ToJson(m_minReplicas);
    }
    if (m_scaleTargetRefIsSet) {
        val[std::string(("scaleTargetRef"))] = ModelUtils::ToJson(m_scaleTargetRef);
    }

    return val;
}

bool V2HorizontalPodAutoscalerSpec::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    
    if (val.contains(std::string(("behavior")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("behavior")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2HorizontalPodAutoscalerBehavior> refValSetBehavior;
            ok &= ModelUtils::FromJson(fieldValue, refValSetBehavior);
            SetBehavior(refValSetBehavior);
        }
    }
    if (val.contains(std::string(("maxReplicas")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("maxReplicas")));
        if (!fieldValue.is_null()) {
            int32_t refValSetMaxReplicas;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMaxReplicas);
            SetMaxReplicas(refValSetMaxReplicas);
        }
    }
    if (val.contains(std::string(("metrics")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("metrics")));
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V2MetricSpec>> refValSetMetrics;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMetrics);
            SetMetrics(refValSetMetrics);
        }
    }
    if (val.contains(std::string(("minReplicas")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("minReplicas")));
        if (!fieldValue.is_null()) {
            int32_t refValSetMinReplicas;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMinReplicas);
            SetMinReplicas(refValSetMinReplicas);
        }
    }
    if (val.contains(std::string(("scaleTargetRef")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("scaleTargetRef")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2CrossVersionObjectReference> refValSetScaleTargetRef;
            ok &= ModelUtils::FromJson(fieldValue, refValSetScaleTargetRef);
            SetScaleTargetRef(refValSetScaleTargetRef);
        }
    }
    return ok;
}

std::shared_ptr<V2HorizontalPodAutoscalerBehavior> V2HorizontalPodAutoscalerSpec::GetBehavior() const
{
    return m_behavior;
}

void V2HorizontalPodAutoscalerSpec::SetBehavior(const std::shared_ptr<V2HorizontalPodAutoscalerBehavior>& value)
{
    m_behavior = value;
    m_behaviorIsSet = true;
}

bool V2HorizontalPodAutoscalerSpec::BehaviorIsSet() const
{
    return m_behaviorIsSet;
}

void V2HorizontalPodAutoscalerSpec::UnsetBehavior()
{
    m_behaviorIsSet = false;
}
int32_t V2HorizontalPodAutoscalerSpec::GetMaxReplicas() const
{
    return m_maxReplicas;
}

void V2HorizontalPodAutoscalerSpec::SetMaxReplicas(int32_t value)
{
    m_maxReplicas = value;
    m_maxReplicasIsSet = true;
}

bool V2HorizontalPodAutoscalerSpec::MaxReplicasIsSet() const
{
    return m_maxReplicasIsSet;
}

void V2HorizontalPodAutoscalerSpec::UnsetMaxReplicas()
{
    m_maxReplicasIsSet = false;
}
std::vector<std::shared_ptr<V2MetricSpec>>& V2HorizontalPodAutoscalerSpec::GetMetrics()
{
    return m_metrics;
}

void V2HorizontalPodAutoscalerSpec::SetMetrics(const std::vector<std::shared_ptr<V2MetricSpec>>& value)
{
    m_metrics = value;
    m_metricsIsSet = true;
}

bool V2HorizontalPodAutoscalerSpec::MetricsIsSet() const
{
    return m_metricsIsSet;
}

void V2HorizontalPodAutoscalerSpec::UnsetMetrics()
{
    m_metricsIsSet = false;
}
int32_t V2HorizontalPodAutoscalerSpec::GetMinReplicas() const
{
    return m_minReplicas;
}

void V2HorizontalPodAutoscalerSpec::SetMinReplicas(int32_t value)
{
    m_minReplicas = value;
    m_minReplicasIsSet = true;
}

bool V2HorizontalPodAutoscalerSpec::MinReplicasIsSet() const
{
    return m_minReplicasIsSet;
}

void V2HorizontalPodAutoscalerSpec::UnsetMinReplicas()
{
    m_minReplicasIsSet = false;
}
std::shared_ptr<V2CrossVersionObjectReference> V2HorizontalPodAutoscalerSpec::GetScaleTargetRef() const
{
    return m_scaleTargetRef;
}

void V2HorizontalPodAutoscalerSpec::SetScaleTargetRef(const std::shared_ptr<V2CrossVersionObjectReference>& value)
{
    m_scaleTargetRef = value;
    m_scaleTargetRefIsSet = true;
}

bool V2HorizontalPodAutoscalerSpec::ScaleTargetRefIsSet() const
{
    return m_scaleTargetRefIsSet;
}

void V2HorizontalPodAutoscalerSpec::UnsetScaleTargetRef()
{
    m_scaleTargetRefIsSet = false;
}
}
}