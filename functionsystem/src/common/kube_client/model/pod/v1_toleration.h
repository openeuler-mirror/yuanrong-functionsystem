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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_TOLERATION_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_TOLERATION_H_

#include <string>

#include "common/kube_client/model/common/model_base.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_TOLERATION = "V1Toleration";

class V1Toleration : public ModelBase {
public:
    V1Toleration();
    ~V1Toleration() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::string GetEffect() const;
    bool EffectIsSet() const;
    void UnSetEffect();
    void SetEffect(const std::string &value);

    std::string GetKey() const;
    bool KeyIsSet() const;
    void UnsetKey();
    void SetKey(const std::string &value);

    std::string GetROperator() const;
    bool ROperatorIsSet() const;
    void UnsetROperator();
    void SetROperator(const std::string &value);

    int64_t GetTolerationSeconds() const;
    bool TolerationSecondsIsSet() const;
    void UnsetTolerationSeconds();

    void SetTolerationSeconds(int64_t value);

    std::string GetValue() const;
    bool ValueIsSet() const;
    void UnsetValue();

    void SetValue(const std::string &value);

protected:
    std::string m_effect;
    bool m_effectIsSet;
    std::string m_key;
    bool m_keyIsSet;
    std::string m_rOperator;
    bool m_rOperatorIsSet;
    int64_t m_tolerationSeconds;
    bool m_tolerationSecondsIsSet;
    std::string m_value;
    bool m_valueIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_TOLERATION_H_ */
