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

// FS-T3: frontend-create retry bookkeeping (FrontendCreateFailureSnapshot
// retriedTimes/firstFailure* fields, recorded by the readiness redeploy loop
// and consumed when the yr.create span stops), plus the D4 span-start
// ownership rule in ScheduleFrontendAndWaitReady (a duplicate requestID must
// never start — or later stop — the original request's span).
//
// Note: span ATTRIBUTE values (yr.result/node/yr.retry_* on stop) are not
// observable through the current TraceManager test surface (only the
// spanMap via GetSpanIDFromStore); they are verified end-to-end.

#include "local_scheduler/instance_control/instance_ctrl_actor.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include "opentelemetry/sdk/trace/processor.h"
#include "opentelemetry/sdk/trace/tracer_provider_factory.h"
#include "opentelemetry/trace/provider.h"

#include "common/proto/pb/message_pb.h"
#include "common/proto/pb/posix_pb.h"
#include "common/status/status.h"
#include "common/trace/trace_manager.h"
#include "common/utils/struct_transfer.h"

namespace functionsystem::test {
using namespace local_scheduler;

namespace {

// Field-by-field assignment (not C++20 designated initializers): keeps the test
// compiling under the project's C++17 standard and immune to member-order
// changes in InstanceCtrlConfig.
InstanceCtrlConfig FrontendCreateTraceConfig()
{
    InstanceCtrlConfig config;
    config.maxInstanceRedeployTimes = 2;
    config.waitStatusCodeUpdateMs = 500;
    config.minDeployIntervalMs = 100;
    config.maxDeployIntervalMs = 101;
    return config;
}

std::shared_ptr<messages::ScheduleRequest> BuildFrontendCreateScheduleReq(const std::string &requestID,
                                                                           const std::string &instanceID,
                                                                           bool withPolicy)
{
    auto scheduleReq = std::make_shared<messages::ScheduleRequest>();
    scheduleReq->set_requestid(requestID);
    scheduleReq->set_traceid("trace-" + requestID);
    auto instance = scheduleReq->mutable_instance();
    instance->set_instanceid(instanceID);
    instance->set_function("default/0-system-faasExecutorPython3.11/$latest");
    (*instance->mutable_createoptions())[CREATE_SOURCE] = FRONTEND_STR;
    (*instance->mutable_createoptions())["tenantId"] = "tenant-a";
    if (withPolicy) {
        (*instance->mutable_createoptions())[CREATE_ERROR_POLICY_KEY] = CREATE_ERROR_POLICY_LAST_FAILURE;
    }
    return scheduleReq;
}

litebus::Future<CallResultAck> NoopReadyCallback(const std::shared_ptr<functionsystem::CallResult> &)
{
    return litebus::Future<CallResultAck>(CallResultAck());
}

}  // namespace

class FrontendCreateTraceTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        // Save the process-wide tracing state; restore it in TearDown so the
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
};

TEST_F(FrontendCreateTraceTest, RecordFrontendCreateRetryAccumulatesAndKeepsFirstFailure)
{
    auto actor = std::make_shared<InstanceCtrlActor>("InstanceCtrlActor", "nodeID", FrontendCreateTraceConfig());
    auto scheduleReq = BuildFrontendCreateScheduleReq("retry-request", "retry-instance", true);
    ASSERT_TRUE(actor->RegisterFrontendReadyTicket(scheduleReq, NoopReadyCallback));

    // A non-zero first code pins down that firstFailureCode is really recorded
    // (a zero code would be indistinguishable from the field's default value).
    actor->RecordFrontendCreateRetry("retry-request", 111, "connect runtime failed", scheduleReq->instance());
    actor->RecordFrontendCreateRetry("retry-request", 500, "check readiness failed", scheduleReq->instance());

    auto snapshot = actor->TakeFrontendCreateFailureSnapshot("retry-request");
    // Only retries so far — no deploy failure recorded.
    EXPECT_FALSE(snapshot.present);
    EXPECT_EQ(snapshot.retriedTimes, 2U);
    EXPECT_EQ(snapshot.firstFailureCode, 111);
    EXPECT_EQ(snapshot.firstFailureMessage, "connect runtime failed");
    // Take consumes the entry: a second take finds nothing.
    auto consumed = actor->TakeFrontendCreateFailureSnapshot("retry-request");
    EXPECT_FALSE(consumed.present);
    EXPECT_EQ(consumed.retriedTimes, 0U);
}

TEST_F(FrontendCreateTraceTest, RecordFrontendCreateFailurePreservesRetryBookkeeping)
{
    auto actor = std::make_shared<InstanceCtrlActor>("InstanceCtrlActor", "nodeID", FrontendCreateTraceConfig());
    auto scheduleReq = BuildFrontendCreateScheduleReq("fail-request", "fail-instance", true);
    ASSERT_TRUE(actor->RegisterFrontendReadyTicket(scheduleReq, NoopReadyCallback));

    actor->RecordFrontendCreateRetry("fail-request", 111, "connect runtime failed", scheduleReq->instance());
    actor->RecordFrontendCreateFailure("fail-request", 12345, "deploy failed", "inst-1", scheduleReq->instance());

    auto snapshot = actor->TakeFrontendCreateFailureSnapshot("fail-request");
    EXPECT_TRUE(snapshot.present);
    EXPECT_EQ(snapshot.code, 12345);
    EXPECT_EQ(snapshot.message, "deploy failed");
    EXPECT_EQ(snapshot.instanceID, "inst-1");
    // The retry bookkeeping accumulated before the failure survives the overwrite.
    EXPECT_EQ(snapshot.retriedTimes, 1U);
    EXPECT_EQ(snapshot.firstFailureCode, 111);
    EXPECT_EQ(snapshot.firstFailureMessage, "connect runtime failed");
}

TEST_F(FrontendCreateTraceTest, RecordFrontendCreateRetryRequiresPendingTicket)
{
    auto actor = std::make_shared<InstanceCtrlActor>("InstanceCtrlActor", "nodeID", FrontendCreateTraceConfig());
    auto scheduleReq = BuildFrontendCreateScheduleReq("no-ticket-request", "no-ticket-instance", true);

    // No ready ticket registered for this requestID: the record is skipped.
    actor->RecordFrontendCreateRetry("no-ticket-request", 0, "connect runtime failed", scheduleReq->instance());

    auto snapshot = actor->TakeFrontendCreateFailureSnapshot("no-ticket-request");
    EXPECT_FALSE(snapshot.present);
    EXPECT_EQ(snapshot.retriedTimes, 0U);
    // Assert the retry evidence was not recorded at all (not just the counter).
    EXPECT_TRUE(snapshot.firstFailureMessage.empty());
    EXPECT_EQ(snapshot.firstFailureCode, 0);
}

TEST_F(FrontendCreateTraceTest, RecordFrontendCreateRetryRequiresLastFailurePolicy)
{
    auto actor = std::make_shared<InstanceCtrlActor>("InstanceCtrlActor", "nodeID", FrontendCreateTraceConfig());
    // No create_error_policy=last_failure_on_timeout in createOptions.
    auto scheduleReq = BuildFrontendCreateScheduleReq("no-policy-request", "no-policy-instance", false);
    ASSERT_TRUE(actor->RegisterFrontendReadyTicket(scheduleReq, NoopReadyCallback));

    actor->RecordFrontendCreateRetry("no-policy-request", 0, "connect runtime failed", scheduleReq->instance());

    auto snapshot = actor->TakeFrontendCreateFailureSnapshot("no-policy-request");
    EXPECT_FALSE(snapshot.present);
    EXPECT_EQ(snapshot.retriedTimes, 0U);
    // Assert the retry evidence was not recorded at all (not just the counter).
    EXPECT_TRUE(snapshot.firstFailureMessage.empty());
    EXPECT_EQ(snapshot.firstFailureCode, 0);
}

// Positive control for the fixture itself: with enableTrace_ on and a real
// provider installed, StartSpanWithRecord must land a span in the store.
// Without this, every negative assertion in this file (span NOT started /
// NOT present) would stay green even if the whole tracing path were dead.
TEST_F(FrontendCreateTraceTest, TracingFixtureStartsSpansWhenEnabled)
{
    trace::TraceManager::SpanParam param;
    param.spanName = trace::SpanName::K_CREATE;
    param.spanKey = "fixture-sanity-request";
    param.traceID = "trace-fixture-sanity";
    (void)trace::TraceManager::GetInstance().StartSpanWithRecord(std::move(param));
    EXPECT_FALSE(trace::TraceManager::GetInstance()
                     .GetSpanIDFromStore("fixture-sanity-request", trace::SpanName::K_CREATE)
                     .empty())
        << "fixture sanity: spans are not being recorded — all negative assertions below are vacuous";
}

// D4 span ownership: when the ready ticket for a requestID is already pending,
// ScheduleFrontendAndWaitReady rejects the duplicate without starting a span
// (the spanMap entry for that requestID belongs to the original request) and
// without writing the registered-ticket marker. A span is seeded under the
// same requestID first (standing in for the original request's live span) so
// the duplicate can also be shown NOT to stop or overwrite it. The manual
// ticket registration below stands in for the original request —
// RegisterFrontendReadyTicket itself never writes the marker or starts a span;
// only the successful path of ScheduleFrontendAndWaitReady does. (Driving the
// original request through the full ScheduleFrontendAndWaitReady would need a
// complete scheduling environment — an actor-level integration test.)
TEST_F(FrontendCreateTraceTest, DuplicateFrontendCreateDoesNotStartSpan)
{
    auto actor = std::make_shared<InstanceCtrlActor>("InstanceCtrlActor", "nodeID", FrontendCreateTraceConfig());
    // The original request owns the pending ticket; the duplicate is a SEPARATE
    // proto object carrying the same requestID, exactly like a second gRPC
    // request would arrive. Sharing one object would conflate the two requests'
    // extension state and could not detect a marker written onto the duplicate.
    auto originalReq = BuildFrontendCreateScheduleReq("dup-request", "dup-instance", true);
    ASSERT_TRUE(actor->RegisterFrontendReadyTicket(originalReq, NoopReadyCallback));
    auto duplicateReq = BuildFrontendCreateScheduleReq("dup-request", "dup-instance", true);

    // Seed the original request's span so the duplicate's non-interference is
    // observable (not just the absence of a new span).
    trace::TraceManager::SpanParam seeded;
    seeded.spanName = trace::SpanName::K_CREATE;
    seeded.spanKey = "dup-request";
    seeded.traceID = "trace-dup-request";
    (void)trace::TraceManager::GetInstance().StartSpanWithRecord(std::move(seeded));
    const std::string originalSpanID =
        trace::TraceManager::GetInstance().GetSpanIDFromStore("dup-request", trace::SpanName::K_CREATE);
    ASSERT_FALSE(originalSpanID.empty());

    auto runtimePromise = std::make_shared<litebus::Promise<messages::ScheduleResponse>>();
    auto future = actor->ScheduleFrontendAndWaitReady(duplicateReq, runtimePromise, NoopReadyCallback);
    // Generous timeout: on a loaded CI host the actor round-trip can exceed 1s.
    auto response = future.Get(5000);
    ASSERT_TRUE(response.IsSome());
    EXPECT_EQ(response.Get().code(), static_cast<int32_t>(StatusCode::ERR_PARAM_INVALID));
    EXPECT_NE(response.Get().message().find("already pending"), std::string::npos);

    // No second yr.create span was started, and the original request's span
    // entry is untouched (same spanID, still present).
    EXPECT_EQ(trace::TraceManager::GetInstance().GetSpanIDFromStore("dup-request", trace::SpanName::K_CREATE),
              originalSpanID);
    // The marker is only written on successful registration (and the rejection
    // above returned before the write), so the duplicate's own extensions must
    // still be free of it — this is the object the dispatcher would read.
    EXPECT_EQ(duplicateReq->instance().extensions().find(FRONTEND_READY_TICKET_REGISTERED),
              duplicateReq->instance().extensions().end());
}

}  // namespace functionsystem::test
