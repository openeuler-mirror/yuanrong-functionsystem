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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_POD_AFFINITY_TERM_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_POD_AFFINITY_TERM_H_

#include <string>
#include <vector>

#include "common/kube_client/model/common/model_base.h"
#include "common/kube_client/model/common/v1_label_selector.h"

namespace functionsystem::kube_client {
namespace model {
const std::string MODE_NAME_V1_POD_AFFINITY_TERM = "V1PodAffinityTerm";

class V1PodAffinityTerm : public ModelBase {
public:
    V1PodAffinityTerm();
    ~V1PodAffinityTerm() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::shared_ptr<V1LabelSelector> GetLabelSelector() const;
    bool LabelSelectorIsSet() const;
    void UnsetLabelSelector();
    void SetLabelSelector(const std::shared_ptr<V1LabelSelector> &value);

    std::shared_ptr<V1LabelSelector> GetNamespaceSelector() const;
    bool NamespaceSelectorIsSet() const;
    void UnsetNamespaceSelector();
    void SetNamespaceSelector(const std::shared_ptr<V1LabelSelector> &value);

    std::vector<std::string> &GetNamespaces();
    bool NamespacesIsSet() const;
    void UnsetNamespaces();
    void SetNamespaces(const std::vector<std::string> &value);

    std::string GetTopologyKey() const;
    bool TopologyKeyIsSet() const;
    void UnsetTopologyKey();
    void SetTopologyKey(const std::string &value);

protected:
    std::shared_ptr<V1LabelSelector> m_labelSelector;
    bool m_labelSelectorIsSet;
    std::shared_ptr<V1LabelSelector> m_namespaceSelector;
    bool m_namespaceSelectorIsSet;
    std::vector<std::string> m_namespaces;
    bool m_namespacesIsSet;
    std::string m_topologyKey;
    bool m_topologyKeyIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_POD_AFFINITY_TERM_H_ */
