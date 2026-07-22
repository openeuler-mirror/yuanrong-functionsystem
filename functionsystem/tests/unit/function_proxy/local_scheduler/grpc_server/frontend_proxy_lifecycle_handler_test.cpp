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

#include <memory>

#include "common/proto/pb/posix_pb.h"
#include "grpc_server/frontend_proxy_service/frontend_proxy_lifecycle_handler.h"

namespace functionsystem::test {
using namespace local_scheduler;

TEST(FrontendProxyLifecycleHandlerTest, CreateUsesFrontendSystemCallerWithoutRuntimeParent)
{
    std::shared_ptr<messages::ScheduleRequest> capturedScheduleReq;
    auto dispatcher = BuildFrontendProxyCreateReadyDispatcher(
        [&capturedScheduleReq](const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
                               const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &,
                               FrontendProxyReadyCallback callback) {
            capturedScheduleReq = scheduleReq;
            auto readyResult = std::make_shared<functionsystem::CallResult>();
            readyResult->set_requestid("runtime-internal-mismatched-request");
            readyResult->set_instanceid("frontend-create-instance");
            readyResult->set_code(common::ERR_NONE);
            readyResult->mutable_runtimeinfo()->set_route("grpc://owning-proxy");
            readyResult->mutable_runtimeinfo()->set_proxyid("owning-node");
            // Deliberately deliver ready before Schedule completes. The
            // pre-registered ticket must retain it exactly once.
            (void)callback(readyResult);
            messages::ScheduleResponse scheduleResponse;
            scheduleResponse.set_code(common::ERR_NONE);
            scheduleResponse.set_requestid(scheduleReq->requestid());
            scheduleResponse.set_instanceid("frontend-create-instance");
            return litebus::Future<messages::ScheduleResponse>(scheduleResponse);
        });

    ::frontend_proxy::CreateInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-client-1");
    request.mutable_context()->set_requestid("request-1");
    request.mutable_context()->set_traceid("trace-1");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_create()->set_function("0/tenant/faas/fn");
    request.mutable_create()->set_requestid("request-1");
    (*request.mutable_create()->mutable_createoptions())["source"] = "frontend";
    (*request.mutable_create()->mutable_createoptions())["tenantID"] = "tenant-a";

    auto response = dispatcher(request).Get(1000);
    ASSERT_TRUE(response.IsSome());
    ASSERT_NE(capturedScheduleReq, nullptr);

    // Frontend unary create is issued by a trusted system component. It must not
    // encode frontendClientID as the old runtime parent/sender identity, because
    // InstanceCtrl authorization interprets non-empty parentID as a runtime
    // instance and rejects it when that instance is not in the local view.
    EXPECT_TRUE(capturedScheduleReq->instance().parentid().empty());
    runtime::CallRequest initCall;
    ASSERT_TRUE(initCall.ParseFromString(capturedScheduleReq->initrequest()));
    EXPECT_TRUE(initCall.senderid().empty());
    auto sourceIter = capturedScheduleReq->instance().createoptions().find("source");
    ASSERT_NE(sourceIter, capturedScheduleReq->instance().createoptions().end());
    EXPECT_EQ(sourceIter->second, "frontend");
    auto extensionIter = capturedScheduleReq->instance().extensions().find("source");
    ASSERT_NE(extensionIter, capturedScheduleReq->instance().extensions().end());
    EXPECT_EQ(extensionIter->second, "frontend");

    ASSERT_TRUE(response.Get().has_callresult());
    EXPECT_EQ(response.Get().callresult().requestid(), "request-1");
    EXPECT_EQ(response.Get().callresult().instanceid(), "frontend-create-instance");
    ASSERT_TRUE(response.Get().callresult().has_runtimeinfo());
    EXPECT_EQ(response.Get().callresult().runtimeinfo().route(), "grpc://owning-proxy");
    EXPECT_EQ(response.Get().callresult().runtimeinfo().proxyid(), "owning-node");
}

TEST(FrontendProxyLifecycleHandlerTest, CreateReadyWaitTimesOutWhenRuntimeNeverReportsReady)
{
    bool unregistered = false;
    auto dispatcher = BuildFrontendProxyCreateReadyDispatcher(
        [](const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
           const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &,
           FrontendProxyReadyCallback) {
            messages::ScheduleResponse response;
            response.set_code(common::ERR_NONE);
            response.set_requestid(scheduleReq->requestid());
            response.set_instanceid("frontend-create-timeout-instance");
            return litebus::Future<messages::ScheduleResponse>(response);
        },
        [&unregistered](const std::string &requestID, const std::string &reason) {
            EXPECT_EQ(requestID, "request-timeout");
            EXPECT_EQ(reason, "frontend create ready call result timed out");
            unregistered = true;
        },
        1);

    ::frontend_proxy::CreateInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-client-1");
    request.mutable_context()->set_requestid("request-timeout");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_create()->set_function("0/tenant/faas/fn");
    request.mutable_create()->set_requestid("request-timeout");
    (*request.mutable_create()->mutable_createoptions())["source"] = "frontend";

    auto response = dispatcher(request).Get(1000);

    ASSERT_TRUE(response.IsSome());
    EXPECT_EQ(response.Get().create().code(), common::ERR_INNER_SYSTEM_ERROR);
    EXPECT_EQ(response.Get().create().message(), "frontend proxy create ready call result timed out");
    EXPECT_TRUE(unregistered);
}

}  // namespace functionsystem::test
