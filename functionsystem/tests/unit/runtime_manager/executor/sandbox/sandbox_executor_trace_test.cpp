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

#include <gtest/gtest.h>

#include "common/trace/create_trace_helper.h"
#include "opentelemetry/sdk/trace/processor.h"
#define private public
#include "common/trace/trace_manager.h"
#undef private

namespace functionsystem::test {
namespace {

constexpr char TEST_TRACE_PARENT[] = "00-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-bbbbbbbbbbbbbbbb-01";
std::shared_ptr<messages::StartInstanceRequest> BuildStartInstanceRequest()
{
    auto request = std::make_shared<messages::StartInstanceRequest>();
    auto *info = request->mutable_runtimeinstanceinfo();
    info->set_traceid("job-trace-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    info->set_requestid("runtime-request");
    info->set_instanceid("instance-id");
    info->set_runtimeid("runtime-id");
    (*request->mutable_scheduleoption()->mutable_extension())["traceparent"] = TEST_TRACE_PARENT;
    return request;
}

}  // namespace

class SandboxExecutorTraceTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        trace::TraceManager::GetInstance().Clear();
        trace::TraceManager::GetInstance().enableTrace_ = true;
        trace::TraceManager::GetInstance().hostID_ = "node-b";
        auto provider = opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider>(
            opentelemetry::sdk::trace::TracerProviderFactory::Create(
                std::vector<std::unique_ptr<opentelemetry::sdk::trace::SpanProcessor>>{})
                .release());
        opentelemetry::trace::Provider::SetTracerProvider(provider);
    }

    void TearDown() override
    {
        trace::TraceManager::GetInstance().Clear();
        trace::TraceManager::GetInstance().enableTrace_ = false;
        trace::TraceManager::GetInstance().hostID_.clear();
        opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider> none;
        opentelemetry::trace::Provider::SetTracerProvider(none);
    }
};

TEST_F(SandboxExecutorTraceTest, StartSandboxCreateSpanPropagatesChildTraceparent)
{
    auto request = BuildStartInstanceRequest();

    trace::StartSandboxCreateSpan(request);

    const auto spanId = trace::TraceManager::GetInstance().GetSpanIDFromStore(
        request->runtimeinstanceinfo().requestid(), trace::kCreateSandboxSpanName);
    ASSERT_FALSE(spanId.empty());

    const auto traceParent = trace::TraceManager::GetTraceParentFromOptions(
        request->scheduleoption().extension(),
        &request->runtimeinstanceinfo().deploymentconfig().deployoptions());
    EXPECT_FALSE(traceParent.empty());
    EXPECT_NE(traceParent, TEST_TRACE_PARENT);
    EXPECT_EQ(traceParent, request->scheduleoption().extension().at("traceparent"));
}

TEST_F(SandboxExecutorTraceTest, StopSandboxCreateSpanRemovesRecordedSpan)
{
    auto request = BuildStartInstanceRequest();
    runtime::v1::StartResponse response;
    response.set_code(static_cast<int32_t>(StatusCode::RUNTIME_MANAGER_CREATE_EXEC_FAILED));
    response.set_message("start sandbox failed");

    trace::StartSandboxCreateSpan(request);
    trace::StopSandboxCreateSpan(request, response);

    EXPECT_TRUE(trace::TraceManager::GetInstance()
                    .GetSpanIDFromStore(request->runtimeinstanceinfo().requestid(),
                                        trace::kCreateSandboxSpanName)
                    .empty());
}

}  // namespace functionsystem::test
