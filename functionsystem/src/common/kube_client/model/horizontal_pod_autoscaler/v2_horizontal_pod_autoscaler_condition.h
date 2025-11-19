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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_CONDITION_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_CONDITION_H

#include "common/kube_client/model/common/model_base.h"
#include "common/kube_client/api/api_exception.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V2_HORIZONTAL_POD_AUTOSCALER_CONDITION = "V2HorizontalPodAutoscalerCondition";

class V2HorizontalPodAutoscalerCondition : public ModelBase {
public:
    V2HorizontalPodAutoscalerCondition();
    ~V2HorizontalPodAutoscalerCondition() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& val) override;

    utility::Datetime GetLastTransitionTime() const;
    bool LastTransitionTimeIsSet() const;
    void UnsetLastTransitionTime();
    void SetLastTransitionTime(const utility::Datetime& value);

    std::string GetMessage() const;
    bool MessageIsSet() const;
    void UnsetMessage();
    void SetMessage(const std::string& value);

    std::string GetReason() const;
    bool ReasonIsSet() const;
    void UnsetReason();
    void SetReason(const std::string& value);

    std::string GetStatus() const;
    bool StatusIsSet() const;
    void UnsetStatus();
    void SetStatus(const std::string& value);

    std::string GetType() const;
    bool TypeIsSet() const;
    void UnsetType();
    void SetType(const std::string& value);

protected:
    utility::Datetime m_lastTransitionTime;
    bool m_lastTransitionTimeIsSet;
    std::string m_message;
    bool m_messageIsSet;
    std::string m_reason;
    bool m_reasonIsSet;
    std::string m_status;
    bool m_statusIsSet;
    std::string m_type;
    bool m_typeIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_CONDITION_H */
