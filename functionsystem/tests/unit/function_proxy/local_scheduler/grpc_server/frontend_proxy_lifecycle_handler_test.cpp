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

#include "grpc_server/frontend_proxy_service/frontend_proxy_lifecycle_handler.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/provider.h"

#include "common/proto/pb/posix_pb.h"
#include "common/trace/trace_manager.h"
#include "common/utils/struct_transfer.h"

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
            // Delivered before Schedule completes; must not surface in the response.
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

    // create returns on schedule success with no CallResult.
    EXPECT_EQ(response.Get().create().code(), common::ERR_NONE);
    EXPECT_EQ(response.Get().create().instanceid(), "frontend-create-instance");
    EXPECT_FALSE(response.Get().has_callresult());
}

TEST(FrontendProxyLifecycleHandlerTest, CreateReturnsOnScheduleEvenWhenRuntimeNeverReportsReady)
{
    std::atomic<bool> unregistered { false };
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
    // create returns on schedule success even though ready never arrives.
    EXPECT_EQ(response.Get().create().code(), common::ERR_NONE);
    EXPECT_EQ(response.Get().create().instanceid(), "frontend-create-timeout-instance");
    EXPECT_FALSE(response.Get().has_callresult());

    // the ready ticket still times out and unregisters in the background.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(unregistered);
}

// yr.create span lifecycle on the FrontendProxyService create path.
// The span itself is started by the actor in ScheduleFrontendAndWaitReady once
// the ready ticket is registered; these tests seed it the same way and drive
// the three dispatcher outlets (ready fulfillment, ready timeout, schedule
// failure), asserting the span is started/stopped via GetSpanIDFromStore.
// Scope note: only span START/STOP is observable here — the TraceManager test
// surface exposes span presence, not attribute values (yr.result/node/
// yr.retry_* on stop are not asserted; see the actor-level tests in
// frontend_create_trace_test.cpp for the snapshot bookkeeping itself).
class FrontendCreateSpanTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Save the process-wide tracing state (TraceManager is a singleton and
        // the global TracerProvider is shared); restore it in TearDown so the
        // suites running after this one are not left with tracing disabled.
        enableTraceSaved_ = trace::TraceManager::GetInstance().enableTrace_;
        hostIDSaved_ = trace::TraceManager::GetInstance().hostID_;
        providerSaved_ = opentelemetry::trace::Provider::GetTracerProvider();
        trace::TraceManager::GetInstance().Clear();
        trace::TraceManager::GetInstance().enableTrace_ = true;
        trace::TraceManager::GetInstance().hostID_ = "node-a";
        auto provider = opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider>(
            opentelemetry::sdk::trace::TracerProviderFactory::Create(
                std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>>{})
                .release());
        opentelemetry::trace::Provider::SetTracerProvider(provider);
    }

    void TearDown() override
    {
        trace::TraceManager::GetInstance().Clear();
        trace::TraceManager::GetInstance().enableTrace_ = enableTraceSaved_;
        trace::TraceManager::GetInstance().hostID_ = hostIDSaved_;
        opentelemetry::trace::Provider::SetTracerProvider(providerSaved_);
    }

private:
    bool enableTraceSaved_ { false };
    std::string hostIDSaved_;
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider> providerSaved_;

    // Starts the yr.create span the way the actor does in
    // ScheduleFrontendAndWaitReady after successful ticket registration.
    void SeedCreateSpan(const std::string &requestID)
    {
        trace::TraceManager::SpanParam param;
        param.spanName = trace::SpanName::K_CREATE;
        param.spanKey = requestID;
        param.traceID = "trace-" + requestID;
        param.function = "0/tenant/faas/fn";
        (void)trace::TraceManager::GetInstance().StartSpanWithRecord(std::move(param));
    }

    bool CreateSpanInStore(const std::string &requestID)
    {
        return !trace::TraceManager::GetInstance()
                    .GetSpanIDFromStore(requestID, trace::SpanName::K_CREATE)
                    .empty();
    }

    bool WaitForCreateSpanGone(const std::string &requestID)
    {
        for (int i = 0; i < 200; ++i) {
            if (!CreateSpanInStore(requestID)) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return !CreateSpanInStore(requestID);
    }

    // Bounded poll for a probe flag written on a callback thread: asserting the
    // flag right after a span-store observation would still be a race (the flag
    // write and the span stop are separate steps in the timeout closure).
    bool WaitForFlag(const std::atomic<bool> &flag)
    {
        for (int i = 0; i < 200; ++i) {
            if (flag.load()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return flag.load();
    }

    ::frontend_proxy::CreateInstanceRequest BuildCreateRequest(const std::string &requestID)
    {
        ::frontend_proxy::CreateInstanceRequest request;
        request.mutable_context()->set_frontendclientid("frontend-client-1");
        request.mutable_context()->set_requestid(requestID);
        request.mutable_context()->set_traceid("trace-" + requestID);
        request.mutable_context()->set_tenantid("tenant-a");
        request.mutable_create()->set_function("0/tenant/faas/fn");
        request.mutable_create()->set_requestid(requestID);
        (*request.mutable_create()->mutable_createoptions())["source"] = "frontend";
        (*request.mutable_create()->mutable_createoptions())["tenantId"] = "tenant-a";
        return request;
    }
};

// Outlet A — ready fulfillment: the ready callback stops the span and consumes
// the failure snapshot (retry bookkeeping) via the lookup. Short timeout (not
// the production constant) so the ready-timeout closure cannot outlive the
// test with a dangling timer if the fulfillment path ever breaks.
TEST_F(FrontendCreateSpanTest, ReadyFulfillmentStopsCreateSpanAndConsumesSnapshot)
{
    const std::string requestID = "span-fulfill-request";
    SeedCreateSpan(requestID);
    ASSERT_TRUE(CreateSpanInStore(requestID));

    std::atomic<bool> lookupCalled { false };
    // Probe on the unregister path: only the timeout closure calls it, so a
    // false flag proves the span was stopped by the fulfillment path itself and
    // not by the (short) ready-timeout closure standing in for it.
    std::atomic<bool> unregistered { false };
    FrontendCreateFailureSnapshot lookupSnapshot;
    lookupSnapshot.present = true;
    lookupSnapshot.code = 12345;
    lookupSnapshot.retriedTimes = 2;
    auto dispatcher = BuildFrontendProxyCreateReadyDispatcher(
        [](const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
           const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &,
           FrontendProxyReadyCallback callback) {
            auto readyResult = std::make_shared<functionsystem::CallResult>();
            readyResult->set_requestid(scheduleReq->requestid());
            readyResult->set_instanceid("frontend-create-instance");
            readyResult->set_code(common::ERR_NONE);
            readyResult->mutable_runtimeinfo()->set_proxyid("owning-node");
            (void)callback(readyResult);
            messages::ScheduleResponse scheduleResponse;
            scheduleResponse.set_code(common::ERR_NONE);
            scheduleResponse.set_requestid(scheduleReq->requestid());
            scheduleResponse.set_instanceid("frontend-create-instance");
            return litebus::Future<messages::ScheduleResponse>(scheduleResponse);
        },
        [&unregistered](const std::string &, const std::string &) { unregistered = true; },
        50,
        [&lookupCalled, lookupSnapshot](const std::string &requestIDLookedUp) {
            EXPECT_EQ(requestIDLookedUp, "span-fulfill-request");
            lookupCalled = true;
            return litebus::Future<FrontendCreateFailureSnapshot>(lookupSnapshot);
        });

    auto response = dispatcher(BuildCreateRequest(requestID)).Get(1000);
    ASSERT_TRUE(response.IsSome());
    EXPECT_EQ(response.Get().create().code(), common::ERR_NONE);

    EXPECT_TRUE(lookupCalled);
    EXPECT_TRUE(WaitForCreateSpanGone(requestID)) << "yr.create span not stopped on ready fulfillment";
    // Outlive the (short) ready timeout and prove the timeout closure stayed
    // disarmed: the fulfillment outlet resolved the promise, which cancelled it.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_FALSE(unregistered) << "timeout closure fired although ready fulfillment resolved the promise";
}

// Outlet B — ready timeout: the 55s (here: 50ms) timeout closure stops the
// span with the failure snapshot from the lookup.
TEST_F(FrontendCreateSpanTest, ReadyTimeoutStopsCreateSpanAndConsumesSnapshot)
{
    const std::string requestID = "span-timeout-request";
    SeedCreateSpan(requestID);
    ASSERT_TRUE(CreateSpanInStore(requestID));

    std::atomic<bool> lookupCalled { false };
    std::atomic<bool> unregistered { false };
    FrontendCreateFailureSnapshot lookupSnapshot;
    lookupSnapshot.present = true;
    lookupSnapshot.code = 12345;
    lookupSnapshot.retriedTimes = 3;
    auto dispatcher = BuildFrontendProxyCreateReadyDispatcher(
        [](const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
           const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &,
           FrontendProxyReadyCallback) {
            // Schedule succeeds but the runtime never reports ready.
            messages::ScheduleResponse scheduleResponse;
            scheduleResponse.set_code(common::ERR_NONE);
            scheduleResponse.set_requestid(scheduleReq->requestid());
            scheduleResponse.set_instanceid("frontend-create-timeout-instance");
            return litebus::Future<messages::ScheduleResponse>(scheduleResponse);
        },
        [&unregistered](const std::string &, const std::string &) { unregistered = true; },
        50,
        [&lookupCalled, lookupSnapshot](const std::string &) {
            lookupCalled = true;
            return litebus::Future<FrontendCreateFailureSnapshot>(lookupSnapshot);
        });

    auto response = dispatcher(BuildCreateRequest(requestID)).Get(1000);
    ASSERT_TRUE(response.IsSome());
    EXPECT_EQ(response.Get().create().code(), common::ERR_NONE);

    EXPECT_TRUE(WaitForCreateSpanGone(requestID)) << "yr.create span not stopped on ready timeout";
    // Poll instead of asserting immediately: the lookup/unregister calls are
    // separate steps on the timeout thread, so a span-store observation alone
    // does not guarantee they have been reached yet.
    EXPECT_TRUE(WaitForFlag(lookupCalled));
    EXPECT_TRUE(WaitForFlag(unregistered));
}

// Outlet C — schedule failure after ticket registration: the span is stopped
// immediately with the real schedule error (not after the ready timeout), and
// the timeout closure is disarmed so it can neither stop the span again nor
// fire the unregister. (In production the ticket itself is unregistered
// actor-side by OnFrontendScheduleCompleted, which watches the same schedule
// future — the dispatcher only disarms its own timeout.)
TEST_F(FrontendCreateSpanTest, ScheduleFailureWithRegisteredTicketStopsSpanImmediately)
{
    const std::string requestID = "span-schedule-fail-request";
    SeedCreateSpan(requestID);
    ASSERT_TRUE(CreateSpanInStore(requestID));

    std::atomic<bool> unregistered { false };
    auto dispatcher = BuildFrontendProxyCreateReadyDispatcher(
        [](const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
           const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &,
           FrontendProxyReadyCallback) {
            // Mimic the actor: the ticket was registered before the schedule
            // failed, so this request owns the yr.create span.
            (*scheduleReq->mutable_instance()->mutable_extensions())[FRONTEND_READY_TICKET_REGISTERED] = "true";
            messages::ScheduleResponse scheduleResponse;
            scheduleResponse.set_code(static_cast<int32_t>(common::ERR_INNER_SYSTEM_ERROR));
            scheduleResponse.set_message("schedule failed");
            scheduleResponse.set_requestid(scheduleReq->requestid());
            return litebus::Future<messages::ScheduleResponse>(scheduleResponse);
        },
        [&unregistered](const std::string &, const std::string &) { unregistered = true; },
        100);

    auto response = dispatcher(BuildCreateRequest(requestID)).Get(1000);
    ASSERT_TRUE(response.IsSome());
    EXPECT_NE(response.Get().create().code(), common::ERR_NONE);

    EXPECT_TRUE(WaitForCreateSpanGone(requestID))
        << "yr.create span not stopped immediately on schedule failure";
    // The failure branch resolved the ready promise, which must disarm the
    // timeout closure: within the (short) window it must not unregister.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_FALSE(unregistered) << "timeout closure fired although the schedule failure disarmed it";
}

// Duplicate requestID rejection: no ticket was registered for this request, so
// the dispatcher must not touch the pending span under this requestID (owned
// by the original in-flight request) and must disarm the ready timeout so the
// timeout closure cannot stop it either. The unregister counter doubles as a
// deterministic probe that the timeout closure really stayed disarmed (timer
// firings can be delayed on a loaded CI host, so span presence alone could
// produce a false pass).
TEST_F(FrontendCreateSpanTest, DuplicateRejectionLeavesOriginalSpanUntouched)
{
    const std::string requestID = "span-duplicate-request";
    SeedCreateSpan(requestID);  // the "original" request's span
    ASSERT_TRUE(CreateSpanInStore(requestID));

    std::atomic<bool> unregistered { false };
    auto dispatcher = BuildFrontendProxyCreateReadyDispatcher(
        [](const std::shared_ptr<messages::ScheduleRequest> &scheduleReq,
           const std::shared_ptr<litebus::Promise<messages::ScheduleResponse>> &,
           FrontendProxyReadyCallback) {
            // Registration rejected (duplicate requestID): no marker written.
            messages::ScheduleResponse scheduleResponse;
            scheduleResponse.set_code(static_cast<int32_t>(common::ERR_INNER_SYSTEM_ERROR));
            scheduleResponse.set_message("frontend create request id is already pending");
            scheduleResponse.set_requestid(scheduleReq->requestid());
            return litebus::Future<messages::ScheduleResponse>(scheduleResponse);
        },
        [&unregistered](const std::string &, const std::string &) { unregistered = true; },
        200);

    auto response = dispatcher(BuildCreateRequest(requestID)).Get(1000);
    ASSERT_TRUE(response.IsSome());
    EXPECT_NE(response.Get().create().code(), common::ERR_NONE);

    // Outlive the (short) ready timeout: the span must still be there and the
    // timeout closure must not have fired (it would unregister).
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    EXPECT_TRUE(CreateSpanInStore(requestID))
        << "duplicate rejection must not stop the original request's yr.create span";
    EXPECT_FALSE(unregistered) << "timeout closure fired on a duplicate rejection and would stop the original span";

    trace::TraceManager::GetInstance().StopSpan(trace::SpanName::K_CREATE, requestID);
}

}  // namespace functionsystem::test
