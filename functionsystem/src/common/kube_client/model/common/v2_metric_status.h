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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2METRICS_STATUS_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2METRICS_STATUS_H

#include <string>

#include "common/kube_client/model/common/model_base.h"
#include "v2_object_metric_status.h"
#include "v2_container_resource_metric_status.h"
#include "v2_external_metric_status.h"
#include "v2_resource_metric_status.h"
#include "v2_pods_metric_status.h"

namespace functionsystem::kube_client {
namespace model {

const std::string MODE_NAME_V2_METRIC_STATUS = "V2MetricStatus";
class V2MetricStatus : public ModelBase {
public:
    V2MetricStatus() noexcept;
    ~V2MetricStatus() override;

    nlohmann::json ToJson() const override;
    bool FromJson(const nlohmann::json& val) override;

    std::shared_ptr<V2ContainerResourceMetricStatus> GetContainerResource() const;
    bool ContainerResourceIsSet() const;
    void UnsetContainerResource();
    void SetContainerResource(const std::shared_ptr<V2ContainerResourceMetricStatus>& value);

    std::shared_ptr<V2ExternalMetricStatus> GetExternal() const;
    bool ExternalIsSet() const;
    void UnsetExternal();
    void SetExternal(const std::shared_ptr<V2ExternalMetricStatus>& value);

    std::shared_ptr<V2ObjectMetricStatus> GetObject() const;
    bool ObjectIsSet() const;
    void Unsetobject();
    void SetObject(const std::shared_ptr<V2ObjectMetricStatus>& value);

    std::shared_ptr<V2PodsMetricStatus> GetPods() const;
    bool PodsIsSet() const;
    void UnsetPods();
    void SetPods(const std::shared_ptr<V2PodsMetricStatus>& value);

    std::shared_ptr<V2ResourceMetricStatus> GetResource() const;
    bool ResourceIsSet() const;
    void UnsetResource();
    void SetResource(const std::shared_ptr<V2ResourceMetricStatus>& value);

    std::string GetType() const;
    bool TypeIsSet() const;
    void UnsetType();
    void SetType(const std::string& value);

protected:
    std::shared_ptr<V2ContainerResourceMetricStatus> m_containerResource;
    bool m_containerResourceIsSet;
    std::shared_ptr<V2ExternalMetricStatus> m_external;
    bool m_externalIsSet;
    std::shared_ptr<V2ObjectMetricStatus> m_object;
    bool m_objectIsSet;
    std::shared_ptr<V2PodsMetricStatus> m_pods;
    bool m_podsIsSet;
    std::shared_ptr<V2ResourceMetricStatus> m_resource;
    bool m_resourceIsSet;
    std::string m_type;
    bool m_typeIsSet;
};

}  // namespace model
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_MODEL_V2METRICS_STATUS_H */
