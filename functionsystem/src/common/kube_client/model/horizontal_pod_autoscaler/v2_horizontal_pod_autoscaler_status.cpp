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

#include "v2_horizontal_pod_autoscaler_status.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2HorizontalPodAutoscalerStatus);

V2HorizontalPodAutoscalerStatus::V2HorizontalPodAutoscalerStatus() : m_conditionsIsSet(false),
    m_currentMetricsIsSet(false),
    m_currentReplicas(0),
    m_currentReplicasIsSet(false),
    m_desiredReplicas(0),
    m_desiredReplicasIsSet(false),
    m_lastScaleTime(utility::Datetime()),
    m_lastScaleTimeIsSet(false),
    m_observedGeneration(0L),
    m_observedGenerationIsSet(false)
{}

V2HorizontalPodAutoscalerStatus::~V2HorizontalPodAutoscalerStatus()
{
}

nlohmann::json V2HorizontalPodAutoscalerStatus::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_conditionsIsSet) {
        val[std::string(("conditions"))] = ModelUtils::ToJson(m_conditions);
    }
    if (m_currentMetricsIsSet) {
        val[std::string(("currentMetrics"))] = ModelUtils::ToJson(m_currentMetrics);
    }
    if (m_currentReplicasIsSet) {
        val[std::string(("currentReplicas"))] = ModelUtils::ToJson(m_currentReplicas);
    }
    if (m_desiredReplicasIsSet) {
        val[std::string(("desiredReplicas"))] = ModelUtils::ToJson(m_desiredReplicas);
    }
    if (m_lastScaleTimeIsSet) {
        val[std::string(("lastScaleTime"))] = ModelUtils::ToJson(m_lastScaleTime);
    }
    if (m_observedGenerationIsSet) {
        val[std::string(("observedGeneration"))] = ModelUtils::ToJson(m_observedGeneration);
    }

    return val;
}

bool V2HorizontalPodAutoscalerStatus::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    
    if (val.contains(std::string(("conditions")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("conditions")));
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V2HorizontalPodAutoscalerCondition>> refValSetConditions;
            ok &= ModelUtils::FromJson(fieldValue, refValSetConditions);
            SetConditions(refValSetConditions);
        }
    }
    if (val.contains(std::string(("currentMetrics")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("currentMetrics")));
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V2MetricStatus>> refValSetCurrentMetrics;
            ok &= ModelUtils::FromJson(fieldValue, refValSetCurrentMetrics);
            SetCurrentMetrics(refValSetCurrentMetrics);
        }
    }
    if (val.contains(std::string(("currentReplicas")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("currentReplicas")));
        if (!fieldValue.is_null()) {
            int32_t refValSetCurrentReplicas;
            ok &= ModelUtils::FromJson(fieldValue, refValSetCurrentReplicas);
            SetCurrentReplicas(refValSetCurrentReplicas);
        }
    }
    if (val.contains(std::string(("desiredReplicas")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("desiredReplicas")));
        if (!fieldValue.is_null()) {
            int32_t refValSetDesiredReplicas;
            ok &= ModelUtils::FromJson(fieldValue, refValSetDesiredReplicas);
            SetDesiredReplicas(refValSetDesiredReplicas);
        }
    }
    if (val.contains(std::string(("lastScaleTime")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("lastScaleTime")));
        if (!fieldValue.is_null()) {
            utility::Datetime refValSetLastScaleTime;
            ok &= ModelUtils::FromJson(fieldValue, refValSetLastScaleTime);
            SetLastScaleTime(refValSetLastScaleTime);
        }
    }
    if (val.contains(std::string(("observedGeneration")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("observedGeneration")));
        if (!fieldValue.is_null()) {
            int64_t refValSetObservedGeneration;
            ok &= ModelUtils::FromJson(fieldValue, refValSetObservedGeneration);
            SetObservedGeneration(refValSetObservedGeneration);
        }
    }
    return ok;
}

std::vector<std::shared_ptr<V2HorizontalPodAutoscalerCondition>>& V2HorizontalPodAutoscalerStatus::GetConditions()
{
    return m_conditions;
}

void V2HorizontalPodAutoscalerStatus::SetConditions(
    const std::vector<std::shared_ptr<V2HorizontalPodAutoscalerCondition>>& value)
{
    m_conditions = value;
    m_conditionsIsSet = true;
}

bool V2HorizontalPodAutoscalerStatus::ConditionsIsSet() const
{
    return m_conditionsIsSet;
}

void V2HorizontalPodAutoscalerStatus::UnsetConditions()
{
    m_conditionsIsSet = false;
}
std::vector<std::shared_ptr<V2MetricStatus>>& V2HorizontalPodAutoscalerStatus::GetCurrentMetrics()
{
    return m_currentMetrics;
}

void V2HorizontalPodAutoscalerStatus::SetCurrentMetrics(const std::vector<std::shared_ptr<V2MetricStatus>>& value)
{
    m_currentMetrics = value;
    m_currentMetricsIsSet = true;
}

bool V2HorizontalPodAutoscalerStatus::CurrentMetricsIsSet() const
{
    return m_currentMetricsIsSet;
}

void V2HorizontalPodAutoscalerStatus::UnsetCurrentMetrics()
{
    m_currentMetricsIsSet = false;
}
int32_t V2HorizontalPodAutoscalerStatus::GetCurrentReplicas() const
{
    return m_currentReplicas;
}

void V2HorizontalPodAutoscalerStatus::SetCurrentReplicas(int32_t value)
{
    m_currentReplicas = value;
    m_currentReplicasIsSet = true;
}

bool V2HorizontalPodAutoscalerStatus::CurrentReplicasIsSet() const
{
    return m_currentReplicasIsSet;
}

void V2HorizontalPodAutoscalerStatus::UnsetCurrentReplicas()
{
    m_currentReplicasIsSet = false;
}
int32_t V2HorizontalPodAutoscalerStatus::GetDesiredReplicas() const
{
    return m_desiredReplicas;
}

void V2HorizontalPodAutoscalerStatus::SetDesiredReplicas(int32_t value)
{
    m_desiredReplicas = value;
    m_desiredReplicasIsSet = true;
}

bool V2HorizontalPodAutoscalerStatus::DesiredReplicasIsSet() const
{
    return m_desiredReplicasIsSet;
}

void V2HorizontalPodAutoscalerStatus::UnsetDesiredReplicas()
{
    m_desiredReplicasIsSet = false;
}
utility::Datetime V2HorizontalPodAutoscalerStatus::GetLastScaleTime() const
{
    return m_lastScaleTime;
}

void V2HorizontalPodAutoscalerStatus::SetLastScaleTime(const utility::Datetime& value)
{
    m_lastScaleTime = value;
    m_lastScaleTimeIsSet = true;
}

bool V2HorizontalPodAutoscalerStatus::LastScaleTimeIsSet() const
{
    return m_lastScaleTimeIsSet;
}

void V2HorizontalPodAutoscalerStatus::UnsetLastScaleTime()
{
    m_lastScaleTimeIsSet = false;
}
int64_t V2HorizontalPodAutoscalerStatus::GetObservedGeneration() const
{
    return m_observedGeneration;
}

void V2HorizontalPodAutoscalerStatus::SetObservedGeneration(int64_t value)
{
    m_observedGeneration = value;
    m_observedGenerationIsSet = true;
}

bool V2HorizontalPodAutoscalerStatus::ObservedGenerationIsSet() const
{
    return m_observedGenerationIsSet;
}

void V2HorizontalPodAutoscalerStatus::UnsetObservedGeneration()
{
    m_observedGenerationIsSet = false;
}
}
}