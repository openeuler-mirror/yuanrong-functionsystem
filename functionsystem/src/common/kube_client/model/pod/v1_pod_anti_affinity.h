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
 * V1PodAntiAffinity.h
 *
 * Pod anti affinity is a group of inter pod anti affinity scheduling rules.
 */

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_POD_ANTI_AFFINITY_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_POD_ANTI_AFFINITY_H_

#include <vector>

#include "common/kube_client/model/common/model_base.h"
#include "v1_pod_affinity_term.h"
#include "v1_weighted_pod_affinity_term.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_POD_ANTI_AFFINITY = "V1PodAntiAffinity";

class V1PodAntiAffinity : public ModelBase {
public:
    V1PodAntiAffinity();
    ~V1PodAntiAffinity() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    bool PreferredDuringSchedulingIgnoredDuringExecutionIsSet() const;
    void UnsetPreferredDuringSchedulingIgnoredDuringExecution();
    void SetPreferredDuringSchedulingIgnoredDuringExecution(
        const std::vector<std::shared_ptr<V1WeightedPodAffinityTerm>> &value);

    void SetRequiredDuringSchedulingIgnoredDuringExecution(
        const std::vector<std::shared_ptr<V1PodAffinityTerm>> &value);
    bool RequiredDuringSchedulingIgnoredDuringExecutionIsSet() const;
    void UnsetRequiredDuringSchedulingIgnoredDuringExecution();

protected:
    std::vector<std::shared_ptr<V1WeightedPodAffinityTerm>> m_preferredDuringSchedulingIgnoredDuringExecution;
    std::vector<std::shared_ptr<V1PodAffinityTerm>> m_requiredDuringSchedulingIgnoredDuringExecution;
    bool m_requiredDuringSchedulingIgnoredDuringExecutionIsSet;
    bool m_preferredDuringSchedulingIgnoredDuringExecutionIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_POD_ANTI_AFFINITY_H_ */
