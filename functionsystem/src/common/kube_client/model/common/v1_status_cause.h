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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1STATUS_CAUSE_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1STATUS_CAUSE_H_

#include <string>

#include "model_base.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_STATUS_CAUSE = "V1StatusCause";

class V1StatusCause : public ModelBase {
public:
    V1StatusCause() noexcept;
    ~V1StatusCause() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& val) override;

    std::string GetField() const;
    bool FieldIsSet() const;
    void UnsetField();
    void SetField(const std::string& value);

    std::string GetMessage() const;
    bool MessageIsSet() const;
    void UnsetMessage();
    void SetMessage(const std::string& value);

    std::string GetReason() const;
    bool ReasonIsSet() const;
    void UnsetReason();
    void SetReason(const std::string& value);

protected:
    std::string m_field;
    bool m_fieldIsSet;
    std::string m_message;
    bool m_messageIsSet;
    std::string m_reason;
    bool m_reasonIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1STATUS_CAUSE_H_ */
