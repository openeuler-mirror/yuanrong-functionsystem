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
 * V1Node.h
 *
 * Node is a worker node in Kubernetes. Each node will have a unique identifier in the cache (i.e. in etcd).
 */

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_H_

#include <string>

#include "common/kube_client/model/common/model_base.h"
#include "v1_node_spec.h"
#include "v1_node_status.h"
#include "common/kube_client/model/common/v1_object_meta.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_NODE = "V1Node";

class V1Node : public ModelBase {
public:
    V1Node();
    ~V1Node() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::string GetApiVersion() const;
    bool ApiVersionIsSet() const;
    void UnsetApiVersion();
    void SetApiVersion(const std::string &value);

    bool KindIsSet() const;
    void UnsetKind();
    std::string GetKind() const;
    void SetKind(const std::string &value);

    bool MetadataIsSet() const;
    void UnsetMetadata();
    std::shared_ptr<V1ObjectMeta> GetMetadata() const;
    void SetMetadata(const std::shared_ptr<V1ObjectMeta> &value);

    std::shared_ptr<V1NodeSpec> GetSpec() const;
    bool SpecIsSet() const;
    void UnsetSpec();
    void SetSpec(const std::shared_ptr<V1NodeSpec> &value);

    std::shared_ptr<V1NodeStatus> GetStatus() const;
    bool StatusIsSet() const;
    void UnsetStatus();
    void SetStatus(const std::shared_ptr<V1NodeStatus> &value);

protected:
    std::string m_apiVersion;
    bool m_apiVersionIsSet;
    std::string m_kind;
    bool m_kindIsSet;
    std::shared_ptr<V1ObjectMeta> m_metadata;
    bool m_metadataIsSet;
    std::shared_ptr<V1NodeSpec> m_spec;
    bool m_specIsSet;
    std::shared_ptr<V1NodeStatus> m_status;
    bool m_statusIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_H_ */
