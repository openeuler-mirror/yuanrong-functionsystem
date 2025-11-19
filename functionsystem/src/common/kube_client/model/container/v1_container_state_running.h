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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONTAINER_STATE_RUNNING_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONTAINER_STATE_RUNNING_H_

#include "common/kube_client/api/api_exception.h"
#include "common/kube_client/model/common/model_base.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_CONTAINER_STATE_RUNNING = "V1ContainerStateRunning";

class V1ContainerStateRunning : public ModelBase {
public:
    V1ContainerStateRunning();
    ~V1ContainerStateRunning() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    utility::Datetime GetStartedAt() const;
    bool StartedAtIsSet() const;
    void UnsetStartedAt();
    void SetStartedAt(const utility::Datetime &value);

protected:
    utility::Datetime m_startedAt;
    bool m_startedAtIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_CONTAINER_STATE_RUNNING_H_ */
