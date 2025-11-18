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

#include "v2_HPA_scaling_rules.h"
#include "common/kube_client/model/common/model_utils.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V2HPAScalingRules);

V2HPAScalingRules::V2HPAScalingRules() : m_policiesIsSet(false),
    m_selectPolicy(std::string("")),
    m_selectPolicyIsSet(false),
    m_stabilizationWindowSeconds(0),
    m_stabilizationWindowSecondsIsSet(false)
{}

V2HPAScalingRules::~V2HPAScalingRules()
{
}

nlohmann::json V2HPAScalingRules::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_policiesIsSet) {
        val[std::string(("policies"))] = ModelUtils::ToJson(m_policies);
    }
    if (m_selectPolicyIsSet) {
        val[std::string(("selectPolicy"))] = ModelUtils::ToJson(m_selectPolicy);
    }
    if (m_stabilizationWindowSecondsIsSet) {
        val[std::string(("stabilizationWindowSeconds"))] = ModelUtils::ToJson(m_stabilizationWindowSeconds);
    }

    return val;
}

bool V2HPAScalingRules::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    
    if (val.contains(std::string(("policies")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("policies")));
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V2HPAScalingPolicy>> refValSetPolicies;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPolicies);
            SetPolicies(refValSetPolicies);
        }
    }
    if (val.contains(std::string(("selectPolicy")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("selectPolicy")));
        if (!fieldValue.is_null()) {
            std::string refValSetSelectPolicy;
            ok &= ModelUtils::FromJson(fieldValue, refValSetSelectPolicy);
            SetSelectPolicy(refValSetSelectPolicy);
        }
    }
    if (val.contains(std::string(("stabilizationWindowSeconds")))) {
        const nlohmann::json& fieldValue = val.at(std::string(("stabilizationWindowSeconds")));
        if (!fieldValue.is_null()) {
            int32_t refValSetStabilizationWindowSeconds;
            ok &= ModelUtils::FromJson(fieldValue, refValSetStabilizationWindowSeconds);
            SetStabilizationWindowSeconds(refValSetStabilizationWindowSeconds);
        }
    }
    return ok;
}

std::vector<std::shared_ptr<V2HPAScalingPolicy>>& V2HPAScalingRules::GetPolicies()
{
    return m_policies;
}

void V2HPAScalingRules::SetPolicies(const std::vector<std::shared_ptr<V2HPAScalingPolicy>>& value)
{
    m_policies = value;
    m_policiesIsSet = true;
}

bool V2HPAScalingRules::PoliciesIsSet() const
{
    return m_policiesIsSet;
}

void V2HPAScalingRules::UnsetPolicies()
{
    m_policiesIsSet = false;
}
std::string V2HPAScalingRules::GetSelectPolicy() const
{
    return m_selectPolicy;
}

void V2HPAScalingRules::SetSelectPolicy(const std::string& value)
{
    m_selectPolicy = value;
    m_selectPolicyIsSet = true;
}

bool V2HPAScalingRules::SelectPolicyIsSet() const
{
    return m_selectPolicyIsSet;
}

void V2HPAScalingRules::UnsetSelectPolicy()
{
    m_selectPolicyIsSet = false;
}
int32_t V2HPAScalingRules::GetStabilizationWindowSeconds() const
{
    return m_stabilizationWindowSeconds;
}

void V2HPAScalingRules::SetStabilizationWindowSeconds(int32_t value)
{
    m_stabilizationWindowSeconds = value;
    m_stabilizationWindowSecondsIsSet = true;
}

bool V2HPAScalingRules::StabilizationWindowSecondsIsSet() const
{
    return m_stabilizationWindowSecondsIsSet;
}

void V2HPAScalingRules::UnsetStabilizationWindowSeconds()
{
    m_stabilizationWindowSecondsIsSet = false;
}
}
}
