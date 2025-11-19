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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_LIST_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_LIST_H

#include "common/kube_client/model/common/model_base.h"
#include "v2_horizontal_pod_autoscaler.h"
#include "common/kube_client/model/common/v1_list_meta.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V2_HORIZONTAL_POD_AUTOSCALER_LIST = "V2HorizontalPodAutoscalerList";

class V2HorizontalPodAutoscalerList : public ModelBase {
public:
    V2HorizontalPodAutoscalerList();
    ~V2HorizontalPodAutoscalerList() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& val) override;

    std::string GetApiVersion() const;
    bool ApiVersionIsSet() const;
    void UnsetApiVersion();
    void SetApiVersion(const std::string& value);

    std::vector<std::shared_ptr<V2HorizontalPodAutoscaler>>& GetItems();
    bool ItemsIsSet() const;
    void UnsetItems();
    void SetItems(const std::vector<std::shared_ptr<V2HorizontalPodAutoscaler>>& value);

    std::string GetKind() const;
    bool KindIsSet() const;
    void UnsetKind();
    void SetKind(const std::string& value);

    std::shared_ptr<V1ListMeta> GetMetadata() const;
    bool MetadataIsSet() const;
    void UnsetMetadata();
    void SetMetadata(const std::shared_ptr<V1ListMeta>& value);

protected:
    std::string m_apiVersion;
    bool m_apiVersionIsSet;
    std::vector<std::shared_ptr<V2HorizontalPodAutoscaler>> m_items;
    bool m_itemsIsSet;
    std::string m_kind;
    bool m_kindIsSet;
    std::shared_ptr<V1ListMeta> m_metadata;
    bool m_metadataIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_LIST_H */
