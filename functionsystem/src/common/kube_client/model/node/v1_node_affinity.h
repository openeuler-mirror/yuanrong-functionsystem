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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_AFFINITY_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_AFFINITY_H_

#include <vector>

#include "common/kube_client/model/common/model_base.h"
#include "v1_node_selector.h"
#include "v1_preferred_scheduling_term.h"


namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_NODE_AFFINITY = "V1NodeAffinity";

class V1NodeAffinity : public ModelBase {
public:
    V1NodeAffinity();
    ~V1NodeAffinity() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& json) override;

    std::shared_ptr<V1NodeSelector> GetRequiredDuringSchedulingIgnoredDuringExecution() const;
    bool RequiredDuringSchedulingIgnoredDuringExecutionIsSet() const;
    void UnsetRequiredDuringSchedulingIgnoredDuringExecution();
    void SetRequiredDuringSchedulingIgnoredDuringExecution(const std::shared_ptr<V1NodeSelector>& value);

    std::vector<std::shared_ptr<V1PreferredSchedulingTerm>>& GetPreferredDuringSchedulingIgnoredDuringExecution()
    {
        return m_preferredDuringSchedulingIgnoredDuringExecution;
    };
    bool PreferredDuringSchedulingIgnoredDuringExecutionIsSet() const;
    void UnsetPreferredDuringSchedulingIgnoredDuringExecution();
    void SetPreferredDuringSchedulingIgnoredDuringExecution(
            const std::vector<std::shared_ptr<V1PreferredSchedulingTerm>>& value);

protected:
    std::shared_ptr<V1NodeSelector> m_requiredDuringSchedulingIgnoredDuringExecution;
    bool m_requiredDuringSchedulingIgnoredDuringExecutionIsSet;
    std::vector<std::shared_ptr<V1PreferredSchedulingTerm>> m_preferredDuringSchedulingIgnoredDuringExecution;
    bool m_preferredDuringSchedulingIgnoredDuringExecutionIsSet;
};
}
}
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_AFFINITY_H_ */
