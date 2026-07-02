/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "runtime_manager/executor/sandboxd/sandboxd_request_builder.h"
#include "runtime_manager/config/command_builder.h"
#include "runtime_manager/config/build.h"
#include "common/proto/pb/message_pb.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace functionsystem::runtime_manager;

namespace functionsystem::test {

// ── SandboxdRequestBuilder tests (flat SandboxService StartRequest) ───────────

class SandboxdRequestBuilderTest : public ::testing::Test {
public:
    void SetUp() override
    {
        RuntimeConfig config;
        config.runtimePath       = "/opt/runtime";
        config.runtimeLogLevel   = "INFO";
        config.runtimeConfigPath = "/etc/runtime";
        config.runtimeLogPath    = "/var/log/runtime";
        config.hostIP            = "127.0.0.1";

        cmdBuilder_ = std::make_unique<CommandBuilder>(/*execLookPath=*/false);
        cmdBuilder_->SetRuntimeConfig(config);
        builder_    = std::make_unique<SandboxdRequestBuilder>(*cmdBuilder_);
    }

    void TearDown() override {}

    // Minimal params with no CONTAINER_ROOTFS -> Build succeeds against the
    // container config; flat request should carry sandbox_id/runtime/rootfs.
    SandboxdStartParams MakeMinimalParams() const
    {
        auto req = std::make_shared<messages::StartInstanceRequest>();
        req->mutable_runtimeinstanceinfo()->set_instanceid("test-instance");
        req->mutable_runtimeinstanceinfo()->set_runtimeid("test-runtime");
        req->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->set_language("python3.9");
        req->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->set_deploydir("/dcache");
        req->mutable_runtimeinstanceinfo()->set_traceid("trace-abc");
        req->mutable_runtimeinstanceinfo()->mutable_container()->set_id("container-001");
        req->mutable_runtimeinstanceinfo()->mutable_container()->set_runtime("runsc");

        SandboxdStartParams params;
        params.request   = req;
        params.runtimeID = "test-runtime";
        return params;
    }

    std::unique_ptr<CommandBuilder>         cmdBuilder_;
    std::unique_ptr<SandboxdRequestBuilder> builder_;
};

// Build succeeds and returns a flat SandboxStartRequest.
TEST_F(SandboxdRequestBuilderTest, BuildReturnsFlatStartRequest)
{
    auto params = MakeMinimalParams();
    auto [status, startReq] = builder_->Build(params);

    EXPECT_TRUE(status.IsOk());
    ASSERT_NE(startReq, nullptr);
}

// sandbox_id is left empty: sandboxd generates it and returns it in StartResponse.id.
TEST_F(SandboxdRequestBuilderTest, FlatRequestLeavesSandboxIdEmpty)
{
    auto params = MakeMinimalParams();
    auto [status, startReq] = builder_->Build(params);

    ASSERT_TRUE(status.IsOk());
    ASSERT_NE(startReq, nullptr);
    EXPECT_TRUE(startReq->sandbox_id().empty());
    EXPECT_EQ(startReq->runtime(), "runsc");
}

// trace_id is the distributed trace ID from the upstream request (not runtimeID).
TEST_F(SandboxdRequestBuilderTest, FlatRequestHasTraceId)
{
    auto params = MakeMinimalParams();
    auto [status, startReq] = builder_->Build(params);

    ASSERT_TRUE(status.IsOk());
    ASSERT_NE(startReq, nullptr);
    EXPECT_EQ(startReq->trace_id(), "trace-abc");
}

// The single envs map carries YR_LANGUAGE derived from the runtime config.
TEST_F(SandboxdRequestBuilderTest, FlatRequestEnvsCarriesLanguage)
{
    auto params = MakeMinimalParams();
    auto [status, startReq] = builder_->Build(params);

    ASSERT_TRUE(status.IsOk());
    ASSERT_NE(startReq, nullptr);
    EXPECT_EQ(startReq->envs().at("YR_LANGUAGE"), "python3.9");
}

// stdout/stderr log paths are resolved onto the flat request.
TEST_F(SandboxdRequestBuilderTest, FlatRequestHasLogPaths)
{
    auto params = MakeMinimalParams();
    auto [status, startReq] = builder_->Build(params);

    ASSERT_TRUE(status.IsOk());
    ASSERT_NE(startReq, nullptr);
    EXPECT_FALSE(startReq->stdout().empty());
    EXPECT_FALSE(startReq->stderr().empty());
}

}  // namespace functionsystem::test
