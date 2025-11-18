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

#include "v1_resource_requirements.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1ResourceRequirements);

V1ResourceRequirements::V1ResourceRequirements()
{
    m_limitsIsSet = false;
    m_requestsIsSet = false;
}

V1ResourceRequirements::~V1ResourceRequirements()
{
}

nlohmann::json V1ResourceRequirements::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_limitsIsSet) {
        val["limits"] = ModelUtils::ToJson(m_limits);
    }
    if (m_requestsIsSet) {
        val["requests"] = ModelUtils::ToJson(m_requests);
    }

    return val;
}

bool V1ResourceRequirements::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("limits")) {
        const nlohmann::json &fieldValue = val.at("limits");
        if (!fieldValue.is_null()) {
            std::map<std::string, std::string> refValSetLimits;
            ok &= ModelUtils::FromJson(fieldValue, refValSetLimits);
            SetLimits(refValSetLimits);
        }
    }
    if (val.contains("requests")) {
        const nlohmann::json &fieldValue = val.at("requests");
        if (!fieldValue.is_null()) {
            std::map<std::string, std::string> refValSetRequests;
            ok &= ModelUtils::FromJson(fieldValue, refValSetRequests);
            SetRequests(refValSetRequests);
        }
    }
    return ok;
}

std::map<std::string, std::string> &V1ResourceRequirements::GetLimits()
{
    return m_limits;
}

void V1ResourceRequirements::SetLimits(const std::map<std::string, std::string> &value)
{
    m_limits = value;
    m_limitsIsSet = true;
}

bool V1ResourceRequirements::LimitsIsSet() const
{
    return m_limitsIsSet;
}

void V1ResourceRequirements::UnsetLimits()
{
    m_limitsIsSet = false;
}

std::map<std::string, std::string> &V1ResourceRequirements::GetRequests()
{
    return m_requests;
}

void V1ResourceRequirements::SetRequests(const std::map<std::string, std::string> &value)
{
    m_requests = value;
    m_requestsIsSet = true;
}

bool V1ResourceRequirements::RequestsIsSet() const
{
    return m_requestsIsSet;
}

void V1ResourceRequirements::UnsetRequests()
{
    m_requestsIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
