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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONTAINER_STATUS_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONTAINER_STATUS_H_

#include <string>

#include "common/kube_client/model/common/model_base.h"
#include "v1_container_state.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_CONTAINER_STATUS = "V1ContainerStatus";

class V1ContainerStatus : public ModelBase {
public:
    V1ContainerStatus();
    ~V1ContainerStatus() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    bool ParseBaseFromJson(const nlohmann::json &json);

    std::string GetContainerID() const;
    bool ContainerIDIsSet() const;
    void UnsetContainerID();
    void SetContainerID(const std::string &value);

    std::string GetImage() const;
    bool ImageIsSet() const;
    void UnsetImage();
    void SetImage(const std::string &value);

    std::string GetImageID() const;
    bool ImageIDIsSet() const;
    void UnsetImageID();
    void SetImageID(const std::string &value);

    std::shared_ptr<V1ContainerState> GetLastState() const;
    bool LastStateIsSet() const;
    void UnsetLastState();
    void SetLastState(const std::shared_ptr<V1ContainerState> &value);

    std::string GetName() const;
    bool NameIsSet() const;
    void UnsetName();
    void SetName(const std::string &value);

    bool IsReady() const;
    bool ReadyIsSet() const;
    void UnsetReady();
    void SetReady(bool value);

    int64_t GetRestartCount() const;
    bool RestartCountIsSet() const;
    void UnsetRestartCount();
    void SetRestartCount(int64_t value);

    bool IsStarted() const;
    bool StartedIsSet() const;
    void UnsetStarted();
    void SetStarted(bool value);

    std::shared_ptr<V1ContainerState> GetState() const;
    bool StateIsSet() const;
    void UnsetState();
    void SetState(const std::shared_ptr<V1ContainerState> &value);

protected:
    std::string m_containerID;
    bool m_containerIDIsSet;
    std::string m_image;
    bool m_imageIsSet;
    std::string m_imageID;
    bool m_imageIDIsSet;
    std::shared_ptr<V1ContainerState> m_lastState;
    bool m_lastStateIsSet;
    std::string m_name;
    bool m_nameIsSet;
    bool m_ready;
    bool m_readyIsSet;
    int64_t m_restartCount;
    bool m_restartCountIsSet;
    bool m_started;
    bool m_startedIsSet;
    std::shared_ptr<V1ContainerState> m_state;
    bool m_stateIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONTAINER_STATUS_H_ */
