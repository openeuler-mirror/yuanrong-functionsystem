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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONTAINER_STATE_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONTAINER_STATE_H_

#include "common/kube_client/model/common/model_base.h"
#include "v1_container_state_running.h"
#include "v1_container_state_terminated.h"
#include "v1_container_state_waiting.h"

namespace functionsystem::kube_client {
namespace model {
const std::string MODE_NAME_V1_CONTAINER_STATE = "V1ContainerState";

class V1ContainerState : public ModelBase {
public:
    V1ContainerState();
    ~V1ContainerState() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::shared_ptr<V1ContainerStateRunning> GetRunning() const;
    bool RunningIsSet() const;
    void UnsetRunning();
    void SetRunning(const std::shared_ptr<V1ContainerStateRunning> &value);

    std::shared_ptr<V1ContainerStateTerminated> GetTerminated() const;
    bool TerminatedIsSet() const;
    void UnsetTerminated();
    void SetTerminated(const std::shared_ptr<V1ContainerStateTerminated> &value);

    std::shared_ptr<V1ContainerStateWaiting> GetWaiting() const;
    bool WaitingIsSet() const;
    void UnsetWaiting();
    void SetWaiting(const std::shared_ptr<V1ContainerStateWaiting> &value);

protected:
    std::shared_ptr<V1ContainerStateRunning> m_running;
    bool m_runningIsSet;
    std::shared_ptr<V1ContainerStateTerminated> m_terminated;
    bool m_terminatedIsSet;
    std::shared_ptr<V1ContainerStateWaiting> m_waiting;
    bool m_waitingIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONTAINER_STATE_H_ */
