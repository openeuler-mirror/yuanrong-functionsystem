/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
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

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <string>

#include "common/proto/pb/posix_pb.h"
#include "grpc_server/frontend_proxy_service/frontend_proxy_service.h"

namespace functionsystem::test {
using namespace local_scheduler;

TEST(FrontendProxyServiceTest, InvokeDoesNotUseFrontendClientIDAsRuntimeSender)
{
    std::string capturedCaller;
    SharedStreamMsg capturedRequest;

    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-a";
    param.invokeResultTimeoutMs = 1;
    param.invokeDispatcher = [&capturedCaller, &capturedRequest](const std::string &caller,
                                                                 const SharedStreamMsg &request) {
        capturedCaller = caller;
        capturedRequest = request;
        auto response = std::make_shared<runtime_rpc::StreamingMessage>();
        response->mutable_invokersp()->set_code(common::ERR_NONE);
        return litebus::Future<SharedStreamMsg>(response);
    };
    FrontendProxyService service(std::move(param));

    ::frontend_proxy::InvokeInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-process");
    request.mutable_context()->set_requestid("frontend-proxy-request-1");
    request.mutable_context()->set_traceid("trace-1");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_invoke()->set_instanceid("faas-instance-1");
    request.mutable_invoke()->set_requestid("frontend-proxy-request-1");
    request.mutable_invoke()->set_traceid("trace-1");
    request.mutable_invoke()->set_function("0/tenant/faas/function");

    ::frontend_proxy::InvokeInstanceResponse response;
    auto status = service.InvokeInstance(nullptr, &request, &response);
    EXPECT_TRUE(status.ok());

    ASSERT_NE(capturedRequest, nullptr);
    ASSERT_TRUE(capturedRequest->has_invokereq());
    EXPECT_TRUE(capturedCaller.empty());
    EXPECT_EQ(capturedRequest->invokereq().requestid(), "frontend-proxy-request-1");
}

TEST(FrontendProxyServiceTest, DuplicateRequestIDDoesNotReplaceExistingWaiter)
{
    std::mutex mutex;
    std::condition_variable entered;
    bool firstInvokeDispatched = false;
    bool releaseFirstInvoke = false;

    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-a";
    param.invokeResultTimeoutMs = 20;
    param.invokeDispatcher = [&mutex, &entered, &firstInvokeDispatched,
                              &releaseFirstInvoke](const std::string &, const SharedStreamMsg &) {
        std::unique_lock<std::mutex> lock(mutex);
        firstInvokeDispatched = true;
        entered.notify_one();
        entered.wait(lock, [&releaseFirstInvoke] { return releaseFirstInvoke; });
        auto response = std::make_shared<runtime_rpc::StreamingMessage>();
        response->mutable_invokersp()->set_code(common::ERR_NONE);
        return litebus::Future<SharedStreamMsg>(response);
    };
    FrontendProxyService service(std::move(param));

    auto makeRequest = [](const std::string &frontendClientID) {
        ::frontend_proxy::InvokeInstanceRequest request;
        request.mutable_context()->set_frontendclientid(frontendClientID);
        request.mutable_context()->set_requestid("duplicate-request-id");
        request.mutable_invoke()->set_instanceid("faas-instance-1");
        request.mutable_invoke()->set_requestid("duplicate-request-id");
        return request;
    };

    auto firstRequest = makeRequest("frontend-process-a");
    ::frontend_proxy::InvokeInstanceResponse firstResponse;
    auto firstCall = std::async(std::launch::async, [&service, &firstRequest, &firstResponse] {
        return service.InvokeInstance(nullptr, &firstRequest, &firstResponse);
    });
    bool firstInvokeEntered = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        firstInvokeEntered = entered.wait_for(lock, std::chrono::seconds(1),
                                              [&firstInvokeDispatched] { return firstInvokeDispatched; });
    }
    EXPECT_TRUE(firstInvokeEntered);
    if (!firstInvokeEntered) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            releaseFirstInvoke = true;
        }
        entered.notify_one();
        (void)firstCall.get();
        return;
    }

    auto duplicateRequest = makeRequest("frontend-process-b");
    ::frontend_proxy::InvokeInstanceResponse duplicateResponse;
    auto duplicateStatus = service.InvokeInstance(nullptr, &duplicateRequest, &duplicateResponse);
    EXPECT_TRUE(duplicateStatus.ok());
    EXPECT_EQ(duplicateResponse.status().code(), common::ERR_PARAM_INVALID);
    EXPECT_EQ(duplicateResponse.status().message(), "frontend proxy invoke requires a globally unique request id");

    {
        std::lock_guard<std::mutex> lock(mutex);
        releaseFirstInvoke = true;
    }
    entered.notify_one();
    EXPECT_TRUE(firstCall.get().ok());
}

}  // namespace functionsystem::test
