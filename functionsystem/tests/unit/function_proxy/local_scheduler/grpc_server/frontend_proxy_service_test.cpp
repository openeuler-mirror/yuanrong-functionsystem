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
#include <thread>

#include <unistd.h>

#include <grpcpp/create_channel.h>
#include <grpcpp/server_builder.h>

#include "common/proto/pb/posix_pb.h"
#include "function_proxy/busproxy/invocation_handler/invocation_handler.h"
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

TEST(FrontendProxyServiceTest, InvokeTerminalSuccessReturnsDirectResult)
{
    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-a";
    param.endpointAddress = "10.0.0.11:19090";
    param.invokeResultTimeoutMs = 100;
    param.invokeDispatcher = [](const std::string &, const SharedStreamMsg &request) {
        auto result = std::make_shared<runtime_rpc::StreamingMessage>();
        result->mutable_callresultreq()->set_code(common::ERR_NONE);
        result->mutable_callresultreq()->set_requestid(request->invokereq().requestid());
        result->mutable_callresultreq()->set_instanceid(request->invokereq().instanceid());
        (void)InvocationHandler::CallResultAdapter("runtime-instance", result);
        auto response = std::make_shared<runtime_rpc::StreamingMessage>();
        response->mutable_invokersp()->set_code(common::ERR_NONE);
        return litebus::Future<SharedStreamMsg>(response);
    };
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::InvokeInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-a");
    request.mutable_context()->set_requestid("invoke-success");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_invoke()->set_instanceid("instance-a");
    ::frontend_proxy::InvokeInstanceResponse response;

    EXPECT_TRUE(service.InvokeInstance(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_NONE);
    EXPECT_EQ(response.callresult().requestid(), "invoke-success");
    EXPECT_EQ(response.callresult().instanceid(), "instance-a");
}

TEST(FrontendProxyServiceTest, CreateReadyTerminalSuccessCarriesOwningRoute)
{
    EXPECT_STREQ(FrontendProxyService::LIFECYCLE_TRANSPORT, "raw-unary");
    EXPECT_STREQ(FrontendProxyService::READY_OPERATION, "ready");
    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-a";
    param.endpointAddress = "10.0.0.11:19090";
    param.enableCreateDispatch = true;
    param.createReadyDispatcher = [](const ::frontend_proxy::CreateInstanceRequest &) {
        ::frontend_proxy::CreateInstanceResponse response;
        response.mutable_create()->set_code(common::ERR_NONE);
        response.mutable_create()->set_instanceid("created-instance");
        response.set_routeaddress("10.0.0.11:19090");
        response.mutable_callresult()->mutable_runtimeinfo()->set_proxyid("proxy-node-a");
        return litebus::Future<::frontend_proxy::CreateInstanceResponse>(response);
    };
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::CreateInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-a");
    request.mutable_context()->set_requestid("create-ready-success");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_create()->set_function("0/tenant-a/faas/function");
    (*request.mutable_create()->mutable_createoptions())["source"] = "frontend";
    ::frontend_proxy::CreateInstanceResponse response;

    EXPECT_TRUE(service.CreateInstance(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_NONE);
    EXPECT_EQ(response.create().instanceid(), "created-instance");
    EXPECT_EQ(response.routeaddress(), "10.0.0.11:19090");
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
        request.mutable_context()->set_tenantid("tenant-a");
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

TEST(FrontendProxyServiceTest, CreateTimeoutUnregistersReadyWaitWithoutCancellingInstance)
{
    auto pending = std::make_shared<litebus::Promise<::frontend_proxy::CreateInstanceResponse>>();
    std::string cancelledRequestID;
    std::string cancelReason;
    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-a";
    param.enableCreateDispatch = true;
    param.invokeResultTimeoutMs = 1;
    param.createReadyDispatcher = [pending](const ::frontend_proxy::CreateInstanceRequest &) {
        return pending->GetFuture();
    };
    param.createWaitCanceller = [&cancelledRequestID, &cancelReason](const std::string &requestID,
                                                                    const std::string &reason) {
        cancelledRequestID = requestID;
        cancelReason = reason;
    };
    FrontendProxyService service(std::move(param));

    ::frontend_proxy::CreateInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-process-a");
    request.mutable_context()->set_requestid("create-timeout-request");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_create()->set_function("0/tenant-a/faas/function");
    (*request.mutable_create()->mutable_createoptions())["source"] = "frontend";
    ::frontend_proxy::CreateInstanceResponse response;

    EXPECT_TRUE(service.CreateInstance(nullptr, &request, &response).ok());
    EXPECT_EQ(cancelledRequestID, "create-timeout-request");
    EXPECT_EQ(cancelReason, "frontend create timed out");
    EXPECT_EQ(response.status().retryreason(), "post-dispatch-unknown");
    EXPECT_FALSE(response.status().retryable());
}

TEST(FrontendProxyServiceTest, CreateRejectsTenantFieldMismatch)
{
    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-a";
    param.enableCreateDispatch = true;
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::CreateInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-process-a");
    request.mutable_context()->set_requestid("tenant-mismatch-request");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_create()->set_function("0/tenant-b/faas/function");
    (*request.mutable_create()->mutable_createoptions())["source"] = "frontend";
    (*request.mutable_create()->mutable_createoptions())["tenantID"] = "tenant-b";
    ::frontend_proxy::CreateInstanceResponse response;

    EXPECT_TRUE(service.CreateInstance(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_AUTHORIZE_FAILED);
}

TEST(FrontendProxyServiceTest, RejectsOperationRequestIDMismatchBeforeDispatch)
{
    int invokeDispatches = 0;
    int createDispatches = 0;
    int killDispatches = 0;
    FrontendProxyServiceParam param;
    param.enableCreateDispatch = true;
    param.enableKillDispatch = true;
    param.invokeDispatcher = [&](const std::string &, const SharedStreamMsg &) {
        ++invokeDispatches;
        return litebus::Future<SharedStreamMsg>();
    };
    param.createReadyDispatcher = [&](const ::frontend_proxy::CreateInstanceRequest &) {
        ++createDispatches;
        return litebus::Future<::frontend_proxy::CreateInstanceResponse>();
    };
    param.killReadyDispatcher = [&](const ::frontend_proxy::KillInstanceRequest &) {
        ++killDispatches;
        return litebus::Future<::frontend_proxy::KillInstanceResponse>();
    };
    FrontendProxyService service(std::move(param));

    ::frontend_proxy::InvokeInstanceRequest invoke;
    invoke.mutable_context()->set_frontendclientid("frontend-a");
    invoke.mutable_context()->set_requestid("context-request");
    invoke.mutable_context()->set_tenantid("tenant-a");
    invoke.mutable_invoke()->set_instanceid("instance-a");
    invoke.mutable_invoke()->set_requestid("operation-request");
    ::frontend_proxy::InvokeInstanceResponse invokeResponse;
    EXPECT_TRUE(service.InvokeInstance(nullptr, &invoke, &invokeResponse).ok());
    EXPECT_EQ(invokeResponse.status().code(), common::ERR_PARAM_INVALID);

    ::frontend_proxy::CreateInstanceRequest create;
    create.mutable_context()->set_frontendclientid("frontend-a");
    create.mutable_context()->set_tenantid("tenant-a");
    create.mutable_context()->set_requestid("context-request");
    create.mutable_create()->set_function("0/tenant-a/faas/function");
    create.mutable_create()->set_requestid("operation-request");
    (*create.mutable_create()->mutable_createoptions())["source"] = "frontend";
    ::frontend_proxy::CreateInstanceResponse createResponse;
    EXPECT_TRUE(service.CreateInstance(nullptr, &create, &createResponse).ok());
    EXPECT_EQ(createResponse.status().code(), common::ERR_PARAM_INVALID);

    ::frontend_proxy::KillInstanceRequest kill;
    kill.mutable_context()->set_frontendclientid("frontend-a");
    kill.mutable_context()->set_tenantid("tenant-a");
    kill.mutable_context()->set_requestid("context-request");
    kill.mutable_kill()->set_instanceid("instance-a");
    kill.mutable_kill()->set_requestid("operation-request");
    ::frontend_proxy::KillInstanceResponse killResponse;
    EXPECT_TRUE(service.KillInstance(nullptr, &kill, &killResponse).ok());
    EXPECT_EQ(killResponse.status().code(), common::ERR_PARAM_INVALID);

    EXPECT_EQ(invokeDispatches, 0);
    EXPECT_EQ(createDispatches, 0);
    EXPECT_EQ(killDispatches, 0);
}

TEST(FrontendProxyServiceTest, InvokeTimeoutAfterDispatchIsNonRetryableUnknown)
{
    auto pending = std::make_shared<litebus::Promise<SharedStreamMsg>>();
    FrontendProxyServiceParam param;
    param.invokeResultTimeoutMs = 1;
    param.invokeDispatcher = [pending](const std::string &, const SharedStreamMsg &) {
        return pending->GetFuture();
    };
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::InvokeInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-a");
    request.mutable_context()->set_requestid("invoke-timeout");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_invoke()->set_instanceid("instance-a");
    ::frontend_proxy::InvokeInstanceResponse response;

    EXPECT_TRUE(service.InvokeInstance(nullptr, &request, &response).ok());
    EXPECT_FALSE(response.status().retryable());
    EXPECT_EQ(response.status().retryreason(), "post-dispatch-unknown");
}

TEST(FrontendProxyServiceTest, KillLocalMissIsTypedAsRetryableStaleRoute)
{
    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-a";
    param.enableKillDispatch = true;
    param.killReadyDispatcher = [](const ::frontend_proxy::KillInstanceRequest &) {
        ::frontend_proxy::KillInstanceResponse response;
        response.mutable_kill()->set_code(common::ERR_INSTANCE_NOT_FOUND);
        response.mutable_kill()->set_message("frontend proxy is not the owning proxy for this instance");
        return litebus::Future<::frontend_proxy::KillInstanceResponse>(response);
    };
    FrontendProxyService service(std::move(param));

    ::frontend_proxy::KillInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-process-a");
    request.mutable_context()->set_requestid("kill-stale-route-request");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_kill()->set_instanceid("instance-on-another-proxy");
    ::frontend_proxy::KillInstanceResponse response;

    EXPECT_TRUE(service.KillInstance(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_INSTANCE_NOT_FOUND);
    EXPECT_TRUE(response.status().retryable());
    EXPECT_EQ(response.status().retryreason(), "route-stale");
}

TEST(FrontendProxyServiceTest, InvokeRequiresTenantAndRejectsConflictingTenantLabelBeforeDispatch)
{
    int dispatches = 0;
    FrontendProxyServiceParam param;
    param.invokeDispatcher = [&dispatches](const std::string &, const SharedStreamMsg &) {
        ++dispatches;
        return litebus::Future<SharedStreamMsg>();
    };
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::InvokeInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-a");
    request.mutable_context()->set_requestid("invoke-tenant");
    request.mutable_invoke()->set_instanceid("instance-a");
    ::frontend_proxy::InvokeInstanceResponse response;

    EXPECT_TRUE(service.InvokeInstance(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_PARAM_INVALID);
    request.mutable_context()->set_tenantid("tenant-a");
    (*request.mutable_context()->mutable_labels())["tenantID"] = "tenant-b";
    response.Clear();
    EXPECT_TRUE(service.InvokeInstance(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_AUTHORIZE_FAILED);
    EXPECT_EQ(dispatches, 0);
}

TEST(FrontendProxyServiceTest, KillUnknownOwnerNotFoundFailsFastWithoutClaimingDeletion)
{
    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-a";
    param.enableKillDispatch = true;
    param.killReadyDispatcher = [](const ::frontend_proxy::KillInstanceRequest &) {
        ::frontend_proxy::KillInstanceResponse response;
        response.mutable_kill()->set_code(common::ERR_INSTANCE_NOT_FOUND);
        response.mutable_kill()->set_message("owner could not authoritatively resolve instance");
        return litebus::Future<::frontend_proxy::KillInstanceResponse>(response);
    };
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::KillInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-a");
    request.mutable_context()->set_requestid("kill-owner-unknown");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_kill()->set_instanceid("instance-a");
    ::frontend_proxy::KillInstanceResponse response;

    EXPECT_TRUE(service.KillInstance(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_INSTANCE_NOT_FOUND);
    EXPECT_FALSE(response.status().retryable());
    EXPECT_EQ(response.status().retryreason(), "owner-unknown");
}

TEST(FrontendProxyServiceTest, KillAuthoritativeDeletedIsOnlyAcceptedAsExplicitSuccess)
{
    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-a";
    param.enableKillDispatch = true;
    param.killReadyDispatcher = [](const ::frontend_proxy::KillInstanceRequest &) {
        ::frontend_proxy::KillInstanceResponse response;
        response.mutable_kill()->set_code(common::ERR_NONE);
        response.mutable_kill()->set_message("authoritative owner confirmed instance already deleted");
        return litebus::Future<::frontend_proxy::KillInstanceResponse>(response);
    };
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::KillInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-a");
    request.mutable_context()->set_requestid("kill-authoritative-deleted");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_kill()->set_instanceid("instance-a");
    ::frontend_proxy::KillInstanceResponse response;

    EXPECT_TRUE(service.KillInstance(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_NONE);
    EXPECT_FALSE(response.status().retryable());
    EXPECT_TRUE(response.status().retryreason().empty());
}

TEST(FrontendProxyServiceTest, RealGrpcCancellationRemovesCreateReadyWaiter)
{
    auto pending = std::make_shared<litebus::Promise<::frontend_proxy::CreateInstanceResponse>>();
    std::mutex mutex;
    std::condition_variable entered;
    bool dispatched = false;
    std::string cancelledRequestID;
    std::string cancelReason;

    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-a";
    param.enableCreateDispatch = true;
    param.invokeResultTimeoutMs = 5000;
    param.createReadyDispatcher = [&](const ::frontend_proxy::CreateInstanceRequest &) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            dispatched = true;
        }
        entered.notify_one();
        return pending->GetFuture();
    };
    param.createWaitCanceller = [&](const std::string &requestID, const std::string &reason) {
        std::lock_guard<std::mutex> lock(mutex);
        cancelledRequestID = requestID;
        cancelReason = reason;
        entered.notify_one();
    };
    FrontendProxyService service(std::move(param));

    ::grpc::ServerBuilder builder;
    const auto socketPath = "/tmp/frontend-proxy-create-cancel-" + std::to_string(::getpid()) + ".sock";
    const auto address = "unix:" + socketPath;
    (void)::unlink(socketPath.c_str());
    builder.AddListeningPort(address, ::grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    ASSERT_NE(server, nullptr);
    auto channel = ::grpc::CreateChannel(address, ::grpc::InsecureChannelCredentials());
    ASSERT_TRUE(channel->WaitForConnected(std::chrono::system_clock::now() + std::chrono::seconds(2)));
    auto stub = ::frontend_proxy::FrontendProxyService::NewStub(channel);
    ::frontend_proxy::CreateInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-process-a");
    request.mutable_context()->set_requestid("create-cancel-request");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_create()->set_function("0/tenant-a/faas/function");
    (*request.mutable_create()->mutable_createoptions())["source"] = "frontend";
    ::frontend_proxy::CreateInstanceResponse response;
    ::grpc::ClientContext clientContext;

    auto call = std::async(std::launch::async, [&] {
        return stub->CreateInstance(&clientContext, request, &response);
    });
    bool enteredServer = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        enteredServer = entered.wait_for(lock, std::chrono::seconds(2), [&] { return dispatched; });
    }
    EXPECT_TRUE(enteredServer);
    if (!enteredServer) {
        clientContext.TryCancel();
        (void)call.get();
        server->Shutdown();
        return;
    }
    clientContext.TryCancel();
    auto status = call.get();
    EXPECT_EQ(status.error_code(), ::grpc::StatusCode::CANCELLED);
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(entered.wait_for(lock, std::chrono::seconds(2), [&] { return !cancelReason.empty(); }));
        EXPECT_EQ(cancelledRequestID, "create-cancel-request");
        EXPECT_EQ(cancelReason, "grpc client cancelled");
    }
    server->Shutdown();
}

TEST(FrontendProxyServiceTest, RealGrpcCancellationStopsInvokeWaitAndClearsRegistry)
{
    auto pending = std::make_shared<litebus::Promise<SharedStreamMsg>>();
    std::mutex mutex;
    std::condition_variable entered;
    int dispatchCount = 0;
    FrontendProxyServiceParam param;
    param.invokeResultTimeoutMs = 5000;
    param.invokeDispatcher = [&](const std::string &, const SharedStreamMsg &) {
        std::lock_guard<std::mutex> lock(mutex);
        ++dispatchCount;
        entered.notify_one();
        if (dispatchCount == 1) {
            return pending->GetFuture();
        }
        auto response = std::make_shared<runtime_rpc::StreamingMessage>();
        response->mutable_invokersp()->set_code(common::ERR_PARAM_INVALID);
        response->mutable_invokersp()->set_message("second dispatch proves registry cleanup");
        return litebus::Future<SharedStreamMsg>(response);
    };
    FrontendProxyService service(std::move(param));
    ::grpc::ServerBuilder builder;
    const auto socketPath = "/tmp/frontend-proxy-invoke-cancel-" + std::to_string(::getpid()) + ".sock";
    const auto address = "unix:" + socketPath;
    (void)::unlink(socketPath.c_str());
    builder.AddListeningPort(address, ::grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    ASSERT_NE(server, nullptr);

    auto channel = ::grpc::CreateChannel(address, ::grpc::InsecureChannelCredentials());
    ASSERT_TRUE(channel->WaitForConnected(std::chrono::system_clock::now() + std::chrono::seconds(2)));
    auto stub = ::frontend_proxy::FrontendProxyService::NewStub(channel);
    ::frontend_proxy::InvokeInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-a");
    request.mutable_context()->set_requestid("invoke-cancel-request");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_invoke()->set_instanceid("instance-a");
    ::frontend_proxy::InvokeInstanceResponse response;
    ::grpc::ClientContext clientContext;
    auto call = std::async(std::launch::async, [&] {
        return stub->InvokeInstance(&clientContext, request, &response);
    });
    bool dispatched = false;
    {
        std::unique_lock<std::mutex> lock(mutex);
        dispatched = entered.wait_for(lock, std::chrono::seconds(2), [&] { return dispatchCount == 1; });
    }
    EXPECT_TRUE(dispatched);
    clientContext.TryCancel();
    EXPECT_EQ(call.get().error_code(), ::grpc::StatusCode::CANCELLED);

    ::frontend_proxy::InvokeInstanceResponse retryResponse;
    const auto cleanupDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    do {
        retryResponse.Clear();
        EXPECT_TRUE(service.InvokeInstance(nullptr, &request, &retryResponse).ok());
        if (dispatchCount == 2) {
            break;
        }
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < cleanupDeadline);
    EXPECT_EQ(dispatchCount, 2);
    EXPECT_EQ(retryResponse.status().message(), "second dispatch proves registry cleanup");
    server->Shutdown();
}

}  // namespace functionsystem::test
