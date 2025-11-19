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

#include "v1_container_state_running.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1ContainerStateRunning);

V1ContainerStateRunning::V1ContainerStateRunning()
{
    m_startedAt = utility::Datetime();
    m_startedAtIsSet = false;
}

V1ContainerStateRunning::~V1ContainerStateRunning()
{
}

nlohmann::json V1ContainerStateRunning::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_startedAtIsSet) {
        val["startedAt"] = ModelUtils::ToJson(m_startedAt);
    }

    return val;
}

bool V1ContainerStateRunning::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("startedAt")) {
        const nlohmann::json &fieldValue = val.at("startedAt");
        if (!fieldValue.is_null()) {
            utility::Datetime refValSetStartedAt;
            ok &= ModelUtils::FromJson(fieldValue, refValSetStartedAt);
            SetStartedAt(refValSetStartedAt);
        }
    }
    return ok;
}

utility::Datetime V1ContainerStateRunning::GetStartedAt() const
{
    return m_startedAt;
}

void V1ContainerStateRunning::SetStartedAt(const utility::Datetime &value)
{
    m_startedAt = value;
    m_startedAtIsSet = true;
}

bool V1ContainerStateRunning::StartedAtIsSet() const
{
    return m_startedAtIsSet;
}

void V1ContainerStateRunning::UnsetStartedAt()
{
    m_startedAtIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
