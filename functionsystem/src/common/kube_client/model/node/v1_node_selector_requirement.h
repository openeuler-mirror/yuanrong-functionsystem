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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_SELECTOR_REQUIREMENT_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_SELECTOR_REQUIREMENT_H_

#include <string>
#include <vector>

#include "common/kube_client/model/common/model_base.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_NODE_SELECTOR_REQUIREMENT = "V1NodeSelectorRequirement";

class V1NodeSelectorRequirement : public ModelBase {
public:
    V1NodeSelectorRequirement();
    ~V1NodeSelectorRequirement() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& json) override;

    std::string GetKey() const;
    bool KeyIsSet() const;
    void UnsetKey();
    void SetKey(const std::string& value);

    std::string GetROperator() const;
    bool ROperatorIsSet() const;
    void UnsetROperator();
    void SetROperator(const std::string& value);

    std::vector<std::string>& GetValues();
    bool ValuesIsSet() const;
    void UnsetValues();
    void SetValues(const std::vector<std::string>& value);
protected:
    std::string m_key;
    bool m_keyIsSet;
    std::string m_rOperator;
    bool m_rOperatorIsSet;
    std::vector<std::string> m_values;
    bool m_valuesIsSet;
};
}
}
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_SELECTOR_REQUIREMENT_H_ */
