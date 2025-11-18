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

#include "coordination_v1_api.h"

#include "common/kube_client/constants.h"
#include "common/logs/logging.h"
#include "common/kube_client/model/common/ihttp_body.h"
#include "common/kube_client/model/common/json_body.h"
#include "common/status/status.h"

namespace functionsystem::kube_client {
namespace api {

using namespace functionsystem::kube_client::model;

const std::string LEASE_PATH = "/apis/coordination.k8s.io/v1/namespaces/";

litebus::Future<std::shared_ptr<V1Lease>> CoordinationV1Api::CreateNamespacedLease(
    const std::string &rNamespace, const std::shared_ptr<V1Lease> &body) const
{
    if (body == nullptr) {
        YRLOG_WARN("Missing required parameter 'body' when calling CoordinationV1Api->CreateNamespacedLease");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Lease>>>();
        promise->SetFailed(litebus::Status::KERROR);
        return promise->GetFuture();
    }
    std::string localVarPath = LEASE_PATH + rNamespace + "/leases";
    return CallNamespacedLeaseApi(body, localVarPath, "POST");
}

litebus::Future<std::shared_ptr<V1Lease>> CoordinationV1Api::ReadNamespacedLease(const std::string &name,
                                                                                 const std::string &rNamespace) const
{
    std::string localVarPath = LEASE_PATH + rNamespace + "/leases/" + name;
    return CallNamespacedLeaseApi(nullptr, localVarPath, "GET");
}

litebus::Future<std::shared_ptr<V1Lease>> CoordinationV1Api::ReplaceNamespacedLease(
    const std::string &name, const std::string &rNamespace, const std::shared_ptr<V1Lease> &body) const
{
    if (body == nullptr) {
        YRLOG_WARN("Missing required parameter 'body' when calling CoordinationV1Api->ReplaceNamespacedLease");
        auto promise = std::make_shared<litebus::Promise<std::shared_ptr<V1Lease>>>();
        promise->SetFailed(litebus::Status::KERROR);
        return promise->GetFuture();
    }
    std::string localVarPath = LEASE_PATH + rNamespace + +"/leases/" + name;
    return CallNamespacedLeaseApi(body, localVarPath, "PUT");
}

litebus::Future<std::shared_ptr<V1Lease>> CoordinationV1Api::CallNamespacedLeaseApi(
    const std::shared_ptr<V1Lease> &body, const std::string &url, const std::string &method) const
{
    std::shared_ptr<IHttpBody> localVarHttpBody = nullptr;
    if (body != nullptr) {
        nlohmann::json localVarJson = ModelUtils::ToJson(body);
        localVarHttpBody = std::make_shared<JsonBody>(localVarJson);
    }
    ASSERT_IF_NULL(m_apiClient);
    return m_apiClient->CallApi(url, method, {}, localVarHttpBody).Then([=](std::string localVarResponse) {
        std::shared_ptr<V1Lease> localVarResult = std::make_shared<V1Lease>();
        nlohmann::json localVarJson = nlohmann::json::parse(localVarResponse);
        (void)ModelUtils::FromJson(localVarJson, localVarResult);
        return localVarResult;
    });
}
}  // namespace api
}  // namespace functionsystem::kube_client