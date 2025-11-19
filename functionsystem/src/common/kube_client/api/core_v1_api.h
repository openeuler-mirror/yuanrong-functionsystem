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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_API_CORE_V1_API_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_API_CORE_V1_API_H_

#include <string>

#include "async/future.hpp"
#include "async/option.hpp"
#include "api_client.h"
#include "common/kube_client/model/common/object.h"
#include "common/kube_client/model/common/v1_delete_options.h"
#include "common/kube_client/model/node/v1_node.h"
#include "common/kube_client/model/node/v1_node_list.h"
#include "common/kube_client/model/pod/v1_pod.h"
#include "common/kube_client/model/pod/v1_pod_list.h"

namespace functionsystem::kube_client {
namespace api {

using namespace functionsystem::kube_client::model;

class CoreV1Api {
public:
    explicit CoreV1Api(const std::shared_ptr<const ApiClient> &apiClient);

    virtual ~CoreV1Api();

    litebus::Future<std::shared_ptr<V1Pod>> CreateNamespacedPod(const std::string &rNamespace,
                                                                const std::shared_ptr<V1Pod> &body) const;

    litebus::Future<std::shared_ptr<V1Pod>> DeleteNamespacedPod(
        const std::string &name, const std::string &rNamespace, const litebus::Option<bool> &orphanDependents,
        const litebus::Option<std::shared_ptr<V1DeleteOptions>> &body) const;

    litebus::Future<std::shared_ptr<V1PodList>> ListNamespacedPod(const std::string &rNamespace,
                                                                  const litebus::Option<std::string> &rContinue,
                                                                  const litebus::Option<uint32_t> &limit,
                                                                  const litebus::Option<bool> &watch) const;

    litebus::Future<std::shared_ptr<V1Pod>> ReadNamespacedPod(const std::string &rNamespace,
                                                              const std::string &name) const;

    litebus::Future<std::shared_ptr<V1NodeList>> ListNode(const litebus::Option<std::string> &rContinue,
                                                          const litebus::Option<uint32_t> &limit,
                                                          const litebus::Option<bool> &watch) const;

    litebus::Future<std::shared_ptr<V1Pod>> PatchNamespacedPod(const std::string &name, const std::string &rNamespace,
                                                               const std::shared_ptr<Object> &body) const;

    litebus::Future<std::shared_ptr<V1Node>> PatchNode(const std::string &name,
                                                       const std::shared_ptr<Object> &body) const;

    litebus::Future<std::string> ListForMonitor() const;

    litebus::Future<std::shared_ptr<V1Pod>> CallNamespacedPodApi(
        const std::shared_ptr<IHttpBody> &localVarHttpBody, const std::string &url, const std::string &method,
        const std::map<std::string, std::string> &queryParams) const;

protected:
    std::shared_ptr<const ApiClient> m_apiClient;
};

}  // namespace api
}  // namespace functionsystem::kube_client

#endif /* ORG_OPENAPITOOLS_CLIENT_API_CoreV1Api_H_ */
