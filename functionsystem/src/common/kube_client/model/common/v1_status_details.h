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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1STATUS_DETAILS_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1STATUS_DETAILS_H_

#include <string>

#include "model_base.h"
#include "v1_status_cause.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_STATUS_DETAILS = "V1StatusDetails";

class V1StatusDetails : public ModelBase {
public:
    V1StatusDetails() noexcept;
    ~V1StatusDetails() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& val) override;

    std::vector<std::shared_ptr<V1StatusCause>>& GetCauses();
    bool CausesIsSet() const;
    void UnsetCauses();
    void SetCauses(const std::vector<std::shared_ptr<V1StatusCause>>& value);

    std::string GetGroup() const;
    bool GroupIsSet() const;
    void UnsetGroup();
    void SetGroup(const std::string& value);

    std::string GetKind() const;
    bool KindIsSet() const;
    void UnsetKind();
    void SetKind(const std::string& value);

    std::string GetName() const;
    bool NameIsSet() const;
    void UnsetName();
    void SetName(const std::string& value);

    int32_t GetRetryAfterSeconds() const;
    bool RetryAfterSecondsIsSet() const;
    void UnsetRetryAfterSeconds();
    void SetRetryAfterSeconds(int32_t value);

    std::string GetUid() const;
    bool UidIsSet() const;
    void UnsetUid();
    void SetUid(const std::string& value);

protected:
    std::vector<std::shared_ptr<V1StatusCause>> m_causes;
    bool m_causesIsSet;
    std::string m_group;
    bool m_groupIsSet;
    std::string m_kind;
    bool m_kindIsSet;
    std::string m_name;
    bool m_nameIsSet;
    int32_t m_retryAfterSeconds;
    bool m_retryAfterSecondsIsSet;
    std::string m_uid;
    bool m_uidIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1STATUS_DETAILS_H_ */
