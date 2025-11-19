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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2METRICS_IDENTIFIER_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2METRICS_IDENTIFIER_H

#include <string>

#include "common/kube_client/model/common/model_base.h"
#include "v1_label_selector.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V2_METRIC_IDENTIFIER = "V2MetricIdentifier";
class V2MetricIdentifier : public ModelBase {
public:
    V2MetricIdentifier() noexcept;
    ~V2MetricIdentifier() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& val) override;

    std::string GetName() const;
    bool NameIsSet() const;
    void UnsetName();
    void SetName(const std::string& value);

    std::shared_ptr<V1LabelSelector> GetSelector() const;
    bool SelectorIsSet() const;
    void UnsetSelector();
    void SetSelector(const std::shared_ptr<V1LabelSelector>& value);

protected:
    std::string m_name;
    bool m_nameIsSet;
    std::shared_ptr<V1LabelSelector> m_selector;
    bool m_selectorIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2METRICS_IDENTIFIER_H */
