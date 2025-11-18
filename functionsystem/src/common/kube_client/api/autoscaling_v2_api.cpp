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

#include "autoscaling_v2_api.h"

#include "common/kube_client/constants.h"
#include "common/logs/logging.h"
#include "common/kube_client/model/common/ihttp_body.h"
#include "common/kube_client/model/common/json_body.h"
#include "common/status/status.h"

namespace functionsystem::kube_client {
namespace api {

using namespace functionsystem::kube_client::model;

AutoscalingV2Api::AutoscalingV2Api(std::shared_ptr<const ApiClient> apiClient) : m_apiClient(apiClient)
{}

AutoscalingV2Api::~AutoscalingV2Api()
{}

litebus::Future<std::shared_ptr<V2HorizontalPodAutoscaler>> AutoscalingV2Api::CreateNamespacedHorizontalPodAutoscaler(
    const std::string &rNamespace,
    const std::shared_ptr<V2HorizontalPodAutoscaler> &body) const
{
    litebus::Promise<std::shared_ptr<V2HorizontalPodAutoscaler>> promise;

    // verify the required parameter 'body' is set
    if (body == nullptr) {
        YRLOG_WARN("Missing required parameter 'body' when calling AppsV1Api->CreateNamespacedDeployment");
        promise.SetFailed(litebus::Status::KERROR);
        return promise.GetFuture();
    }

    std::string localVarPath = "/apis/autoscaling/v2beta2/namespaces/" + rNamespace + "/horizontalpodautoscalers";
    nlohmann::json localVarJson = ModelUtils::ToJson(body);
    std::shared_ptr<IHttpBody> localVarHttpBody = std::make_shared<JsonBody>(localVarJson);

    ASSERT_IF_NULL(m_apiClient);
    m_apiClient->CallApi(localVarPath, std::string("POST"), {}, localVarHttpBody)
        .OnComplete([=](litebus::Future<std::string> localVarResponse) {
            if (localVarResponse.IsError()) {
                promise.SetFailed(litebus::Status::KERROR);
                return;
            }

            std::shared_ptr<V2HorizontalPodAutoscaler> localVarResult = std::make_shared<V2HorizontalPodAutoscaler>();
            (void)ModelUtils::FromJson(nlohmann::json::parse(localVarResponse.Get()), localVarResult);
            promise.SetValue(localVarResult);
            return;
        });
    return promise.GetFuture();
}

litebus::Future<std::shared_ptr<V1Status>> AutoscalingV2Api::DeleteNamespacedHorizontalPodAutoscaler(
    const std::string &name,
    const std::string &rNamespace) const
{
    litebus::Promise<std::shared_ptr<V1Status>> promise;
    std::string localVarPath = "/apis/autoscaling/v2beta2/namespaces/" +
        rNamespace + "/horizontalpodautoscalers/" + name;
    std::map<std::string, std::string> localVarQueryParams;
    ASSERT_IF_NULL(m_apiClient);
    m_apiClient->CallApi(localVarPath, std::string("DELETE"), {}, nullptr)
        .OnComplete([=](litebus::Future<std::string> localVarResponse) {
            if (localVarResponse.IsError()) {
                promise.SetFailed(litebus::Status::KERROR);
                return;
            }

            std::shared_ptr<V1Status> localVarResult = std::make_shared<V1Status>();
            (void)ModelUtils::FromJson(nlohmann::json::parse(localVarResponse.Get()), localVarResult);
            promise.SetValue(localVarResult);
            return;
        });
    return promise.GetFuture();
}

litebus::Future<std::shared_ptr<V2HorizontalPodAutoscalerList>> AutoscalingV2Api::ListNamespacedHorizontalPodAutoscaler(
    const std::string &rNamespace,
    const litebus::Option<std::string> &rContinue,
    const litebus::Option<uint32_t> &limit,
    const litebus::Option<bool> &watch) const
{
    litebus::Promise<std::shared_ptr<V2HorizontalPodAutoscalerList>> promise;
    std::string localVarPath = "/apis/autoscaling/v2beta2/namespaces/" + rNamespace + "/horizontalpodautoscalers";
    std::map<std::string, std::string> localVarQueryParams = ApiClient::GetLocalQueryParams(rContinue, limit, watch);
    ASSERT_IF_NULL(m_apiClient);
    m_apiClient->CallApi(localVarPath, std::string("GET"), localVarQueryParams, nullptr)
        .OnComplete([=](litebus::Future<std::string> localVarResponse) {
            if (localVarResponse.IsError()) {
                promise.SetFailed(litebus::Status::KERROR);
                return;
            }

            auto localVarResult = std::make_shared<V2HorizontalPodAutoscalerList>();
            if (localVarResponse.Get().empty()) {
                YRLOG_WARN("error calling ListNamespacedHorizontalPodAutoscaler: localVarResponse empty");
                if (watch.IsSome() && !watch.Get()) {
                    auto watchStr = ApiClient::ParameterToString(watch.Get());
                    (void)ListNamespacedHorizontalPodAutoscaler(rNamespace, rContinue, limit, watch);
                    YRLOG_WARN("error calling ListNamespacedHorizontalPodAutoscaler: watchStr: {}", watchStr);
                }
                promise.SetFailed(litebus::Status::KERROR);
                return;
            }
            nlohmann::json localVarJson = nlohmann::json::parse(localVarResponse.Get());
            (void)ModelUtils::FromJson(localVarJson, localVarResult);
            promise.SetValue(localVarResult);
            return;
        });
    return promise.GetFuture();
}

litebus::Future<std::shared_ptr<V2HorizontalPodAutoscaler>> AutoscalingV2Api::PatchNamespacedHorizontalPodAutoscaler(
    const std::string &name,
    const std::string &rNamespace,
    const std::shared_ptr<Object> &body) const
{
    litebus::Promise<std::shared_ptr<V2HorizontalPodAutoscaler>> promise;
    std::string localVarPath = "/apis/autoscaling/v2beta2/namespaces/" +
        rNamespace + "/horizontalpodautoscalers/" + name;
    nlohmann::json localVarJson = ModelUtils::ToJson(body);
    std::shared_ptr<IHttpBody> localVarHttpBody = std::make_shared<JsonBody>(localVarJson);
    ASSERT_IF_NULL(m_apiClient);
    m_apiClient->CallApi(localVarPath, std::string("PATCH"), {}, localVarHttpBody)
        .OnComplete([=](litebus::Future<std::string> localVarResponse) {
            if (localVarResponse.IsError()) {
                promise.SetFailed(litebus::Status::KERROR);
                return;
            }

            std::shared_ptr<V2HorizontalPodAutoscaler> localVarResult = std::make_shared<V2HorizontalPodAutoscaler>();
            nlohmann::json localVarJson = nlohmann::json::parse(localVarResponse.Get());
            (void)ModelUtils::FromJson(localVarJson, localVarResult);
            promise.SetValue(localVarResult);
            return;
        });
    return promise.GetFuture();
}
}  // namespace api
}  // namespace functionsystem::kube_client