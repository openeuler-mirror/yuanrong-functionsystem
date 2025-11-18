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

#include "v1_container_state.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1ContainerState);

V1ContainerState::V1ContainerState()
{
    m_runningIsSet = false;
    m_terminatedIsSet = false;
    m_waitingIsSet = false;
}

V1ContainerState::~V1ContainerState()
{
}

nlohmann::json V1ContainerState::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_runningIsSet) {
        val["running"] = ModelUtils::ToJson(m_running);
    }
    if (m_terminatedIsSet) {
        val["terminated"] = ModelUtils::ToJson(m_terminated);
    }
    if (m_waitingIsSet) {
        val["waiting"] = ModelUtils::ToJson(m_waiting);
    }

    return val;
}

bool V1ContainerState::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("running")) {
        const nlohmann::json &fieldValue = val.at("running");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ContainerStateRunning> refValSetRunning;
            ok &= ModelUtils::FromJson(fieldValue, refValSetRunning);
            SetRunning(refValSetRunning);
        }
    }
    if (val.contains("terminated")) {
        const nlohmann::json &fieldValue = val.at("terminated");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ContainerStateTerminated> refValSetTerminated;
            ok &= ModelUtils::FromJson(fieldValue, refValSetTerminated);
            SetTerminated(refValSetTerminated);
        }
    }
    if (val.contains("waiting")) {
        const nlohmann::json &fieldValue = val.at("waiting");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ContainerStateWaiting> refValSetWaiting;
            ok &= ModelUtils::FromJson(fieldValue, refValSetWaiting);
            SetWaiting(refValSetWaiting);
        }
    }
    return ok;
}

std::shared_ptr<V1ContainerStateRunning> V1ContainerState::GetRunning() const
{
    return m_running;
}

void V1ContainerState::SetRunning(const std::shared_ptr<V1ContainerStateRunning> &value)
{
    m_running = value;
    m_runningIsSet = true;
}

bool V1ContainerState::RunningIsSet() const
{
    return m_runningIsSet;
}

void V1ContainerState::UnsetRunning()
{
    m_runningIsSet = false;
}
std::shared_ptr<V1ContainerStateTerminated> V1ContainerState::GetTerminated() const
{
    return m_terminated;
}

void V1ContainerState::SetTerminated(const std::shared_ptr<V1ContainerStateTerminated> &value)
{
    m_terminated = value;
    m_terminatedIsSet = true;
}

bool V1ContainerState::TerminatedIsSet() const
{
    return m_terminatedIsSet;
}

void V1ContainerState::UnsetTerminated()
{
    m_terminatedIsSet = false;
}
std::shared_ptr<V1ContainerStateWaiting> V1ContainerState::GetWaiting() const
{
    return m_waiting;
}

void V1ContainerState::SetWaiting(const std::shared_ptr<V1ContainerStateWaiting> &value)
{
    m_waiting = value;
    m_waitingIsSet = true;
}

bool V1ContainerState::WaitingIsSet() const
{
    return m_waitingIsSet;
}

void V1ContainerState::UnsetWaiting()
{
    m_waitingIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
