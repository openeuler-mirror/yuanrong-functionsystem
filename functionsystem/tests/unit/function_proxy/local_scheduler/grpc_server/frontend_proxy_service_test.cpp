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
#include <cstdlib>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <sys/stat.h>
#include <unistd.h>
#include <ftw.h>

#include <grpcpp/create_channel.h>
#include <grpcpp/server_builder.h>

#include "common/proto/pb/posix_pb.h"
#include "function_proxy/busproxy/invocation_handler/invocation_handler.h"
#include "mocks/mock_instance_proxy_wrapper.h"
// Allow test access to private members of FrontendProxyService.
#define private public
#include "grpc_server/frontend_proxy_service/frontend_proxy_service.h"
#undef private

namespace functionsystem::test {
using namespace local_scheduler;

namespace {
class UnixGrpcServer {
public:
    UnixGrpcServer(std::string socketPath, std::unique_ptr<::grpc::Server> server)
        : socketPath_(std::move(socketPath)), server_(std::move(server))
    {
    }

    ~UnixGrpcServer()
    {
        if (server_ != nullptr) {
            server_->Shutdown();
            server_->Wait();
        }
        (void)::unlink(socketPath_.c_str());
    }

    ::grpc::Server *Get() const
    {
        return server_.get();
    }

private:
    std::string socketPath_;
    std::unique_ptr<::grpc::Server> server_;
};

class ScopedInvocationProxy {
public:
    ScopedInvocationProxy() : proxy_(std::make_shared<MockInstanceProxy>())
    {
        ON_CALL(*proxy_, CompleteFrontendCall)
            .WillByDefault([](const litebus::AID &, const SharedStreamMsg &, const SharedStreamMsg &ack) {
                return litebus::Future<SharedStreamMsg>(ack);
            });
        InvocationHandler::BindInstanceProxy(proxy_);
    }

    ~ScopedInvocationProxy()
    {
        InvocationHandler::UnBindInstanceProxy();
    }

private:
    std::shared_ptr<MockInstanceProxy> proxy_;
};
}  // namespace

TEST(FrontendProxyServiceTest, RejectsUnauthenticatedPeerWhenRequired)
{
    FrontendProxyServiceParam param;
    param.requireAuthenticatedPeer = true;
    FrontendProxyService service(std::move(param));

    ::frontend_proxy::InvokeInstanceRequest request;
    ::frontend_proxy::InvokeInstanceResponse response;
    auto status = service.InvokeInstance(nullptr, &request, &response);

    EXPECT_EQ(status.error_code(), ::grpc::StatusCode::UNAUTHENTICATED);
}

TEST(FrontendProxyServiceTest, InvokeTimeoutUsesFunctionTimeoutWithResultBuffer)
{
    FrontendProxyServiceParam param;
    param.invokeResultTimeoutMs = 60000;
    param.invokeResultTimeoutBufferMs = 5000;
    ::frontend_proxy::InvokeInstanceRequest request;

    EXPECT_EQ(FrontendProxyService::ResolveInvokeResultTimeout(request, param).count(), 65000);
    request.set_invoketimeoutms(-1);
    EXPECT_EQ(FrontendProxyService::ResolveInvokeResultTimeout(request, param).count(), 65000);
    request.set_invoketimeoutms(30000);
    EXPECT_EQ(FrontendProxyService::ResolveInvokeResultTimeout(request, param).count(), 35000);
    request.set_invoketimeoutms(std::numeric_limits<int64_t>::max());
    EXPECT_EQ(FrontendProxyService::ResolveInvokeResultTimeout(request, param).count(), 8640005000);
}

TEST(FrontendProxyServiceTest, AwaitInvokeResultUsesRemainingDeadline)
{
    FrontendProxyService service(FrontendProxyServiceParam {});
    ::frontend_proxy::InvokeInstanceRequest request;
    request.mutable_context()->set_requestid("invoke-before-deadline");
    request.mutable_invoke()->set_instanceid("instance-before-deadline");
    litebus::Promise<SharedStreamMsg> promise;
    auto resultFuture = promise.GetFuture();
    std::thread producer([&promise]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        auto result = std::make_shared<runtime_rpc::StreamingMessage>();
        result->mutable_callresultreq()->set_code(common::ERR_NONE);
        result->mutable_callresultreq()->set_requestid("invoke-before-deadline");
        promise.SetValue(result);
    });
    ::frontend_proxy::InvokeInstanceResponse response;

    auto status = service.AwaitInvokeResult(nullptr, request, response, resultFuture,
                                            std::chrono::steady_clock::now() + std::chrono::milliseconds(200));
    producer.join();

    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.status().code(), common::ERR_NONE);
    EXPECT_EQ(response.callresult().requestid(), "invoke-before-deadline");
}

TEST(FrontendProxyServiceTest, AwaitInvokeResultTimesOutAtExpiredDeadline)
{
    FrontendProxyService service(FrontendProxyServiceParam {});
    ::frontend_proxy::InvokeInstanceRequest request;
    request.mutable_context()->set_requestid("invoke-expired-deadline");
    request.mutable_invoke()->set_instanceid("instance-expired-deadline");
    litebus::Promise<SharedStreamMsg> promise;
    ::frontend_proxy::InvokeInstanceResponse response;

    auto status = service.AwaitInvokeResult(nullptr, request, response, promise.GetFuture(),
                                            std::chrono::steady_clock::now() - std::chrono::milliseconds(1));

    EXPECT_TRUE(status.ok());
    EXPECT_EQ(response.status().code(), common::ERR_INNER_SYSTEM_ERROR);
    EXPECT_EQ(response.status().retryreason(), "post-dispatch-unknown");
}

TEST(FrontendProxyServiceTest, InvokeUsesFunctionProxyAsRuntimeSender)
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
    EXPECT_EQ(capturedCaller, "function-proxy");
    EXPECT_EQ(capturedRequest->invokereq().requestid(), "frontend-proxy-request-1");
}

TEST(FrontendProxyServiceTest, InvokeRejectsTenantThatDoesNotOwnTargetInstance)
{
    bool dispatched = false;
    FrontendProxyServiceParam param;
    param.invokeTenantAuthorizer = [](const std::string &tenantID, const std::string &instanceID) {
        EXPECT_EQ(tenantID, "tenant-a");
        EXPECT_EQ(instanceID, "instance-b");
        return false;
    };
    param.invokeDispatcher = [&dispatched](const std::string &, const SharedStreamMsg &) {
        dispatched = true;
        return litebus::Future<SharedStreamMsg>();
    };
    FrontendProxyService service(std::move(param));

    ::frontend_proxy::InvokeInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-process");
    request.mutable_context()->set_requestid("tenant-mismatch-request");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_invoke()->set_instanceid("instance-b");
    request.mutable_invoke()->set_requestid("tenant-mismatch-request");
    ::frontend_proxy::InvokeInstanceResponse response;

    EXPECT_TRUE(service.InvokeInstance(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_AUTHORIZE_FAILED);
    EXPECT_FALSE(dispatched);
}

TEST(FrontendProxyServiceTest, InvokeTerminalSuccessReturnsDirectResult)
{
    ScopedInvocationProxy invocationProxy;
    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-a";
    param.endpointAddress = "10.0.0.11:19090";
    param.invokeResultTimeoutMs = 100;
    param.invokeDispatcher = [](const std::string &, const SharedStreamMsg &request) {
        auto result = std::make_shared<runtime_rpc::StreamingMessage>();
        result->mutable_callresultreq()->set_code(common::ERR_NONE);
        result->mutable_callresultreq()->set_requestid(request->invokereq().requestid());
        result->mutable_callresultreq()->set_instanceid(request->invokereq().instanceid());
        (void)InvocationHandler::CallResultAdapter(request->invokereq().instanceid(), result);
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

TEST(FrontendProxyServiceTest, InvokeStreamForwardsEventsBeforeFinalResult)
{
    ScopedInvocationProxy invocationProxy;
    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-stream";
    param.endpointAddress = "127.0.0.1:19090";
    param.invokeResultTimeoutMs = 1000;
    param.invokeDispatcher = [](const std::string &caller, const SharedStreamMsg &request) {
        EXPECT_EQ(caller, "function-proxy");

        auto event = std::make_shared<runtime_rpc::StreamingMessage>();
        event->mutable_eventreq()->set_requestid(request->invokereq().requestid());
        event->mutable_eventreq()->set_message(std::string(16, '\0') + "first-event");
        (void)InvocationHandler::EventAdapter("runtime-instance", event);

        auto eof = std::make_shared<runtime_rpc::StreamingMessage>();
        eof->mutable_eventreq()->set_requestid(request->invokereq().requestid());
        eof->mutable_eventreq()->set_message(std::string(16, '\0') + "yuanrong_event_EOF");
        (void)InvocationHandler::EventAdapter("runtime-instance", eof);

        auto result = std::make_shared<runtime_rpc::StreamingMessage>();
        result->mutable_callresultreq()->set_code(common::ERR_NONE);
        result->mutable_callresultreq()->set_requestid(request->invokereq().requestid());
        result->mutable_callresultreq()->set_instanceid(request->invokereq().instanceid());
        result->mutable_callresultreq()->add_smallobjects()->set_value("final-result");
        (void)InvocationHandler::CallResultAdapter(request->invokereq().instanceid(), result);

        auto response = std::make_shared<runtime_rpc::StreamingMessage>();
        response->mutable_invokersp()->set_code(common::ERR_NONE);
        return litebus::Future<SharedStreamMsg>(response);
    };
    FrontendProxyService service(std::move(param));
    ::grpc::ServerBuilder builder;
    const auto socketPath = "/tmp/frontend-proxy-invoke-stream-" + std::to_string(::getpid()) + ".sock";
    const auto address = "unix:" + socketPath;
    (void)::unlink(socketPath.c_str());
    builder.AddListeningPort(address, ::grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    UnixGrpcServer server(socketPath, builder.BuildAndStart());
    ASSERT_NE(server.Get(), nullptr);

    auto channel = ::grpc::CreateChannel(address, ::grpc::InsecureChannelCredentials());
    ASSERT_TRUE(channel->WaitForConnected(std::chrono::system_clock::now() + std::chrono::seconds(2)));
    auto stub = ::frontend_proxy::FrontendProxyService::NewStub(channel);
    ::frontend_proxy::InvokeInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-a");
    request.mutable_context()->set_requestid("invoke-stream-request");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_invoke()->set_instanceid("instance-a");
    ::grpc::ClientContext clientContext;
    auto reader = stub->InvokeInstanceStream(&clientContext, request);

    ::frontend_proxy::InvokeInstanceStreamResponse frame;
    ASSERT_TRUE(reader->Read(&frame));
    ASSERT_TRUE(frame.has_event());
    EXPECT_EQ(frame.event(), "first-event");
    ASSERT_TRUE(reader->Read(&frame));
    ASSERT_TRUE(frame.has_final());
    EXPECT_EQ(frame.final().status().code(), common::ERR_NONE);
    ASSERT_EQ(frame.final().callresult().smallobjects_size(), 1);
    EXPECT_EQ(frame.final().callresult().smallobjects(0).value(), "final-result");
    EXPECT_FALSE(reader->Read(&frame));
    EXPECT_TRUE(reader->Finish().ok());
}

TEST(FrontendProxyServiceTest, InvokeStreamCancellationClearsEventAndResultRegistries)
{
    auto pending = std::make_shared<litebus::Promise<SharedStreamMsg>>();
    std::mutex mutex;
    std::condition_variable entered;
    int dispatchCount = 0;
    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-stream-cancel";
    param.endpointAddress = "127.0.0.1:19090";
    param.invokeResultTimeoutMs = 5000;
    param.invokeDispatcher = [&](const std::string &, const SharedStreamMsg &) {
        std::lock_guard<std::mutex> lock(mutex);
        ++dispatchCount;
        entered.notify_all();
        if (dispatchCount == 1) {
            return pending->GetFuture();
        }
        auto response = std::make_shared<runtime_rpc::StreamingMessage>();
        response->mutable_invokersp()->set_code(common::ERR_PARAM_INVALID);
        response->mutable_invokersp()->set_message("retry proves stream registries were cleared");
        return litebus::Future<SharedStreamMsg>(response);
    };
    FrontendProxyService service(std::move(param));
    ::grpc::ServerBuilder builder;
    const auto socketPath = "/tmp/frontend-proxy-stream-cancel-" + std::to_string(::getpid()) + ".sock";
    const auto address = "unix:" + socketPath;
    (void)::unlink(socketPath.c_str());
    builder.AddListeningPort(address, ::grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    UnixGrpcServer server(socketPath, builder.BuildAndStart());
    ASSERT_NE(server.Get(), nullptr);

    auto channel = ::grpc::CreateChannel(address, ::grpc::InsecureChannelCredentials());
    ASSERT_TRUE(channel->WaitForConnected(std::chrono::system_clock::now() + std::chrono::seconds(2)));
    auto stub = ::frontend_proxy::FrontendProxyService::NewStub(channel);
    ::frontend_proxy::InvokeInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-a");
    request.mutable_context()->set_requestid("invoke-stream-cancel-request");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_invoke()->set_instanceid("instance-a");

    ::grpc::ClientContext firstContext;
    auto firstReader = stub->InvokeInstanceStream(&firstContext, request);
    auto firstCall = std::async(std::launch::async, [&] {
        ::frontend_proxy::InvokeInstanceStreamResponse frame;
        while (firstReader->Read(&frame)) {
        }
        return firstReader->Finish();
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(entered.wait_for(lock, std::chrono::seconds(2), [&] { return dispatchCount == 1; }));
    }
    firstContext.TryCancel();
    EXPECT_EQ(firstCall.get().error_code(), ::grpc::StatusCode::CANCELLED);

    bool retrySucceeded = false;
    const auto retryDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!retrySucceeded && std::chrono::steady_clock::now() < retryDeadline) {
        ::grpc::ClientContext retryContext;
        auto retryReader = stub->InvokeInstanceStream(&retryContext, request);
        ::frontend_proxy::InvokeInstanceStreamResponse retryFrame;
        ASSERT_TRUE(retryReader->Read(&retryFrame));
        ASSERT_TRUE(retryFrame.has_final());
        retrySucceeded = retryFrame.final().status().message() == "retry proves stream registries were cleared";
        EXPECT_FALSE(retryReader->Read(&retryFrame));
        EXPECT_TRUE(retryReader->Finish().ok());
        if (!retrySucceeded) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    EXPECT_TRUE(retrySucceeded);
    std::lock_guard<std::mutex> lock(mutex);
    EXPECT_EQ(dispatchCount, 2);
}

TEST(FrontendProxyServiceTest, InvokeTerminalSuccessUsesRuntimeSenderWhenResultOmitsInstanceID)
{
    ScopedInvocationProxy invocationProxy;
    FrontendProxyServiceParam param;
    param.invokeResultTimeoutMs = 100;
    param.invokeDispatcher = [](const std::string &, const SharedStreamMsg &request) {
        auto result = std::make_shared<runtime_rpc::StreamingMessage>();
        result->mutable_callresultreq()->set_code(common::ERR_NONE);
        result->mutable_callresultreq()->set_requestid(request->invokereq().requestid());
        (void)InvocationHandler::CallResultAdapter(request->invokereq().instanceid(), result);
        auto response = std::make_shared<runtime_rpc::StreamingMessage>();
        response->mutable_invokersp()->set_code(common::ERR_NONE);
        return litebus::Future<SharedStreamMsg>(response);
    };
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::InvokeInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-a");
    request.mutable_context()->set_requestid("invoke-without-result-instance");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_invoke()->set_instanceid("instance-a");
    ::frontend_proxy::InvokeInstanceResponse response;

    EXPECT_TRUE(service.InvokeInstance(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_NONE);
    EXPECT_EQ(response.callresult().requestid(), "invoke-without-result-instance");
    EXPECT_TRUE(response.callresult().instanceid().empty());
}

TEST(FrontendProxyServiceTest, CreateReadyTerminalSuccessCarriesOwningRoute)
{
    EXPECT_STREQ(FrontendProxyService::lifecycleTransport, "raw-unary");
    EXPECT_STREQ(FrontendProxyService::readyOperation, "ready");
    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-a";
    param.endpointAddress = "10.0.0.11:19090";
    param.enableCreateDispatch = true;
    param.createReadyDispatcher = [](const ::frontend_proxy::CreateInstanceRequest &) {
        ::frontend_proxy::CreateInstanceResponse response;
        response.mutable_create()->set_code(common::ERR_NONE);
        response.mutable_create()->set_instanceid("created-instance");
        response.set_routeaddress("stale-entry-proxy");
        response.mutable_callresult()->mutable_runtimeinfo()->set_proxyid("final-owner-proxy");
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
    EXPECT_EQ(response.routeaddress(), "final-owner-proxy");
}

// async create: no callresult -> owningProxyID empty -> fallback to param_.nodeID.
TEST(FrontendProxyServiceTest, CreateAsyncTerminalCarriesLocalNodeAsOwnerWhenNoCallResult)
{
    EXPECT_STREQ(FrontendProxyService::lifecycleTransport, "raw-unary");
    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-a";
    param.endpointAddress = "10.0.0.11:19090";
    param.enableCreateDispatch = true;
    param.createReadyDispatcher = [](const ::frontend_proxy::CreateInstanceRequest &) {
        ::frontend_proxy::CreateInstanceResponse response;
        response.mutable_create()->set_code(common::ERR_NONE);
        response.mutable_create()->set_instanceid("creating-instance");
        return litebus::Future<::frontend_proxy::CreateInstanceResponse>(response);
    };
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::CreateInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-a");
    request.mutable_context()->set_requestid("create-async-no-callresult");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_create()->set_function("0/tenant-a/faas/function");
    (*request.mutable_create()->mutable_createoptions())["source"] = "frontend";
    ::frontend_proxy::CreateInstanceResponse response;

    EXPECT_TRUE(service.CreateInstance(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_NONE);
    EXPECT_EQ(response.create().instanceid(), "creating-instance");
    EXPECT_EQ(response.routeaddress(), "proxy-node-a");
}

TEST(FrontendProxyServiceTest, DuplicateRequestIDDoesNotReplaceExistingWaiter)
{
    std::mutex mutex;
    std::condition_variable entered;
    bool firstInvokeDispatched = false;
    bool releaseFirstInvoke = false;
    size_t dispatchCount = 0;

    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-a";
    param.invokeResultTimeoutMs = 20;
    param.invokeDispatcher = [&mutex, &entered, &firstInvokeDispatched, &releaseFirstInvoke,
                              &dispatchCount](const std::string &, const SharedStreamMsg &) {
        std::unique_lock<std::mutex> lock(mutex);
        ++dispatchCount;
        if (dispatchCount == 1) {
            firstInvokeDispatched = true;
            entered.notify_one();
            entered.wait(lock, [&releaseFirstInvoke] { return releaseFirstInvoke; });
        }
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

TEST(FrontendProxyServiceTest, InvokeRejectedBeforeRuntimeExecutionIsRetryable)
{
    FrontendProxyServiceParam param;
    param.invokeDispatcher = [](const std::string &, const SharedStreamMsg &) {
        auto response = std::make_shared<runtime_rpc::StreamingMessage>();
        response->mutable_invokersp()->set_code(common::ERR_INSTANCE_NOT_FOUND);
        response->mutable_invokersp()->set_message("runtime did not accept invoke");
        return litebus::Future<SharedStreamMsg>(response);
    };
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::InvokeInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-a");
    request.mutable_context()->set_requestid("invoke-rejected");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_invoke()->set_instanceid("instance-a");
    ::frontend_proxy::InvokeInstanceResponse response;

    EXPECT_TRUE(service.InvokeInstance(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_INSTANCE_NOT_FOUND);
    EXPECT_TRUE(response.status().retryable());
    EXPECT_EQ(response.status().retryreason(), "call-response-error");
}

TEST(FrontendProxyServiceTest, InvokeRuntimeResultFailureIsSeparatedFromProxyStatus)
{
    ScopedInvocationProxy invocationProxy;
    FrontendProxyServiceParam param;
    param.invokeResultTimeoutMs = 100;
    param.invokeDispatcher = [](const std::string &, const SharedStreamMsg &request) {
        auto result = std::make_shared<runtime_rpc::StreamingMessage>();
        result->mutable_callresultreq()->set_code(common::ERR_INSTANCE_EXITED);
        result->mutable_callresultreq()->set_message("runtime exited during invoke");
        result->mutable_callresultreq()->set_requestid(request->invokereq().requestid());
        (void)InvocationHandler::CallResultAdapter(request->invokereq().instanceid(), result);
        auto response = std::make_shared<runtime_rpc::StreamingMessage>();
        response->mutable_invokersp()->set_code(common::ERR_NONE);
        return litebus::Future<SharedStreamMsg>(response);
    };
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::InvokeInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-a");
    request.mutable_context()->set_requestid("invoke-result-failed");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_invoke()->set_instanceid("instance-a");
    ::frontend_proxy::InvokeInstanceResponse response;

    EXPECT_TRUE(service.InvokeInstance(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_NONE);
    EXPECT_FALSE(response.status().retryable());
    EXPECT_TRUE(response.status().retryreason().empty());
    EXPECT_EQ(response.callresult().code(), common::ERR_INSTANCE_EXITED);
    EXPECT_EQ(response.callresult().message(), "runtime exited during invoke");
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

TEST(FrontendProxyServiceTest, KillSuccessWaitsForObservedCleanupCompletion)
{
    int probes = 0;
    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-a";
    param.enableKillDispatch = true;
    param.killCleanupTimeoutMs = 100;
    param.killReadyDispatcher = [](const ::frontend_proxy::KillInstanceRequest &) {
        ::frontend_proxy::KillInstanceResponse response;
        response.mutable_kill()->set_code(common::ERR_NONE);
        return litebus::Future<::frontend_proxy::KillInstanceResponse>(response);
    };
    param.killCleanupProbe = [&probes](const std::string &requestID, const std::string &instanceID) {
        FrontendKillCleanupSnapshot snapshot;
        snapshot.requestTicketKnown = true;
        snapshot.requestTicketCleared = ++probes > 1;
        snapshot.instanceTicketKnown = true;
        snapshot.instanceTicketCleared = probes > 1;
        snapshot.runtimeState = probes > 1 ? "terminated" : "terminating";
        snapshot.instanceState = probes > 1 ? "absent" : "exiting";
        EXPECT_EQ(requestID, "kill-cleanup-complete");
        EXPECT_EQ(instanceID, "instance-a");
        return litebus::Future<FrontendKillCleanupSnapshot>(snapshot);
    };
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::KillInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-a");
    request.mutable_context()->set_requestid("kill-cleanup-complete");
    request.mutable_context()->set_traceid("trace-cleanup-complete");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_kill()->set_instanceid("instance-a");
    ::frontend_proxy::KillInstanceResponse response;

    EXPECT_TRUE(service.KillInstance(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_NONE);
    EXPECT_GE(probes, 2);
}

TEST(FrontendProxyServiceTest, KillSuccessDoesNotSynthesizeCleanupWhenEvidenceStaysIncomplete)
{
    size_t probes = 0;
    FrontendProxyServiceParam param;
    param.nodeID = "proxy-node-a";
    param.enableKillDispatch = true;
    param.killCleanupTimeoutMs = 20;
    param.killReadyDispatcher = [](const ::frontend_proxy::KillInstanceRequest &) {
        ::frontend_proxy::KillInstanceResponse response;
        response.mutable_kill()->set_code(common::ERR_NONE);
        return litebus::Future<::frontend_proxy::KillInstanceResponse>(response);
    };
    param.killCleanupProbe = [&probes](const std::string &, const std::string &) {
        ++probes;
        FrontendKillCleanupSnapshot snapshot;
        snapshot.requestTicketKnown = true;
        snapshot.requestTicketCleared = true;
        snapshot.instanceTicketKnown = true;
        snapshot.instanceTicketCleared = false;
        snapshot.runtimeState = "unknown";
        snapshot.instanceState = "exiting";
        return litebus::Future<FrontendKillCleanupSnapshot>(snapshot);
    };
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::KillInstanceRequest request;
    request.mutable_context()->set_frontendclientid("frontend-a");
    request.mutable_context()->set_requestid("kill-cleanup-incomplete");
    request.mutable_context()->set_traceid("trace-cleanup-incomplete");
    request.mutable_context()->set_tenantid("tenant-a");
    request.mutable_kill()->set_instanceid("instance-a");
    ::frontend_proxy::KillInstanceResponse response;

    const auto started = std::chrono::steady_clock::now();
    EXPECT_TRUE(service.KillInstance(nullptr, &request, &response).ok());
    const auto elapsed = std::chrono::steady_clock::now() - started;
    EXPECT_EQ(response.status().code(), common::ERR_NONE);
    EXPECT_GE(probes, static_cast<size_t>(1));
    EXPECT_GE(elapsed, std::chrono::milliseconds(10));
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
    UnixGrpcServer server(socketPath, builder.BuildAndStart());
    ASSERT_NE(server.Get(), nullptr);
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
    UnixGrpcServer server(socketPath, builder.BuildAndStart());
    ASSERT_NE(server.Get(), nullptr);

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
}

// --- 方案 A: Workspace RPC 单测 ---

namespace {
// ScopedTempDir 在构造时创建临时目录,析构时递归删除,用于 workspace RPC 测试隔离。
class ScopedTempDir {
public:
    ScopedTempDir()
    {
        char tmpl[] = "/tmp/frontend_proxy_workspace_test_XXXXXX";
        // mkdtemp 原地修改模板并返回指针;失败时返回 nullptr。
        // 此处不使用 ASSERT_* 是因为构造函数不能返回值;失败时 path_ 留空,
        // 后续 Path() 返回空字符串,测试通过 ASSERT_FALSE(Path().empty()) 提前终止。
        const char *dir = ::mkdtemp(tmpl);
        if (dir != nullptr) {
            path_ = dir;
        }
    }
    ~ScopedTempDir()
    {
        if (!path_.empty()) {
            // 递归删除使用 nftw,与生产实现一致。
            auto cb = [](const char *fpath, const struct stat *, int, struct FTW *) -> int {
                if (::rmdir(fpath) == 0) {
                    return 0;
                }
                if (errno == ENOTDIR) {
                    (void)::unlink(fpath);
                }
                return 0;
            };
            (void)::nftw(path_.c_str(), cb, 16, FTW_DEPTH | FTW_PHYS);
        }
    }
    const std::string &Path() const
    {
        return path_;
    }

private:
    std::string path_;
};

bool PathExists(const std::string &path)
{
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0;
}

mode_t PathMode(const std::string &path)
{
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) {
        return 0;
    }
    return st.st_mode & 0777;
}

FrontendProxyServiceParam MakeWorkspaceParam(const std::string &storageRoot)
{
    FrontendProxyServiceParam param;
    param.workspaceStorageRootProvider = [root = storageRoot]() { return root; };
    // 使用当前测试进程的 uid/gid,避免非 root 测试环境下的 chown 失败。
    param.workspaceUidGidProvider = []() {
        return std::make_pair(::getuid(), ::getgid());
    };
    return param;
}
}  // namespace

TEST(FrontendProxyServiceWorkspaceTest, PrepareWorkspaceCreatesLeafDirectoryWithMode)
{
    ScopedTempDir root;
    auto param = MakeWorkspaceParam(root.Path());
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::PrepareWorkspaceRequest request;
    request.mutable_context()->set_requestid("prepare-create");
    request.set_owner("alice");
    request.set_mode("0700");
    ::frontend_proxy::PrepareWorkspaceResponse response;
    ASSERT_TRUE(service.PrepareWorkspace(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_NONE);
    const auto target = root.Path() + "/alice";
    EXPECT_TRUE(PathExists(target));
    EXPECT_EQ(PathMode(target), static_cast<mode_t>(0700));
}

TEST(FrontendProxyServiceWorkspaceTest, PrepareWorkspaceIsIdempotentWhenDirectoryExists)
{
    ScopedTempDir root;
    auto param = MakeWorkspaceParam(root.Path());
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::PrepareWorkspaceRequest request;
    request.mutable_context()->set_requestid("prepare-idempotent");
    request.set_owner("bob");
    request.set_mode("0750");
    ::frontend_proxy::PrepareWorkspaceResponse first;
    ASSERT_TRUE(service.PrepareWorkspace(nullptr, &request, &first).ok());
    EXPECT_EQ(first.status().code(), common::ERR_NONE);
    ::frontend_proxy::PrepareWorkspaceResponse second;
    ASSERT_TRUE(service.PrepareWorkspace(nullptr, &request, &second).ok());
    EXPECT_EQ(second.status().code(), common::ERR_NONE);
    EXPECT_EQ(PathMode(root.Path() + "/bob"), static_cast<mode_t>(0750));
}

TEST(FrontendProxyServiceWorkspaceTest, PrepareWorkspaceUsesDefaultModeWhenModeEmpty)
{
    ScopedTempDir root;
    auto param = MakeWorkspaceParam(root.Path());
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::PrepareWorkspaceRequest request;
    request.mutable_context()->set_requestid("prepare-default-mode");
    request.set_owner("carol");
    request.clear_mode();
    ::frontend_proxy::PrepareWorkspaceResponse response;
    ASSERT_TRUE(service.PrepareWorkspace(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_NONE);
    // 默认权限 0700
    EXPECT_EQ(PathMode(root.Path() + "/carol"), static_cast<mode_t>(0700));
}

TEST(FrontendProxyServiceWorkspaceTest, PrepareWorkspaceCreatesIntermediateParents)
{
    ScopedTempDir root;
    // 直接在 root 下创建多级子目录的 owner 不合法(含 '/'),因此构造一个
    // 已存在的父目录层级,让 PrepareWorkspace 只负责创建叶子目录。
    const auto parent = root.Path() + "/tenant-x";
    ASSERT_EQ(::mkdir(parent.c_str(), 0755), 0);
    auto param = MakeWorkspaceParam(parent);
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::PrepareWorkspaceRequest request;
    request.mutable_context()->set_requestid("prepare-with-parent");
    request.set_owner("dave");
    request.set_mode("0700");
    ::frontend_proxy::PrepareWorkspaceResponse response;
    ASSERT_TRUE(service.PrepareWorkspace(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_NONE);
    EXPECT_TRUE(PathExists(parent + "/dave"));
}

TEST(FrontendProxyServiceWorkspaceTest, PrepareWorkspaceRejectsEmptyOwner)
{
    ScopedTempDir root;
    auto param = MakeWorkspaceParam(root.Path());
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::PrepareWorkspaceRequest request;
    request.set_owner("");
    ::frontend_proxy::PrepareWorkspaceResponse response;
    ASSERT_TRUE(service.PrepareWorkspace(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_PARAM_INVALID);
    EXPECT_FALSE(response.status().message().empty());
}

TEST(FrontendProxyServiceWorkspaceTest, PrepareWorkspaceRejectsPathTraversalOwner)
{
    ScopedTempDir root;
    auto param = MakeWorkspaceParam(root.Path());
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::PrepareWorkspaceRequest request;
    request.set_owner("..");
    ::frontend_proxy::PrepareWorkspaceResponse response;
    ASSERT_TRUE(service.PrepareWorkspace(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_PARAM_INVALID);
    request.set_owner("../etc");
    response.Clear();
    ASSERT_TRUE(service.PrepareWorkspace(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_PARAM_INVALID);
    request.set_owner(".hidden");
    response.Clear();
    ASSERT_TRUE(service.PrepareWorkspace(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_PARAM_INVALID);
}

TEST(FrontendProxyServiceWorkspaceTest, PrepareWorkspaceRejectsInvalidMode)
{
    ScopedTempDir root;
    auto param = MakeWorkspaceParam(root.Path());
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::PrepareWorkspaceRequest request;
    request.set_owner("eve");
    request.set_mode("9999");
    ::frontend_proxy::PrepareWorkspaceResponse response;
    ASSERT_TRUE(service.PrepareWorkspace(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_PARAM_INVALID);
}

TEST(FrontendProxyServiceWorkspaceTest, RemoveWorkspaceDeletesLeafDirectory)
{
    ScopedTempDir root;
    const auto target = root.Path() + "/frank";
    ASSERT_EQ(::mkdir(target.c_str(), 0700), 0);
    ASSERT_TRUE(PathExists(target));
    auto param = MakeWorkspaceParam(root.Path());
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::RemoveWorkspaceRequest request;
    request.mutable_context()->set_requestid("remove-leaf");
    request.set_owner("frank");
    ::frontend_proxy::RemoveWorkspaceResponse response;
    ASSERT_TRUE(service.RemoveWorkspace(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_NONE);
    EXPECT_FALSE(PathExists(target));
}

TEST(FrontendProxyServiceWorkspaceTest, RemoveWorkspaceIsIdempotentWhenMissing)
{
    ScopedTempDir root;
    auto param = MakeWorkspaceParam(root.Path());
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::RemoveWorkspaceRequest request;
    request.mutable_context()->set_requestid("remove-missing");
    request.set_owner("ghost");
    ::frontend_proxy::RemoveWorkspaceResponse response;
    ASSERT_TRUE(service.RemoveWorkspace(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_NONE);
}

TEST(FrontendProxyServiceWorkspaceTest, RemoveWorkspaceRecursivelyDeletesNestedContents)
{
    ScopedTempDir root;
    const auto target = root.Path() + "/heidi";
    ASSERT_EQ(::mkdir(target.c_str(), 0700), 0);
    ASSERT_EQ(::mkdir((target + "/sub").c_str(), 0700), 0);
    // 写入若干普通文件
    FILE *fp = std::fopen((target + "/sub/file.txt").c_str(), "w");
    ASSERT_NE(fp, nullptr);
    std::fputs("data", fp);
    std::fclose(fp);
    auto param = MakeWorkspaceParam(root.Path());
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::RemoveWorkspaceRequest request;
    request.mutable_context()->set_requestid("remove-recursive");
    request.set_owner("heidi");
    ::frontend_proxy::RemoveWorkspaceResponse response;
    ASSERT_TRUE(service.RemoveWorkspace(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_NONE);
    EXPECT_FALSE(PathExists(target));
}

TEST(FrontendProxyServiceWorkspaceTest, RemoveWorkspaceRejectsPathTraversalOwner)
{
    ScopedTempDir root;
    auto param = MakeWorkspaceParam(root.Path());
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::RemoveWorkspaceRequest request;
    request.set_owner("../etc");
    ::frontend_proxy::RemoveWorkspaceResponse response;
    ASSERT_TRUE(service.RemoveWorkspace(nullptr, &request, &response).ok());
    EXPECT_EQ(response.status().code(), common::ERR_PARAM_INVALID);
}

TEST(FrontendProxyServiceWorkspaceTest, PrepareAndRemoveWorkspaceRoundTripPreservesRoot)
{
    ScopedTempDir root;
    auto param = MakeWorkspaceParam(root.Path());
    FrontendProxyService service(std::move(param));
    ::frontend_proxy::PrepareWorkspaceRequest prepare;
    prepare.mutable_context()->set_requestid("round-trip-prepare");
    prepare.set_owner("ivan");
    prepare.set_mode("0700");
    ::frontend_proxy::PrepareWorkspaceResponse prepareResponse;
    ASSERT_TRUE(service.PrepareWorkspace(nullptr, &prepare, &prepareResponse).ok());
    EXPECT_EQ(prepareResponse.status().code(), common::ERR_NONE);
    ::frontend_proxy::RemoveWorkspaceRequest remove;
    remove.mutable_context()->set_requestid("round-trip-remove");
    remove.set_owner("ivan");
    ::frontend_proxy::RemoveWorkspaceResponse removeResponse;
    ASSERT_TRUE(service.RemoveWorkspace(nullptr, &remove, &removeResponse).ok());
    EXPECT_EQ(removeResponse.status().code(), common::ERR_NONE);
    EXPECT_FALSE(PathExists(root.Path() + "/ivan"));
    // 根目录仍然存在
    EXPECT_TRUE(PathExists(root.Path()));
}

}  // namespace functionsystem::test
