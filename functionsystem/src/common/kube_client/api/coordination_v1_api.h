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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_API_COORDINATION_V1_API_H_
#define FUNCTIONSYSTEM_KUBE_CLIENT_API_COORDINATION_V1_API_H_

#include <string>

#include "async/future.hpp"
#include "async/option.hpp"
#include "api_client.h"
#include "common/kube_client/model/lease/v1_lease.h"

namespace functionsystem::kube_client {
namespace api {

using namespace functionsystem::kube_client::model;

class CoordinationV1Api {
public:
    explicit CoordinationV1Api(const std::shared_ptr<const ApiClient> &apiClient): m_apiClient(apiClient)
    {
    }

    virtual ~CoordinationV1Api()
    {
    }

    litebus::Future<std::shared_ptr<V1Lease>> CreateNamespacedLease(const std::string &rNamespace,
                                                                    const std::shared_ptr<V1Lease> &body) const;
    litebus::Future<std::shared_ptr<V1Lease>> ReadNamespacedLease(const std::string &name,
                                                                  const std::string &rNamespace) const;
    litebus::Future<std::shared_ptr<V1Lease>> ReplaceNamespacedLease(const std::string &name,
                                                                     const std::string &rNamespace,
                                                                     const std::shared_ptr<V1Lease> &body) const;
    litebus::Future<std::shared_ptr<V1Lease>> CallNamespacedLeaseApi(const std::shared_ptr<V1Lease> &body,
                                                                     const std::string &url,
                                                                     const std::string &method) const;

protected:
    std::shared_ptr<const ApiClient> m_apiClient;
};
}  // namespace api
}  // namespace functionsystem::kube_client
#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_API_COORDINATION_V1_API_H_ */

