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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LEASE_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LEASE_H_

#include <string>

#include "common/kube_client/model/common/model_base.h"
#include "common/kube_client/model/common/v1_object_meta.h"
#include "v1_lease_spec.h"

namespace functionsystem::kube_client {
namespace model {

class V1Lease : public ModelBase {
public:
    V1Lease();
    ~V1Lease() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& json) override;

    std::string GetApiVersion() const;
    bool ApiVersionIsSet() const;
    void UnsetApiVersion();
    void SetApiVersion(const std::string& value);

    std::string GetKind() const;
    bool KindIsSet() const;
    void UnsetKind();
    void SetKind(const std::string& value);

    std::shared_ptr<V1ObjectMeta> GetMetadata() const;
    bool MetadataIsSet() const;
    void UnsetMetadata();
    void SetMetadata(const std::shared_ptr<V1ObjectMeta>& value);

    std::shared_ptr<V1LeaseSpec> GetSpec() const;
    bool SpecIsSet() const;
    void UnsetSpec();
    void SetSpec(const std::shared_ptr<V1LeaseSpec>& value);
protected:
    std::string m_apiVersion;
    bool m_apiVersionIsSet;
    std::string m_kind;
    bool m_kindIsSet;
    std::shared_ptr<V1ObjectMeta> m_metadata;
    bool m_metadataIsSet;
    std::shared_ptr<V1LeaseSpec> m_spec;
    bool m_specIsSet;
};
}
}
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_LEASE_H_ */
