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

#include "v2_horizontal_pod_autoscaler_behavior.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2HorizontalPodAutoscalerBehavior);

V2HorizontalPodAutoscalerBehavior::V2HorizontalPodAutoscalerBehavior() : m_scaleDownIsSet(false),
    m_scaleUpIsSet(false)
{}

V2HorizontalPodAutoscalerBehavior::~V2HorizontalPodAutoscalerBehavior()
{
}

nlohmann::json V2HorizontalPodAutoscalerBehavior::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_scaleDownIsSet) {
        val[std::string(("scaleDown"))] = ModelUtils::ToJson(m_scaleDown);
    }
    if (m_scaleUpIsSet) {
        val[std::string(("scaleUp"))] = ModelUtils::ToJson(m_scaleUp);
    }

    return val;
}

bool V2HorizontalPodAutoscalerBehavior::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    
    if (val.contains(std::string(("scaleDown")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("scaleDown")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2HPAScalingRules> refValSetScaleDown;
            ok &= ModelUtils::FromJson(fieldValue, refValSetScaleDown);
            SetScaleDown(refValSetScaleDown);
        }
    }
    if (val.contains(std::string(("scaleUp")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("scaleUp")));
        if (!fieldValue.is_null()) {
            std::shared_ptr<V2HPAScalingRules> refValSetScaleUp;
            ok &= ModelUtils::FromJson(fieldValue, refValSetScaleUp);
            SetScaleUp(refValSetScaleUp);
        }
    }
    return ok;
}

std::shared_ptr<V2HPAScalingRules> V2HorizontalPodAutoscalerBehavior::GetScaleDown() const
{
    return m_scaleDown;
}

void V2HorizontalPodAutoscalerBehavior::SetScaleDown(const std::shared_ptr<V2HPAScalingRules>& value)
{
    m_scaleDown = value;
    m_scaleDownIsSet = true;
}

bool V2HorizontalPodAutoscalerBehavior::ScaleDownIsSet() const
{
    return m_scaleDownIsSet;
}

void V2HorizontalPodAutoscalerBehavior::UnsetScaleDown()
{
    m_scaleDownIsSet = false;
}
std::shared_ptr<V2HPAScalingRules> V2HorizontalPodAutoscalerBehavior::GetScaleUp() const
{
    return m_scaleUp;
}

void V2HorizontalPodAutoscalerBehavior::SetScaleUp(const std::shared_ptr<V2HPAScalingRules>& value)
{
    m_scaleUp = value;
    m_scaleUpIsSet = true;
}

bool V2HorizontalPodAutoscalerBehavior::ScaleUpIsSet() const
{
    return m_scaleUpIsSet;
}

void V2HorizontalPodAutoscalerBehavior::UnsetScaleUp()
{
    m_scaleUpIsSet = false;
}
}
}