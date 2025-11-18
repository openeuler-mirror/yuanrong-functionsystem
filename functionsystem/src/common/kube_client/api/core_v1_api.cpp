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

#include "core_v1_api.h"

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

CoreV1Api::CoreV1Api(const std::shared_ptr<const ApiClient> &apiClient) : m_apiClient(apiClient)
{
}

CoreV1Api::~CoreV1Api()
{
}

litebus::Future<std::shared_ptr<V1Pod>> CoreV1Api::CreateNamespacedPod(const std::string &rNamespace,
                                                                       const std::shared_ptr<V1Pod> &body) const
{
    litebus::Promise<std::shared_ptr<V1Pod>> promise;

    // verify the required parameter 'body' is set
    if (body == nullptr) {
        YRLOG_WARN("Missing required parameter 'body' when calling CoreV1Api->CreateNamespacedPod");
        promise.SetFailed(litebus::Status::KERROR);
        return promise.GetFuture();
    }
    std::string localVarPath = "/api/v1/namespaces/" + rNamespace + "/pods";
    nlohmann::json localVarJson = ModelUtils::ToJson(body);
    std::shared_ptr<IHttpBody> localVarHttpBody = std::make_shared<JsonBody>(localVarJson);
    return CallNamespacedPodApi(localVarHttpBody, "/api/v1/namespaces/" + rNamespace + "/pods", "POST", {});
}

litebus::Future<std::shared_ptr<V1Pod>> CoreV1Api::DeleteNamespacedPod(
    const std::string &name, const std::string &rNamespace, const litebus::Option<bool> &orphanDependents,
    const litebus::Option<std::shared_ptr<V1DeleteOptions>> &body) const
{
    std::map<std::string, std::string> localVarQueryParams;
    if (orphanDependents.IsSome()) {
        localVarQueryParams[std::string("orphanDependents")] = ApiClient::ParameterToString(orphanDependents.Get());
    }
    nlohmann::json localVarJson;
    if (body.IsSome()) {
        localVarJson = ModelUtils::ToJson(body.Get());
    }
    std::shared_ptr<IHttpBody> localVarHttpBody = std::make_shared<JsonBody>(localVarJson);
    return CallNamespacedPodApi(localVarHttpBody, "/api/v1/namespaces/" + rNamespace + "/pods/" + name, "DELETE",
                                localVarQueryParams);
}

litebus::Future<std::shared_ptr<V1PodList>> CoreV1Api::ListNamespacedPod(const std::string &rNamespace,
                                                                         const litebus::Option<std::string> &rContinue,
                                                                         const litebus::Option<uint32_t> &limit,
                                                                         const litebus::Option<bool> &watch) const
{
    litebus::Promise<std::shared_ptr<V1PodList>> promise;
    std::string localVarPath = "/api/v1/namespaces/" + rNamespace + "/pods";
    std::map<std::string, std::string> localVarQueryParams = ApiClient::GetLocalQueryParams(rContinue, limit, watch);
    ASSERT_IF_NULL(m_apiClient);
    return m_apiClient->CallApi(localVarPath, std::string("GET"), localVarQueryParams, nullptr)
        .Then([=](std::string localVarResponse) -> litebus::Future<std::shared_ptr<V1PodList>> {
            std::shared_ptr<V1PodList> localVarResult = std::make_shared<V1PodList>();
            if (localVarResponse.empty()) {
                YRLOG_WARN("error calling ListNamespacedPod: localVarResponse empty");

                if (watch.IsSome()) {
                    auto watchStr = ApiClient::ParameterToString(watch.Get());
                    (void)ListNamespacedPod(rNamespace, rContinue, limit, watch);
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

litebus::Future<std::shared_ptr<V1Pod>> CoreV1Api::ReadNamespacedPod(const std::string &rNamespace,
                                                                     const std::string &name) const
{
    std::string localVarPath = "/api/v1/namespaces/" + rNamespace + "/pods/" + name;
    return CallNamespacedPodApi(nullptr, localVarPath, "GET", {});
}

litebus::Future<std::shared_ptr<V1Pod>> CoreV1Api::CallNamespacedPodApi(
    const std::shared_ptr<IHttpBody> &localVarHttpBody, const std::string &url, const std::string &method,
    const std::map<std::string, std::string> &queryParams) const
{
    ASSERT_IF_NULL(m_apiClient);
    return m_apiClient->CallApi(url, method, queryParams, localVarHttpBody)
        .Then([](const std::string &localVarResponse) {
            std::shared_ptr<V1Pod> localVarResult = std::make_shared<V1Pod>();
            nlohmann::json localVarJson = nlohmann::json::parse(localVarResponse);
            (void)ModelUtils::FromJson(localVarJson, localVarResult);
            return localVarResult;
    });
}

litebus::Future<std::shared_ptr<V1NodeList>> CoreV1Api::ListNode(const litebus::Option<std::string> &rContinue,
                                                                 const litebus::Option<uint32_t> &limit,
                                                                 const litebus::Option<bool> &watch) const
{
    litebus::Promise<std::shared_ptr<V1NodeList>> promise;
    std::string localVarPath = std::string("/api/v1/nodes");
    std::map<std::string, std::string> localVarQueryParams = ApiClient::GetLocalQueryParams(rContinue, limit, watch);
    ASSERT_IF_NULL(m_apiClient);
    return m_apiClient->CallApi(localVarPath, std::string("GET"), localVarQueryParams, nullptr)
        .Then([=](std::string localVarResponse) -> litebus::Future<std::shared_ptr<V1NodeList>> {
            std::shared_ptr<V1NodeList> localVarResult = std::make_shared<V1NodeList>();
            if (localVarResponse.empty()) {
                YRLOG_WARN("error calling ListNode: localVarResponse empty");

                if (watch.IsSome()) {
                    auto watchStr = ApiClient::ParameterToString(watch.Get());
                    (void)ListNode(rContinue, limit, watch);
                    YRLOG_WARN("error calling ListNode: watchStr: {}", watchStr);
                }

                promise.SetFailed(litebus::Status::KERROR);
                return promise.GetFuture();
            }
            nlohmann::json localVarJson = nlohmann::json::parse(localVarResponse);
            (void)ModelUtils::FromJson(localVarJson, localVarResult);
            return localVarResult;
        });
}

litebus::Future<std::shared_ptr<V1Pod>> CoreV1Api::PatchNamespacedPod(const std::string &name,
                                                                      const std::string &rNamespace,
                                                                      const std::shared_ptr<Object> &body) const
{
    std::string localVarPath = "/api/v1/namespaces/" + rNamespace + "/pods/" + name;
    nlohmann::json localVarJson = ModelUtils::ToJson(body);
    std::shared_ptr<IHttpBody> localVarHttpBody = std::make_shared<JsonBody>(localVarJson);
    ASSERT_IF_NULL(m_apiClient);
    return m_apiClient->CallApi(localVarPath, std::string("PATCH"), {}, localVarHttpBody)
        .Then([=](std::string localVarResponse) {
            std::shared_ptr<V1Pod> localVarResult = std::make_shared<V1Pod>();
            nlohmann::json localVarJson = nlohmann::json::parse(localVarResponse);
            (void)ModelUtils::FromJson(localVarJson, localVarResult);
            return localVarResult;
        });
}

litebus::Future<std::shared_ptr<V1Node>> CoreV1Api::PatchNode(const std::string &name,
                                                              const std::shared_ptr<Object> &body) const
{
    std::string localVarPath = "/api/v1/nodes/" + name;
    nlohmann::json localVarJson = ModelUtils::ToJson(body);
    std::shared_ptr<IHttpBody> localVarHttpBody = std::make_shared<JsonBody>(localVarJson);
    ASSERT_IF_NULL(m_apiClient);
    return m_apiClient->CallApi(localVarPath, std::string("PATCH"), {}, localVarHttpBody)
        .Then([=](std::string localVarResponse) {
            std::shared_ptr<V1Node> localVarResult = std::make_shared<V1Node>();
            nlohmann::json localVarJson = nlohmann::json::parse(localVarResponse);
            (void)ModelUtils::FromJson(localVarJson, localVarResult);
            return localVarResult;
        });
}

litebus::Future<std::string> CoreV1Api::ListForMonitor() const
{
    std::string localVarPath = std::string("/api/v1/nodes");
    ASSERT_IF_NULL(m_apiClient);
    return m_apiClient->CallApi(localVarPath, std::string("GET"), {}, nullptr);
}

}  // namespace api
}  // namespace functionsystem::kube_client
