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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_LIST_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_LIST_H_

#include <string>
#include <vector>

#include "common/kube_client/model/common/model_base.h"
#include "common/kube_client/model/common/v1_list_meta.h"
#include "v1_node.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V1_NODE_LIST = "V1NodeList";

class V1NodeList : public ModelBase {
public:
    V1NodeList();
    ~V1NodeList() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json &json) override;

    std::shared_ptr<V1ListMeta> GetMetadata() const;
    bool MetadataIsSet() const;
    void UnsetMetadata();
    void SetMetadata(const std::shared_ptr<V1ListMeta> &value);

    std::string GetApiVersion() const;
    bool ApiVersionIsSet() const;
    void UnsetApiVersion();
    void SetApiVersion(const std::string &value);

    std::vector<std::shared_ptr<V1Node>> &GetItems();
    bool ItemsIsSet() const;
    void UnsetItems();
    void SetItems(const std::vector<std::shared_ptr<V1Node>> &value);

    std::string GetKind() const;
    bool KindIsSet() const;
    void UnsetKind();
    void SetKind(const std::string &value);

protected:
    std::string m_apiVersion;
    bool m_apiVersionIsSet;
    std::vector<std::shared_ptr<V1Node>> m_items;
    bool m_itemsIsSet;
    std::string m_kind;
    bool m_kindIsSet;
    std::shared_ptr<V1ListMeta> m_metadata;
    bool m_metadataIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V1_NODE_LIST_H_ */
