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

#include "apps_v1_api.h"

#include <regex>
#include <unordered_set>

#include "common/kube_client/constants.h"
#include "common/logs/logging.h"
#include "common/kube_client/model/common/ihttp_body.h"
#include "common/kube_client/model/common/json_body.h"
#include "common/status/status.h"

namespace functionsystem::kube_client {
namespace api {

using namespace functionsystem::kube_client::model;

AppsV1Api::AppsV1Api(const std::shared_ptr<const ApiClient> &apiClient) : m_apiClient(apiClient)
{
}

AppsV1Api::~AppsV1Api()
{
}

litebus::Future<std::shared_ptr<V1Deployment>> AppsV1Api::CreateNamespacedDeployment(
    const std::string &rNamespace, const std::shared_ptr<V1Deployment> &body) const
{
    // verify the required parameter 'body' is set
    if (body == nullptr) {
        YRLOG_WARN("Missing required parameter 'body' when calling createNamespacedDeployment");
        litebus::Promise<std::shared_ptr<V1Deployment>> promise;
        promise.SetFailed(litebus::Status::KERROR);
        return promise.GetFuture();
    }
    nlohmann::json localVarJson = ModelUtils::ToJson(body);
    std::shared_ptr<IHttpBody> localVarHttpBody = std::make_shared<JsonBody>(localVarJson);
    return CallNamespacedDeploymentApi(localVarHttpBody, "/apis/apps/v1/namespaces/" + rNamespace + "/deployments",
                                       "POST");
}

litebus::Future<std::shared_ptr<V1DeploymentList>> AppsV1Api::ListNamespacedDeployment(
    const std::string &rNamespace, const litebus::Option<std::string> &rContinue,
    const litebus::Option<uint32_t> &limit, const litebus::Option<bool> &watch) const
{
    std::string localVarPath = "/apis/apps/v1/namespaces/" + rNamespace + "/deployments";
    std::map<std::string, std::string> localVarQueryParams = ApiClient::GetLocalQueryParams(rContinue, limit, watch);
    ASSERT_IF_NULL(m_apiClient);
    return m_apiClient->CallApi(localVarPath, std::string("GET"), localVarQueryParams, nullptr)
        .Then([=](std::string localVarResponse) -> litebus::Future<std::shared_ptr<V1DeploymentList>> {
            litebus::Promise<std::shared_ptr<V1DeploymentList>> promise;
            std::shared_ptr<V1DeploymentList> localVarResult = std::make_shared<V1DeploymentList>();
            if (localVarResponse.empty()) {
                YRLOG_WARN("error calling ListNamespacedDeployment: localVarResponse empty");
                if (watch.IsSome()) {
                    auto watchStr = ApiClient::ParameterToString(watch.Get());
                    (void)ListNamespacedDeployment(rNamespace, rContinue, limit, watch);
                    YRLOG_WARN("error calling ListNamespacedPod: watchStr: {}", watchStr);
                }
                promise.SetFailed(litebus::Status::KERROR);
                return promise.GetFuture();
            }
            nlohmann::json localVarJson = nlohmann::json::parse(localVarResponse);
            (void)ModelUtils::FromJson(localVarJson, localVarResult);
            return localVarResult;
        });
}

litebus::Future<std::shared_ptr<V1Deployment>> AppsV1Api::PatchNamespacedDeployment(
    const std::string &name, const std::string &rNamespace, const std::shared_ptr<Object> &body) const
{
    if (body == nullptr) {
        YRLOG_WARN("Missing required parameter 'body' when calling patchNamespacedDeployment");
        litebus::Promise<std::shared_ptr<V1Deployment>> promise;
        promise.SetFailed(litebus::Status::KERROR);
        return promise.GetFuture();
    }
    nlohmann::json localVarJson = ModelUtils::ToJson(body);
    std::shared_ptr<IHttpBody> localVarHttpBody = std::make_shared<JsonBody>(localVarJson);
    return CallNamespacedDeploymentApi(localVarHttpBody,
                                       "/apis/apps/v1/namespaces/" + rNamespace + "/deployments/" + name, "PATCH");
}

litebus::Future<std::shared_ptr<V1Deployment>> AppsV1Api::DeleteNamespacedDeployment(
    const std::string &name, const std::string &rNamespace) const
{
    return CallNamespacedDeploymentApi(nullptr, "/apis/apps/v1/namespaces/" + rNamespace + "/deployments/" + name,
                                       "DELETE");
}

litebus::Future<std::shared_ptr<V1Deployment>> AppsV1Api::ReadNamespacedDeployment(
    const std::string &name, const std::string &rNamespace) const
{
    return CallNamespacedDeploymentApi(nullptr, "/apis/apps/v1/namespaces/" + rNamespace + "/deployments/" + name,
                                       "GET");
}

litebus::Future<std::shared_ptr<V1Deployment>> AppsV1Api::CallNamespacedDeploymentApi(
    const std::shared_ptr<IHttpBody> &body, const std::string &url, const std::string &method) const
{
    ASSERT_IF_NULL(m_apiClient);
    return m_apiClient->CallApi(url, method, {}, body).Then([](std::string localVarResponse) {
        std::shared_ptr<V1Deployment> localVarResult = std::make_shared<V1Deployment>();
        nlohmann::json localVarJson = nlohmann::json::parse(localVarResponse);
        (void)ModelUtils::FromJson(localVarJson, localVarResult);
        return localVarResult;
    });
}

}  // namespace api
}  // namespace functionsystem::kube_client
