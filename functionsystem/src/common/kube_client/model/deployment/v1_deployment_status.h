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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_DEPLOYMENT_STATUS_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_DEPLOYMENT_STATUS_H_

#include "common/kube_client/model/common/model_base.h"
namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_DEPLOYMENT_STATUS = "V1DeploymentStatus";

class V1DeploymentStatus : public ModelBase {
public:
    V1DeploymentStatus();
    ~V1DeploymentStatus() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    int32_t GetAvailableReplicas() const;
    bool AvailableReplicasIsSet() const;
    void UnsetAvailableReplicas();
    void SetAvailableReplicas(int32_t value);

    int32_t GetReplicas() const;
    bool ReplicasIsSet() const;
    void UnsetReplicas();
    void SetReplicas(int32_t value);

protected:
    int32_t m_availableReplicas;
    bool m_availableReplicasIsSet;
    int32_t m_replicas;
    bool m_replicasIsSet;
};
}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_DEPLOYMENT_STATUS_H_ */
