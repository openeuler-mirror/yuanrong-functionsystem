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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1STATUS_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1STATUS_H_

#include <string>

#include "model_base.h"
#include "v1_status_details.h"
#include "v1_list_meta.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_STATUS = "V1Status";

class V1Status : public ModelBase {
public:
    V1Status() noexcept;
    ~V1Status() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& val) override;

    std::string GetApiVersion() const;
    bool ApiVersionIsSet() const;
    void UnsetApiVersion();
    void SetApiVersion(const std::string& value);

    int32_t GetCode() const;
    bool CodeIsSet() const;
    void UnsetCode();
    void SetCode(int32_t value);

    std::shared_ptr<V1StatusDetails> GetDetails() const;
    bool DetailsIsSet() const;
    void UnsetDetails();
    void SetDetails(const std::shared_ptr<V1StatusDetails>& value);

    std::string GetKind() const;
    bool KindIsSet() const;
    void UnsetKind();
    void SetKind(const std::string& value);

    std::string GetMessage() const;
    bool MessageIsSet() const;
    void UnsetMessage();
    void SetMessage(const std::string& value);

    std::shared_ptr<V1ListMeta> GetMetadata() const;
    bool MetadataIsSet() const;
    void UnsetMetadata();
    void SetMetadata(const std::shared_ptr<V1ListMeta>& value);

    std::string GetReason() const;
    bool ReasonIsSet() const;
    void UnsetReason();
    void SetReason(const std::string& value);

    std::string GetStatus() const;
    bool StatusIsSet() const;
    void UnsetStatus();
    void SetStatus(const std::string& value);

protected:
    std::string m_apiVersion;
    bool m_apiVersionIsSet;
    int32_t m_code;
    bool m_codeIsSet;
    std::shared_ptr<V1StatusDetails> m_details;
    bool m_detailsIsSet;
    std::string m_kind;
    bool m_kindIsSet;
    std::string m_message;
    bool m_messageIsSet;
    std::shared_ptr<V1ListMeta> m_metadata;
    bool m_metadataIsSet;
    std::string m_reason;
    bool m_reasonIsSet;
    std::string m_status;
    bool m_statusIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1STATUS_H_ */
