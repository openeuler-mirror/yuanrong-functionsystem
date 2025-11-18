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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_API_AUTOSCALING_V2_API_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_API_AUTOSCALING_V2_API_H

#include <string>

#include "async/future.hpp"
#include "async/option.hpp"
#include "api_client.h"
#include "common/kube_client/model/common/object.h"
#include "common/kube_client/model/common/v1_delete_options.h"
#include "common/kube_client/model/horizontal_pod_autoscaler/v2_horizontal_pod_autoscaler.h"
#include "common/kube_client/model/horizontal_pod_autoscaler/v2_horizontal_pod_autoscaler_list.h"
#include "common/kube_client/model/common/v1_status.h"

namespace functionsystem::kube_client {
namespace api {

using namespace functionsystem::kube_client::model;

class AutoscalingV2Api {
public:
    explicit AutoscalingV2Api(std::shared_ptr<const ApiClient> apiClient);

    virtual ~AutoscalingV2Api();

    litebus::Future<std::shared_ptr<V2HorizontalPodAutoscaler>> CreateNamespacedHorizontalPodAutoscaler(
        const std::string &rNamespace,
        const std::shared_ptr<V2HorizontalPodAutoscaler> &body) const;

    litebus::Future<std::shared_ptr<V1Status>> DeleteNamespacedHorizontalPodAutoscaler(
        const std::string &name,
        const std::string &rNamespace) const;

    litebus::Future<std::shared_ptr<V2HorizontalPodAutoscalerList>> ListNamespacedHorizontalPodAutoscaler(
        const std::string &rNamespace,
        const litebus::Option<std::string> &rContinue,
        const litebus::Option<uint32_t> &limit,
        const litebus::Option<bool> &watch) const;

    litebus::Future<std::shared_ptr<V2HorizontalPodAutoscaler>> PatchNamespacedHorizontalPodAutoscaler(
        const std::string &name,
        const std::string &rNamespace,
        const std::shared_ptr<Object> &body) const;

protected:
    std::shared_ptr<const ApiClient> m_apiClient;
};

}  // namespace api
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_API_AUTOSCALING_V2_API_H */
