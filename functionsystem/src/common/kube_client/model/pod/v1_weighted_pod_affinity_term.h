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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_WEIGHTED_POD_AFFINITY_TERM_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_WEIGHTED_POD_AFFINITY_TERM_H_

#include "common/kube_client/model/common/model_base.h"
#include "v1_pod_affinity_term.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_WEIGHTED_POD_AFFINITY_TERM = "V1WeightedPodAffinityTerm";

class V1WeightedPodAffinityTerm : public ModelBase {
public:
    V1WeightedPodAffinityTerm();
    ~V1WeightedPodAffinityTerm() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::shared_ptr<V1PodAffinityTerm> GetPodAffinityTerm() const;
    bool PodAffinityTermIsSet() const;
    void UnSetPodAffinityTerm();
    void SetPodAffinityTerm(const std::shared_ptr<V1PodAffinityTerm> &value);

    int64_t GetWeight() const;
    bool WeightIsSet() const;
    void UnsetWeight();
    void SetWeight(int64_t value);

protected:
    std::shared_ptr<V1PodAffinityTerm> m_podAffinityTerm;
    bool m_podAffinityTermIsSet;
    int64_t m_weight;
    bool m_weightIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_WEIGHTED_POD_AFFINITY_TERM_H_ */
