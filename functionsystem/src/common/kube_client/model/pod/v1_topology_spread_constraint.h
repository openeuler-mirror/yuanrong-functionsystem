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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_TOPOLOGY_SPREAD_CONSTRAINT_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_TOPOLOGY_SPREAD_CONSTRAINT_H_

#include "common/kube_client/model/common/model_base.h"
#include "common/kube_client/model/common/v1_label_selector.h"

namespace functionsystem::kube_client {
namespace model {

class V1TopologySpreadConstraint : public ModelBase {
public:
    V1TopologySpreadConstraint();
    ~V1TopologySpreadConstraint() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::shared_ptr<V1LabelSelector> GetLabelSelector() const;
    bool LabelSelectorIsSet() const;
    void UnsetLabelSelector();
    void SetLabelSelector(const std::shared_ptr<V1LabelSelector> &value);

    std::vector<std::string> &GetMatchLabelKeys();
    bool MatchLabelKeysIsSet() const;
    void UnsetMatchLabelKeys();
    void SetMatchLabelKeys(const std::vector<std::string> &value);

    int32_t GetMaxSkew() const;
    bool MaxSkewIsSet() const;
    void UnsetMaxSkew();
    void SetMaxSkew(int32_t value);

    int32_t GetMinDomains() const;
    bool MinDomainsIsSet() const;
    void UnsetMinDomains();
    void SetMinDomains(int32_t value);

    std::string GetNodeAffinityPolicy() const;
    bool NodeAffinityPolicyIsSet() const;
    void UnsetNodeAffinityPolicy();
    void SetNodeAffinityPolicy(const std::string &value);

    std::string GetNodeTaintsPolicy() const;
    bool NodeTaintsPolicyIsSet() const;
    void UnsetNodeTaintsPolicy();
    void SetNodeTaintsPolicy(const std::string &value);

    std::string GetTopologyKey() const;
    bool TopologyKeyIsSet() const;
    void UnsetTopologyKey();
    void SetTopologyKey(const std::string &value);

    std::string GetWhenUnsatisfiable() const;
    bool WhenUnsatisfiableIsSet() const;
    void UnsetWhenUnsatisfiable();
    void SetWhenUnsatisfiable(const std::string &value);

protected:
    std::shared_ptr<V1LabelSelector> m_labelSelector;
    bool m_labelSelectorIsSet;
    std::vector<std::string> m_matchLabelKeys;
    bool m_matchLabelKeysIsSet;
    int32_t m_maxSkew;
    bool m_maxSkewIsSet;
    int32_t m_minDomains;
    bool m_minDomainsIsSet;
    std::string m_nodeAffinityPolicy;
    bool m_nodeAffinityPolicyIsSet;
    std::string m_nodeTaintsPolicy;
    bool m_nodeTaintsPolicyIsSet;
    std::string m_topologyKey;
    bool m_topologyKeyIsSet;
    std::string m_whenUnsatisfiable;
    bool m_whenUnsatisfiableIsSet;
};
}
}
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_TOPOLOGY_SPREAD_CONSTRAINT_H_ */
