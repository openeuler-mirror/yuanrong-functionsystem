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

#include "v1_pod_status.h"

namespace functionsystem::kube_client {
namespace model {

REGISTER_MODEL_OBJECT(V1PodStatus);

V1PodStatus::V1PodStatus()
{
    m_containerStatusesIsSet = false;
    m_ephemeralContainerStatusesIsSet = false;
    m_hostIP = std::string("");
    m_hostIPIsSet = false;
    m_initContainerStatusesIsSet = false;
    m_message = std::string("");
    m_messageIsSet = false;
    m_nominatedNodeName = std::string("");
    m_nominatedNodeNameIsSet = false;
    m_phase = std::string("");
    m_phaseIsSet = false;
    m_podIP = std::string("");
    m_podIPIsSet = false;
    m_qosClass = std::string("");
    m_qosClassIsSet = false;
    m_reason = std::string("");
    m_reasonIsSet = false;
    m_startTime = utility::Datetime();
    m_startTimeIsSet = false;
}

V1PodStatus::~V1PodStatus()
{
}

nlohmann::json V1PodStatus::ToJson() const
{
    nlohmann::json val = nlohmann::json::object();

    if (m_containerStatusesIsSet) {
        val["containerStatuses"] = ModelUtils::ToJson(m_containerStatuses);
    }
    if (m_ephemeralContainerStatusesIsSet) {
        val["ephemeralContainerStatuses"] = ModelUtils::ToJson(m_ephemeralContainerStatuses);
    }
    if (m_hostIPIsSet) {
        val["hostIP"] = ModelUtils::ToJson(m_hostIP);
    }
    if (m_initContainerStatusesIsSet) {
        val["initContainerStatuses"] = ModelUtils::ToJson(m_initContainerStatuses);
    }
    if (m_messageIsSet) {
        val["message"] = ModelUtils::ToJson(m_message);
    }
    if (m_nominatedNodeNameIsSet) {
        val["nominatedNodeName"] = ModelUtils::ToJson(m_nominatedNodeName);
    }
    if (m_phaseIsSet) {
        val["phase"] = ModelUtils::ToJson(m_phase);
    }
    if (m_podIPIsSet) {
        val["podIP"] = ModelUtils::ToJson(m_podIP);
    }
    if (m_qosClassIsSet) {
        val["qosClass"] = ModelUtils::ToJson(m_qosClass);
    }
    if (m_reasonIsSet) {
        val["reason"] = ModelUtils::ToJson(m_reason);
    }
    if (m_startTimeIsSet) {
        val["startTime"] = ModelUtils::ToJson(m_startTime);
    }

    return val;
}

bool V1PodStatus::FromJson(const nlohmann::json &val)
{
    bool ok = ParseBaseFromJson(val);
    if (val.contains("containerStatuses")) {
        const nlohmann::json &fieldValue = val.at("containerStatuses");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1ContainerStatus>> refValSetContainerStatuses;
            ok &= ModelUtils::FromJson(fieldValue, refValSetContainerStatuses);
            SetContainerStatuses(refValSetContainerStatuses);
        }
    }
    if (val.contains("ephemeralContainerStatuses")) {
        const nlohmann::json &fieldValue = val.at("ephemeralContainerStatuses");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1ContainerStatus>> refValSetEphemeralContainerStatuses;
            ok &= ModelUtils::FromJson(fieldValue, refValSetEphemeralContainerStatuses);
            SetEphemeralContainerStatuses(refValSetEphemeralContainerStatuses);
        }
    }
    if (ModelUtils::CheckHasKey(val, "hostIP")) {
        const nlohmann::json &fieldValue = val.at("hostIP");
        std::string refValSetHostIP;
        ok &= ModelUtils::FromJson(fieldValue, refValSetHostIP);
        SetHostIP(refValSetHostIP);
    }
    if (val.contains("initContainerStatuses")) {
        const nlohmann::json &fieldValue = val.at("initContainerStatuses");
        if (!fieldValue.is_null()) {
            std::vector<std::shared_ptr<V1ContainerStatus>> refValSetInitContainerStatuses;
            ok &= ModelUtils::FromJson(fieldValue, refValSetInitContainerStatuses);
            SetInitContainerStatuses(refValSetInitContainerStatuses);
        }
    }
    if (val.contains("message")) {
        const nlohmann::json &fieldValue = val.at("message");
        if (!fieldValue.is_null()) {
            std::string refValSetMessage;
            ok &= ModelUtils::FromJson(fieldValue, refValSetMessage);
            SetMessage(refValSetMessage);
        }
    }
    if (val.contains("nominatedNodeName")) {
        const nlohmann::json &fieldValue = val.at("nominatedNodeName");
        if (!fieldValue.is_null()) {
            std::string refValSetNominatedNodeName;
            ok &= ModelUtils::FromJson(fieldValue, refValSetNominatedNodeName);
            SetNominatedNodeName(refValSetNominatedNodeName);
        }
    }
    return ok;
}

bool V1PodStatus::ParseBaseFromJson(const nlohmann::json &val)
{
    bool ok = true;
    if (val.contains("phase")) {
        const nlohmann::json &fieldValue = val.at("phase");
        if (!fieldValue.is_null()) {
            std::string refValSetPhase;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPhase);
            SetPhase(refValSetPhase);
        }
    }
    if (val.contains("podIP")) {
        const nlohmann::json &fieldValue = val.at("podIP");
        if (!fieldValue.is_null()) {
            std::string refValSetPodIP;
            ok &= ModelUtils::FromJson(fieldValue, refValSetPodIP);
            SetPodIP(refValSetPodIP);
        }
    }
    if (val.contains("qosClass")) {
        const nlohmann::json &fieldValue = val.at("qosClass");
        if (!fieldValue.is_null()) {
            std::string refValSetQosClass;
            ok &= ModelUtils::FromJson(fieldValue, refValSetQosClass);
            SetQosClass(refValSetQosClass);
        }
    }
    if (val.contains("reason")) {
        const nlohmann::json &fieldValue = val.at("reason");
        if (!fieldValue.is_null()) {
            std::string refValSetReason;
            ok &= ModelUtils::FromJson(fieldValue, refValSetReason);
            SetReason(refValSetReason);
        }
    }
    if (val.contains("startTime")) {
        const nlohmann::json &fieldValue = val.at("startTime");
        if (!fieldValue.is_null()) {
            utility::Datetime refValSetStartTime;
            ok &= ModelUtils::FromJson(fieldValue, refValSetStartTime);
            SetStartTime(refValSetStartTime);
        }
    }
    return ok;
}

std::vector<std::shared_ptr<V1ContainerStatus>> &V1PodStatus::GetContainerStatuses()
{
    return m_containerStatuses;
}

void V1PodStatus::SetContainerStatuses(const std::vector<std::shared_ptr<V1ContainerStatus>> &value)
{
    m_containerStatuses = value;
    m_containerStatusesIsSet = true;
}

bool V1PodStatus::ContainerStatusesIsSet() const
{
    return m_containerStatusesIsSet;
}

void V1PodStatus::UnsetContainerStatuses()
{
    m_containerStatusesIsSet = false;
}
std::vector<std::shared_ptr<V1ContainerStatus>> &V1PodStatus::GetEphemeralContainerStatuses()
{
    return m_ephemeralContainerStatuses;
}

void V1PodStatus::SetEphemeralContainerStatuses(const std::vector<std::shared_ptr<V1ContainerStatus>> &value)
{
    m_ephemeralContainerStatuses = value;
    m_ephemeralContainerStatusesIsSet = true;
}

bool V1PodStatus::EphemeralContainerStatusesIsSet() const
{
    return m_ephemeralContainerStatusesIsSet;
}

void V1PodStatus::UnsetEphemeralContainerStatuses()
{
    m_ephemeralContainerStatusesIsSet = false;
}
std::string V1PodStatus::GetHostIP() const
{
    return m_hostIP;
}

void V1PodStatus::SetHostIP(const std::string &value)
{
    m_hostIP = value;
    m_hostIPIsSet = true;
}

bool V1PodStatus::HostIPIsSet() const
{
    return m_hostIPIsSet;
}

void V1PodStatus::UnsetHostIP()
{
    m_hostIPIsSet = false;
}
std::vector<std::shared_ptr<V1ContainerStatus>> &V1PodStatus::GetInitContainerStatuses()
{
    return m_initContainerStatuses;
}

void V1PodStatus::SetInitContainerStatuses(const std::vector<std::shared_ptr<V1ContainerStatus>> &value)
{
    m_initContainerStatuses = value;
    m_initContainerStatusesIsSet = true;
}

bool V1PodStatus::InitContainerStatusesIsSet() const
{
    return m_initContainerStatusesIsSet;
}

void V1PodStatus::UnsetInitContainerStatuses()
{
    m_initContainerStatusesIsSet = false;
}
std::string V1PodStatus::GetMessage() const
{
    return m_message;
}

void V1PodStatus::SetMessage(const std::string &value)
{
    m_message = value;
    m_messageIsSet = true;
}

bool V1PodStatus::MessageIsSet() const
{
    return m_messageIsSet;
}

void V1PodStatus::UnsetMessage()
{
    m_messageIsSet = false;
}
std::string V1PodStatus::GetNominatedNodeName() const
{
    return m_nominatedNodeName;
}

void V1PodStatus::SetNominatedNodeName(const std::string &value)
{
    m_nominatedNodeName = value;
    m_nominatedNodeNameIsSet = true;
}

bool V1PodStatus::NominatedNodeNameIsSet() const
{
    return m_nominatedNodeNameIsSet;
}

void V1PodStatus::UnsetNominatedNodeName()
{
    m_nominatedNodeNameIsSet = false;
}
std::string V1PodStatus::GetPhase() const
{
    return m_phase;
}

void V1PodStatus::SetPhase(const std::string &value)
{
    m_phase = value;
    m_phaseIsSet = true;
}

bool V1PodStatus::PhaseIsSet() const
{
    return m_phaseIsSet;
}

void V1PodStatus::UnsetPhase()
{
    m_phaseIsSet = false;
}

std::string V1PodStatus::GetPodIP() const
{
    return m_podIP;
}

void V1PodStatus::SetPodIP(const std::string &value)
{
    m_podIP = value;
    m_podIPIsSet = true;
}

bool V1PodStatus::PodIPIsSet() const
{
    return m_podIPIsSet;
}

void V1PodStatus::UnSetPodIP()
{
    m_podIPIsSet = false;
}

std::string V1PodStatus::GetQosClass() const
{
    return m_qosClass;
}

void V1PodStatus::SetQosClass(const std::string &value)
{
    m_qosClass = value;
    m_qosClassIsSet = true;
}

bool V1PodStatus::QosClassIsSet() const
{
    return m_qosClassIsSet;
}

void V1PodStatus::UnsetQosClass()
{
    m_qosClassIsSet = false;
}
std::string V1PodStatus::GetReason() const
{
    return m_reason;
}

void V1PodStatus::SetReason(const std::string &value)
{
    m_reason = value;
    m_reasonIsSet = true;
}

bool V1PodStatus::ReasonIsSet() const
{
    return m_reasonIsSet;
}

void V1PodStatus::UnsetReason()
{
    m_reasonIsSet = false;
}
utility::Datetime V1PodStatus::GetStartTime() const
{
    return m_startTime;
}

void V1PodStatus::SetStartTime(const utility::Datetime &value)
{
    m_startTime = value;
    m_startTimeIsSet = true;
}

bool V1PodStatus::StartTimeIsSet() const
{
    return m_startTimeIsSet;
}

void V1PodStatus::UnsetStartTime()
{
    m_startTimeIsSet = false;
}
}  // namespace model
}  // namespace functionsystem::kube_client
