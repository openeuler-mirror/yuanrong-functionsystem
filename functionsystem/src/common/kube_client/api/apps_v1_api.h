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

#ifndef FUNCTIONSYSTEM_KUBE_CLIENT_API_APPS_V1API_H
#define FUNCTIONSYSTEM_KUBE_CLIENT_API_APPS_V1API_H

#include <string>

#include "async/future.hpp"
#include "async/option.hpp"
#include "api_client.h"
#include "common/kube_client/model/common/object.h"
#include "common/kube_client/model/deployment/v1_deployment.h"
#include "common/kube_client/model/deployment/v1_deployment_list.h"

namespace functionsystem::kube_client {
namespace api {

using namespace functionsystem::kube_client::model;

class AppsV1Api {
public:
    explicit AppsV1Api(const std::shared_ptr<const ApiClient> &apiClient);

    virtual ~AppsV1Api();

    litebus::Future<std::shared_ptr<V1Deployment>> CreateNamespacedDeployment(
        const std::string &rNamespace, const std::shared_ptr<V1Deployment> &body) const;

    litebus::Future<std::shared_ptr<V1DeploymentList>> ListNamespacedDeployment(
        const std::string &rNamespace, const litebus::Option<std::string> &rContinue,
        const litebus::Option<uint32_t> &limit, const litebus::Option<bool> &watch) const;

    litebus::Future<std::shared_ptr<V1Deployment>> PatchNamespacedDeployment(const std::string &name,
                                                                             const std::string &rNamespace,
                                                                             const std::shared_ptr<Object> &body) const;

    litebus::Future<std::shared_ptr<V1Deployment>> DeleteNamespacedDeployment(const std::string &name,
                                                                              const std::string &rNamespace) const;

    litebus::Future<std::shared_ptr<V1Deployment>> ReadNamespacedDeployment(const std::string &name,
                                                                            const std::string &rNamespace) const;

protected:
    litebus::Future<std::shared_ptr<V1Deployment>> CallNamespacedDeploymentApi(const std::shared_ptr<IHttpBody> &body,
                                                                               const std::string &url,
                                                                               const std::string &method) const;
    std::shared_ptr<const ApiClient> m_apiClient;
};

}  // namespace api
}  // namespace functionsystem::kube_client

#endif /* FUNCTIONSYSTEM_KUBE_CLIENT_API_APPS_V1API_H */
