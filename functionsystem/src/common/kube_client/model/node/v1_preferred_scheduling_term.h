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

#ifndef YUANRONGKERNEL_V1_PREFERRED_SCHEDULING_TERM_H
#define YUANRONGKERNEL_V1_PREFERRED_SCHEDULING_TERM_H

#include "common/kube_client/model/common/model_base.h"
#include "v1_node_selector_term.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_NODE_PREFERRED_SCHEDULING_TERM = "V1PreferredSchedulingTerm";

class V1PreferredSchedulingTerm : public ModelBase {
public:
    V1PreferredSchedulingTerm();
    ~V1PreferredSchedulingTerm() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& val) override;

    std::shared_ptr<V1NodeSelectorTerm> GetPreference() const;
    bool PreferenceIsSet() const;
    void UnsetPreference();
    void SetPreference(const std::shared_ptr<V1NodeSelectorTerm>& value);

    int32_t GetWeight() const;
    bool WeightIsSet() const;
    void UnsetWeight();
    void SetWeight(int32_t value);

protected:
    std::shared_ptr<V1NodeSelectorTerm> m_preference;
    bool m_preferenceIsSet {false};
    int32_t m_weight {1};
    bool m_weightIsSet {false};
};
}
}
#endif // YUANRONGKERNEL_V1_PREFERRED_SCHEDULING_TERM_H
