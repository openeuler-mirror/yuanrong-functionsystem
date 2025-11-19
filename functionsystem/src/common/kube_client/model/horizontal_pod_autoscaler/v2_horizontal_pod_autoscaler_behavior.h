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

/*
 * V1Pod.h
 *
 * Pod is a collection of containers that can run on a host. This resource is created by clients and scheduled onto
 * hosts.
 */

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_BEHAVIOR_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_BEHAVIOR_H

#include <string>

#include "common/kube_client/model/common/model_base.h"
#include "v2_HPA_scaling_rules.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V2_HORIZONTAL_POD_AUTOSCALER_BEHAVIOR = "V2HorizontalPodAutoscalerBehavior";

class V2HorizontalPodAutoscalerBehavior : public ModelBase {
public:
    V2HorizontalPodAutoscalerBehavior();
    ~V2HorizontalPodAutoscalerBehavior() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& val) override;

    std::shared_ptr<V2HPAScalingRules> GetScaleDown() const;
    bool ScaleDownIsSet() const;
    void UnsetScaleDown();
    void SetScaleDown(const std::shared_ptr<V2HPAScalingRules>& value);

    std::shared_ptr<V2HPAScalingRules> GetScaleUp() const;
    bool ScaleUpIsSet() const;
    void UnsetScaleUp();
    void SetScaleUp(const std::shared_ptr<V2HPAScalingRules>& value);

protected:
    std::shared_ptr<V2HPAScalingRules> m_scaleDown;
    bool m_scaleDownIsSet;
    std::shared_ptr<V2HPAScalingRules> m_scaleUp;
    bool m_scaleUpIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_BEHAVIOR_H */
