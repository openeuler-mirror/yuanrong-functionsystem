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

/*
 * V1Pod.h
 *
 * Pod is a collection of containers that can run on a host. This resource is created by clients and scheduled onto
 * hosts.
 */

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_POD_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_POD_H_

#include <string>

#include "common/kube_client/model/common/model_base.h"
#include "common/kube_client/model/common/v1_object_meta.h"
#include "v1_pod_spec.h"
#include "v1_pod_status.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_POD = "V1Pod";

class V1Pod : public ModelBase {
public:
    V1Pod();
    ~V1Pod() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    bool ApiVersionIsSet() const;
    void UnsetApiVersion();
    void SetApiVersion(const std::string &value);
    std::string GetApiVersion() const;

    std::string GetKind() const;
    bool KindIsSet() const;
    void UnsetKind();
    void SetKind(const std::string &value);

    bool MetadataIsSet() const;
    void UnsetMetadata();
    void SetMetadata(const std::shared_ptr<V1ObjectMeta> &value);
    std::shared_ptr<V1ObjectMeta> GetMetadata() const;

    std::shared_ptr<V1PodSpec> GetSpec() const;
    bool SpecIsSet() const;
    void UnsetSpec();
    void SetSpec(const std::shared_ptr<V1PodSpec> &value);

    std::shared_ptr<V1PodStatus> GetStatus() const;
    bool StatusIsSet() const;
    void UnsetStatus();
    void SetStatus(const std::shared_ptr<V1PodStatus> &value);

protected:
    std::string m_apiVersion;
    bool m_apiVersionIsSet;
    std::string m_kind;
    bool m_kindIsSet;
    std::shared_ptr<V1ObjectMeta> m_metadata;
    bool m_metadataIsSet;
    std::shared_ptr<V1PodSpec> m_spec;
    bool m_specIsSet;
    std::shared_ptr<V1PodStatus> m_status;
    bool m_statusIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_POD_H_ */
