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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LABEL_SELECTOR_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LABEL_SELECTOR_H_

#include <map>
#include <string>
#include <vector>

#include "model_base.h"
#include "v1_label_selector_requirement.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_LABEL_SELECTOR = "V1LabelSelector";

class V1LabelSelector : public ModelBase {
public:
    V1LabelSelector();
    ~V1LabelSelector() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::vector<std::shared_ptr<V1LabelSelectorRequirement>> &GetMatchExpressions();
    bool MatchExpressionsIsSet() const;
    void UnsetMatchExpressions();
    void SetMatchExpressions(const std::vector<std::shared_ptr<V1LabelSelectorRequirement>> &value);

    std::map<std::string, std::string> &GetMatchLabels();
    bool MatchLabelsIsSet() const;
    void UnsetMatchLabels();
    void SetMatchLabels(const std::map<std::string, std::string> &value);

protected:
    std::vector<std::shared_ptr<V1LabelSelectorRequirement>> m_matchExpressions;
    bool m_matchExpressionsIsSet;
    std::map<std::string, std::string> m_matchLabels;
    bool m_matchLabelsIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LABEL_SELECTOR_H_ */
