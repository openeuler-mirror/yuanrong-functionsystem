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

#include "v1_deployment_status.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1DeploymentStatus);

V1DeploymentStatus::V1DeploymentStatus()
{
    m_availableReplicas = 0;
    m_availableReplicasIsSet = false;
    m_replicas = 0;
    m_replicasIsSet = false;
}

V1DeploymentStatus::~V1DeploymentStatus()
{
}

nlohmann::json V1DeploymentStatus::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();
    if (m_availableReplicasIsSet) {
        val["availableReplicas"] = ModelUtils::ToJson(m_availableReplicas);
    }
    if (m_replicasIsSet) {
        val["replicas"] = ModelUtils::ToJson(m_replicas);
    }
    return val;
}

bool V1DeploymentStatus::FromJson(const nlohmann::json& val)
{
    bool ok = true;
    if (val.contains("availableReplicas")) {
        const nlohmann::json &fieldValue = val.at("availableReplicas");
        if (!fieldValue.is_null()) {
            int32_t refValSetAvailableReplicas;
            ok &= ModelUtils::FromJson(fieldValue, refValSetAvailableReplicas);
            SetAvailableReplicas(refValSetAvailableReplicas);
        }
    }
    if (val.contains("replicas")) {
        const nlohmann::json &fieldValue = val.at("replicas");
        if (!fieldValue.is_null()) {
            int32_t refValSetReplicas;
            ok &= ModelUtils::FromJson(fieldValue, refValSetReplicas);
            SetReplicas(refValSetReplicas);
        }
    }
    return ok;
}

int32_t V1DeploymentStatus::GetAvailableReplicas() const
{
    return m_availableReplicas;
}

void V1DeploymentStatus::SetAvailableReplicas(int32_t value)
{
    m_availableReplicas = value;
    m_availableReplicasIsSet = true;
}

bool V1DeploymentStatus::AvailableReplicasIsSet() const
{
    return m_availableReplicasIsSet;
}

void V1DeploymentStatus::UnsetAvailableReplicas()
{
    m_availableReplicasIsSet = false;
}
int32_t V1DeploymentStatus::GetReplicas() const
{
    return m_replicas;
}

void V1DeploymentStatus::SetReplicas(int32_t value)
{
    m_replicas = value;
    m_replicasIsSet = true;
}

bool V1DeploymentStatus::ReplicasIsSet() const
{
    return m_replicasIsSet;
}

void V1DeploymentStatus::UnsetReplicas()
{
    m_replicasIsSet = false;
}
}
}
