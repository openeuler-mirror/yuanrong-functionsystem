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

constexpr char TEST_TRACE_PARENT[] = "00-11111111111111111111111111111111-2222222222222222-01";
std::shared_ptr<messages::DeployInstanceRequest> BuildDeployInstanceRequest()
{
    auto request = std::make_shared<messages::DeployInstanceRequest>();
    request->set_traceid("job-trace-11111111111111111111111111111111");
    request->set_requestid("deploy-request");
    request->set_instanceid("instance-id");
    (*request->mutable_scheduleoption()->mutable_extension())["traceparent"] = TEST_TRACE_PARENT;
    return request;
}

}  // namespace

class AgentServiceTraceTest : public ::testing::Test {
protected:
    void SetUp() override
    {
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
        trace::TraceManager::GetInstance().enableTrace_ = false;
        trace::TraceManager::GetInstance().hostID_.clear();
        opentelemetry::nostd::shared_ptr<opentelemetry::trace::TracerProvider> none;
        opentelemetry::trace::Provider::SetTracerProvider(none);
    }
};

TEST_F(AgentServiceTraceTest, StartCodeDownloadSpanPropagatesChildTraceparent)
{
    auto request = BuildDeployInstanceRequest();

    trace::StartCodeDownloadSpan(request);

    const auto spanId = trace::TraceManager::GetInstance().GetSpanIDFromStore(
        request->requestid(), trace::kCodeDownloadSpanName);
    ASSERT_FALSE(spanId.empty());

    const auto traceParent = trace::TraceManager::GetTraceParentFromOptions(
        request->createoptions(), &request->scheduleoption().extension());
    EXPECT_FALSE(traceParent.empty());
    EXPECT_NE(traceParent, TEST_TRACE_PARENT);
    EXPECT_EQ(traceParent, request->scheduleoption().extension().at("traceparent"));
}

TEST_F(AgentServiceTraceTest, StopCodeDownloadSpanRemovesRecordedSpan)
{
    auto request = BuildDeployInstanceRequest();

    trace::StartCodeDownloadSpan(request);

    function_agent::DeployResult result;
    result.status = Status(StatusCode::FUNC_AGENT_OBS_GET_OBJECT_ERROR, "download failed");
    trace::StopCodeDownloadSpan(request, result);

    EXPECT_TRUE(trace::TraceManager::GetInstance()
                    .GetSpanIDFromStore(request->requestid(), trace::kCodeDownloadSpanName)
                    .empty());
}

}  // namespace functionsystem::test
