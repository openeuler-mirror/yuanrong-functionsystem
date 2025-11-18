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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_POD_STATUS_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_POD_STATUS_H_

#include <string>
#include <vector>

#include "common/kube_client/api/api_exception.h"
#include "common/kube_client/model/common/model_base.h"
#include "common/kube_client/model/container/v1_container_status.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_POD_STATUS = "V1PodStatus";

class V1PodStatus : public ModelBase {
public:
    V1PodStatus();
    ~V1PodStatus() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;
    bool ParseBaseFromJson(const nlohmann::json &json);

    std::vector<std::shared_ptr<V1ContainerStatus>> &GetContainerStatuses();
    bool ContainerStatusesIsSet() const;
    void UnsetContainerStatuses();
    void SetContainerStatuses(const std::vector<std::shared_ptr<V1ContainerStatus>> &value);

    std::vector<std::shared_ptr<V1ContainerStatus>> &GetEphemeralContainerStatuses();
    bool EphemeralContainerStatusesIsSet() const;
    void UnsetEphemeralContainerStatuses();
    void SetEphemeralContainerStatuses(const std::vector<std::shared_ptr<V1ContainerStatus>> &value);

    std::string GetHostIP() const;
    bool HostIPIsSet() const;
    void UnsetHostIP();
    void SetHostIP(const std::string &value);

    std::vector<std::shared_ptr<V1ContainerStatus>> &GetInitContainerStatuses();
    bool InitContainerStatusesIsSet() const;
    void UnsetInitContainerStatuses();
    void SetInitContainerStatuses(const std::vector<std::shared_ptr<V1ContainerStatus>> &value);

    std::string GetMessage() const;
    bool MessageIsSet() const;
    void UnsetMessage();
    void SetMessage(const std::string &value);

    std::string GetNominatedNodeName() const;
    bool NominatedNodeNameIsSet() const;
    void UnsetNominatedNodeName();
    void SetNominatedNodeName(const std::string &value);

    std::string GetPhase() const;
    bool PhaseIsSet() const;
    void UnsetPhase();
    void SetPhase(const std::string &value);

    std::string GetPodIP() const;
    bool PodIPIsSet() const;
    void UnSetPodIP();
    void SetPodIP(const std::string &value);

    std::string GetQosClass() const;
    bool QosClassIsSet() const;
    void UnsetQosClass();
    void SetQosClass(const std::string &value);

    std::string GetReason() const;
    bool ReasonIsSet() const;
    void UnsetReason();
    void SetReason(const std::string &value);

    utility::Datetime GetStartTime() const;
    bool StartTimeIsSet() const;
    void UnsetStartTime();
    void SetStartTime(const utility::Datetime &value);

protected:
    std::vector<std::shared_ptr<V1ContainerStatus>> m_containerStatuses;
    bool m_containerStatusesIsSet;
    std::vector<std::shared_ptr<V1ContainerStatus>> m_ephemeralContainerStatuses;
    bool m_ephemeralContainerStatusesIsSet;
    std::string m_hostIP;
    bool m_hostIPIsSet;
    std::vector<std::shared_ptr<V1ContainerStatus>> m_initContainerStatuses;
    bool m_initContainerStatusesIsSet;
    std::string m_message;
    bool m_messageIsSet;
    std::string m_nominatedNodeName;
    bool m_nominatedNodeNameIsSet;
    std::string m_phase;
    bool m_phaseIsSet;
    std::string m_podIP;
    bool m_podIPIsSet;
    std::string m_qosClass;
    bool m_qosClassIsSet;
    std::string m_reason;
    bool m_reasonIsSet;
    utility::Datetime m_startTime;
    bool m_startTimeIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_POD_STATUS_H_ */
