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

#include "v1_container_state_terminated.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1ContainerStateTerminated);

V1ContainerStateTerminated::V1ContainerStateTerminated()
{
    m_containerID = std::string("");
    m_containerIDIsSet = false;
    m_exitCode = 0;
    m_exitCodeIsSet = false;
    m_finishedAt = utility::Datetime();
    m_finishedAtIsSet = false;
    m_startedAt = utility::Datetime();
    m_startedAtIsSet = false;
}

V1ContainerStateTerminated::~V1ContainerStateTerminated()
{
}

nlohmann::json V1ContainerStateTerminated::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_containerIDIsSet) {
        val["containerID"] = ModelUtils::ToJson(m_containerID);
    }
    if (m_exitCodeIsSet) {
        val["exitCode"] = ModelUtils::ToJson(m_exitCode);
    }
    if (m_finishedAtIsSet) {
        val["finishedAt"] = ModelUtils::ToJson(m_finishedAt);
    }
    if (m_startedAtIsSet) {
        val["startedAt"] = ModelUtils::ToJson(m_startedAt);
    }

    return val;
}

bool V1ContainerStateTerminated::FromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (val.contains("containerID")) {
        const nlohmann::json &fieldValue = val.at("containerID");
        if (!fieldValue.is_null()) {
            std::string refValSetContainerID;
            ok &= ModelUtils::FromJson(fieldValue, refValSetContainerID);
            SetContainerID(refValSetContainerID);
        }
    }
    if (val.contains("exitCode")) {
        const nlohmann::json &fieldValue = val.at("exitCode");
        if (!fieldValue.is_null()) {
            int64_t refValSetExitCode;
            ok &= ModelUtils::FromJson(fieldValue, refValSetExitCode);
            SetExitCode(refValSetExitCode);
        }
    }
    if (val.contains("finishedAt")) {
        const nlohmann::json &fieldValue = val.at("finishedAt");
        if (!fieldValue.is_null()) {
            utility::Datetime refValSetFinishedAt;
            ok &= ModelUtils::FromJson(fieldValue, refValSetFinishedAt);
            SetFinishedAt(refValSetFinishedAt);
        }
    }
    if (ModelUtils::CheckHasKey(val, "startedAt")) {
        const nlohmann::json &fieldValue = val.at("startedAt");
        utility::Datetime refValSetStartedAt;
        ok &= ModelUtils::FromJson(fieldValue, refValSetStartedAt);
        SetStartedAt(refValSetStartedAt);
    }
    return ok;
}

std::string V1ContainerStateTerminated::GetContainerID() const
{
    return m_containerID;
}

void V1ContainerStateTerminated::SetContainerID(const std::string &value)
{
    m_containerID = value;
    m_containerIDIsSet = true;
}

bool V1ContainerStateTerminated::ContainerIDIsSet() const
{
    return m_containerIDIsSet;
}

void V1ContainerStateTerminated::UnsetContainerID()
{
    m_containerIDIsSet = false;
}
int64_t V1ContainerStateTerminated::GetExitCode() const
{
    return m_exitCode;
}

void V1ContainerStateTerminated::SetExitCode(int64_t value)
{
    m_exitCode = value;
    m_exitCodeIsSet = true;
}

bool V1ContainerStateTerminated::ExitCodeIsSet() const
{
    return m_exitCodeIsSet;
}

void V1ContainerStateTerminated::UnsetExitCode()
{
    m_exitCodeIsSet = false;
}
utility::Datetime V1ContainerStateTerminated::GetFinishedAt() const
{
    return m_finishedAt;
}

void V1ContainerStateTerminated::SetFinishedAt(const utility::Datetime &value)
{
    m_finishedAt = value;
    m_finishedAtIsSet = true;
}

bool V1ContainerStateTerminated::FinishedAtIsSet() const
{
    return m_finishedAtIsSet;
}

void V1ContainerStateTerminated::UnsetFinishedAt()
{
    m_finishedAtIsSet = false;
}
utility::Datetime V1ContainerStateTerminated::GetStartedAt() const
{
    return m_startedAt;
}

void V1ContainerStateTerminated::SetStartedAt(const utility::Datetime &value)
{
    m_startedAt = value;
    m_startedAtIsSet = true;
}

bool V1ContainerStateTerminated::StartedAtIsSet() const
{
    return m_startedAtIsSet;
}

void V1ContainerStateTerminated::UnsetStartedAt()
{
    m_startedAtIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
