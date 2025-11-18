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

#include "v1_container_status.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1ContainerStatus);

V1ContainerStatus::V1ContainerStatus()
{
    m_containerID = std::string("");
    m_containerIDIsSet = false;
    m_image = std::string("");
    m_imageIsSet = false;
    m_imageID = std::string("");
    m_imageIDIsSet = false;
    m_lastStateIsSet = false;
    m_name = std::string("");
    m_nameIsSet = false;
    m_ready = false;
    m_readyIsSet = false;
    m_restartCount = 0;
    m_restartCountIsSet = false;
    m_started = false;
    m_startedIsSet = false;
    m_stateIsSet = false;
}

V1ContainerStatus::~V1ContainerStatus()
{
}

nlohmann::json V1ContainerStatus::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_containerIDIsSet) {
        val["containerID"] = ModelUtils::ToJson(m_containerID);
    }
    if (m_imageIsSet) {
        val["image"] = ModelUtils::ToJson(m_image);
    }
    if (m_imageIDIsSet) {
        val["imageID"] = ModelUtils::ToJson(m_imageID);
    }
    if (m_lastStateIsSet) {
        val["lastState"] = ModelUtils::ToJson(m_lastState);
    }
    if (m_nameIsSet) {
        val["name"] = ModelUtils::ToJson(m_name);
    }
    if (m_readyIsSet) {
        val["ready"] = ModelUtils::ToJson(m_ready);
    }
    if (m_restartCountIsSet) {
        val["restartCount"] = ModelUtils::ToJson(m_restartCount);
    }
    if (m_startedIsSet) {
        val["started"] = ModelUtils::ToJson(m_started);
    }
    if (m_stateIsSet) {
        val["state"] = ModelUtils::ToJson(m_state);
    }

    return val;
}

bool V1ContainerStatus::FromJson(const nlohmann::json &val)
{
    bool ok = ParseBaseFromJson(val);

    if (val.contains("lastState")) {
        const nlohmann::json &fieldValue = val.at("lastState");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ContainerState> refValSetLastState;
            ok &= ModelUtils::FromJson(fieldValue, refValSetLastState);
            SetLastState(refValSetLastState);
        }
    }
    if (val.contains("name")) {
        const nlohmann::json &fieldValue = val.at("name");
        if (!fieldValue.is_null()) {
            std::string refValSetName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetName);
            SetName(refValSetName);
        }
    }
    if (val.contains("ready")) {
        const nlohmann::json &fieldValue = val.at("ready");
        if (!fieldValue.is_null()) {
            bool refValSetReady;
            ok &= ModelUtils::FromJson(fieldValue, refValSetReady);
            SetReady(refValSetReady);
        }
    }
    if (val.contains("restartCount")) {
        const nlohmann::json &fieldValue = val.at("restartCount");
        if (!fieldValue.is_null()) {
            int64_t refValSetRestartCount;
            ok &= ModelUtils::FromJson(fieldValue, refValSetRestartCount);
            SetRestartCount(refValSetRestartCount);
        }
    }
    if (val.contains("started")) {
        const nlohmann::json &fieldValue = val.at("started");
        if (!fieldValue.is_null()) {
            bool refValSetStarted;
            ok &= ModelUtils::FromJson(fieldValue, refValSetStarted);
            SetStarted(refValSetStarted);
        }
    }
    if (val.contains("state")) {
        const nlohmann::json &fieldValue = val.at("state");
        if (!fieldValue.is_null()) {
            std::shared_ptr<V1ContainerState> refValSetState;
            ok &= ModelUtils::FromJson(fieldValue, refValSetState);
            SetState(refValSetState);
        }
    }
    return ok;
}

bool V1ContainerStatus::ParseBaseFromJson(const nlohmann::json &val)
{
    bool ok = true;

    if (ModelUtils::CheckHasKey(val, "containerID")) {
        const nlohmann::json &fieldValue = val.at("containerID");
        std::string refValSetContainerID;
        ok &= ModelUtils::FromJson(fieldValue, refValSetContainerID);
        SetContainerID(refValSetContainerID);
    }
    if (ModelUtils::CheckHasKey(val, "image")) {
        const nlohmann::json &fieldValue = val.at("image");
        std::string refValSetImage;
        ok &= ModelUtils::FromJson(fieldValue, refValSetImage);
        SetImage(refValSetImage);
    }
    if (val.contains("imageID")) {
        const nlohmann::json &fieldValue = val.at("imageID");
        if (!fieldValue.is_null()) {
            std::string refValSetImageID;
            ok &= ModelUtils::FromJson(fieldValue, refValSetImageID);
            SetImageID(refValSetImageID);
        }
    }
    return ok;
}

std::string V1ContainerStatus::GetContainerID() const
{
    return m_containerID;
}

void V1ContainerStatus::SetContainerID(const std::string &value)
{
    m_containerID = value;
    m_containerIDIsSet = true;
}

bool V1ContainerStatus::ContainerIDIsSet() const
{
    return m_containerIDIsSet;
}

void V1ContainerStatus::UnsetContainerID()
{
    m_containerIDIsSet = false;
}
std::string V1ContainerStatus::GetImage() const
{
    return m_image;
}

void V1ContainerStatus::SetImage(const std::string &value)
{
    m_image = value;
    m_imageIsSet = true;
}

bool V1ContainerStatus::ImageIsSet() const
{
    return m_imageIsSet;
}

void V1ContainerStatus::UnsetImage()
{
    m_imageIsSet = false;
}
std::string V1ContainerStatus::GetImageID() const
{
    return m_imageID;
}

void V1ContainerStatus::SetImageID(const std::string &value)
{
    m_imageID = value;
    m_imageIDIsSet = true;
}

bool V1ContainerStatus::ImageIDIsSet() const
{
    return m_imageIDIsSet;
}

void V1ContainerStatus::UnsetImageID()
{
    m_imageIDIsSet = false;
}
std::shared_ptr<V1ContainerState> V1ContainerStatus::GetLastState() const
{
    return m_lastState;
}

void V1ContainerStatus::SetLastState(const std::shared_ptr<V1ContainerState> &value)
{
    m_lastState = value;
    m_lastStateIsSet = true;
}

bool V1ContainerStatus::LastStateIsSet() const
{
    return m_lastStateIsSet;
}

void V1ContainerStatus::UnsetLastState()
{
    m_lastStateIsSet = false;
}
std::string V1ContainerStatus::GetName() const
{
    return m_name;
}

void V1ContainerStatus::SetName(const std::string &value)
{
    m_name = value;
    m_nameIsSet = true;
}

bool V1ContainerStatus::NameIsSet() const
{
    return m_nameIsSet;
}

void V1ContainerStatus::UnsetName()
{
    m_nameIsSet = false;
}
bool V1ContainerStatus::IsReady() const
{
    return m_ready;
}

void V1ContainerStatus::SetReady(bool value)
{
    m_ready = value;
    m_readyIsSet = true;
}

bool V1ContainerStatus::ReadyIsSet() const
{
    return m_readyIsSet;
}

void V1ContainerStatus::UnsetReady()
{
    m_readyIsSet = false;
}
int64_t V1ContainerStatus::GetRestartCount() const
{
    return m_restartCount;
}

void V1ContainerStatus::SetRestartCount(int64_t value)
{
    m_restartCount = value;
    m_restartCountIsSet = true;
}

bool V1ContainerStatus::RestartCountIsSet() const
{
    return m_restartCountIsSet;
}

void V1ContainerStatus::UnsetRestartCount()
{
    m_restartCountIsSet = false;
}
bool V1ContainerStatus::IsStarted() const
{
    return m_started;
}

void V1ContainerStatus::SetStarted(bool value)
{
    m_started = value;
    m_startedIsSet = true;
}

bool V1ContainerStatus::StartedIsSet() const
{
    return m_startedIsSet;
}

void V1ContainerStatus::UnsetStarted()
{
    m_startedIsSet = false;
}
std::shared_ptr<V1ContainerState> V1ContainerStatus::GetState() const
{
    return m_state;
}

void V1ContainerStatus::SetState(const std::shared_ptr<V1ContainerState> &value)
{
    m_state = value;
    m_stateIsSet = true;
}

bool V1ContainerStatus::StateIsSet() const
{
    return m_stateIsSet;
}

void V1ContainerStatus::UnsetState()
{
    m_stateIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
