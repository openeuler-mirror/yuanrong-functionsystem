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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_H

#include <string>

#include "common/kube_client/model/common/model_base.h"
#include "v2_horizontal_pod_autoscaler_spec.h"
#include "v2_horizontal_pod_autoscaler_status.h"
#include "common/kube_client/model/common/v1_object_meta.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V2_HORIZONTAL_POD_AUTOSCALER = "V2HorizontalPodAutoscaler";

class V2HorizontalPodAutoscaler : public ModelBase {
public:
    V2HorizontalPodAutoscaler();
    ~V2HorizontalPodAutoscaler() override;

    bool FromJson(const nlohmann::json& val) override;
    nlohmann::json ToJson() const override;

    bool ApiVersionIsSet() const;
    void UnsetApiVersion();
    void SetApiVersion(const std::string &value);
    std::string GetApiVersion() const;

    std::string GetKind() const;
    bool KindIsSet() const;
    void UnsetKind();
    void SetKind(const std::string &value);

    std::shared_ptr<V1ObjectMeta> GetMetadata() const;
    bool MetadataIsSet() const;
    void UnsetMetadata();
    void SetMetadata(const std::shared_ptr<V1ObjectMeta> &value);

    std::shared_ptr<V2HorizontalPodAutoscalerSpec> GetSpec() const;
    bool SpecIsSet() const;
    void UnsetSpec();
    void SetSpec(const std::shared_ptr<V2HorizontalPodAutoscalerSpec> &value);

    std::shared_ptr<V2HorizontalPodAutoscalerStatus> GetStatus() const;
    bool StatusIsSet() const;
    void UnsetStatus();
    void SetStatus(const std::shared_ptr<V2HorizontalPodAutoscalerStatus>& value);

protected:
    std::string m_apiVersion;
    bool m_apiVersionIsSet;
    std::string m_kind;
    bool m_kindIsSet;
    std::shared_ptr<V1ObjectMeta> m_metadata;
    bool m_metadataIsSet;
    std::shared_ptr<V2HorizontalPodAutoscalerSpec> m_spec;
    bool m_specIsSet;
    std::shared_ptr<V2HorizontalPodAutoscalerStatus> m_status;
    bool m_statusIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2HORIZONTALPODAUTOSCALER_H */
