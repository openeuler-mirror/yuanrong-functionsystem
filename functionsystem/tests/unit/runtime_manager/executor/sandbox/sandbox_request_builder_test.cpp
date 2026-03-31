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

#include "runtime_manager/executor/sandbox/sandbox_request_builder.h"
#include "runtime_manager/config/command_builder.h"
#include "runtime_manager/config/build.h"
#include "common/proto/pb/message_pb.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace functionsystem::runtime_manager;

namespace functionsystem::test {

// ── T11: SandboxRequestBuilder tests ─────────────────────────────────────────

class SandboxRequestBuilderTest : public ::testing::Test {
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
        builder_    = std::make_unique<SandboxRequestBuilder>(*cmdBuilder_);
    }

    void TearDown() override {}

    // Create a minimal SandboxStartParams with no CONTAINER_ROOTFS → Build succeeds
    SandboxStartParams MakeMinimalParams(const std::string &checkpointID = "") const
    {
        auto req = std::make_shared<messages::StartInstanceRequest>();
        req->mutable_runtimeinstanceinfo()->set_instanceid("test-instance");
        req->mutable_runtimeinstanceinfo()->set_runtimeid("test-runtime");
        req->mutable_runtimeinstanceinfo()->mutable_runtimeconfig()->set_language("python3.9");
        req->mutable_runtimeinstanceinfo()->mutable_deploymentconfig()->set_deploydir("/dcache");

        SandboxStartParams params;
        params.request      = req;
        params.runtimeID    = "test-runtime";
        params.checkpointID = checkpointID;
        return params;
    }

    std::unique_ptr<CommandBuilder>        cmdBuilder_;
    std::unique_ptr<SandboxRequestBuilder> builder_;
};

// T11-1: Build with empty checkpointID → returns StartRequest variant (not RestoreRequest)
TEST_F(SandboxRequestBuilderTest, BuildWithEmptyCheckpointIdReturnsStartRequest)
{
    auto params = MakeMinimalParams(/*checkpointID=*/"");
    auto [status, proto] = builder_->Build(params);

    EXPECT_TRUE(status.IsOk());
    auto startReq = SandboxRequestBuilder::AsStart(proto);
    EXPECT_NE(startReq, nullptr);
}

// T11-2: Build with non-empty checkpointID → returns RestoreRequest variant
TEST_F(SandboxRequestBuilderTest, BuildWithNonEmptyCheckpointIdReturnsRestoreRequest)
{
    auto params = MakeMinimalParams(/*checkpointID=*/"ckpt-abc");
    auto [status, proto] = builder_->Build(params);

    EXPECT_TRUE(status.IsOk());
    auto restoreReq = SandboxRequestBuilder::AsRestore(proto);
    EXPECT_NE(restoreReq, nullptr);
}

// T11-3: AsStart on StartRequest variant → non-null
TEST_F(SandboxRequestBuilderTest, AsStartOnStartVariantIsNonNull)
{
    auto params = MakeMinimalParams();
    auto [status, proto] = builder_->Build(params);

    ASSERT_TRUE(status.IsOk());
    EXPECT_NE(SandboxRequestBuilder::AsStart(proto), nullptr);
}

// T11-4: AsRestore on RestoreRequest variant → non-null
TEST_F(SandboxRequestBuilderTest, AsRestoreOnRestoreVariantIsNonNull)
{
    auto params = MakeMinimalParams(/*checkpointID=*/"ckpt-xyz");
    auto [status, proto] = builder_->Build(params);

    ASSERT_TRUE(status.IsOk());
    EXPECT_NE(SandboxRequestBuilder::AsRestore(proto), nullptr);
}

// T11-5: Build with empty checkpointID → StartRequest has funcruntime set
TEST_F(SandboxRequestBuilderTest, BuildStartRequestHasFuncruntime)
{
    auto params = MakeMinimalParams();
    params.request->mutable_runtimeinstanceinfo()->mutable_container()->set_id("container-001");

    auto [status, proto] = builder_->Build(params);

    ASSERT_TRUE(status.IsOk());
    auto startReq = SandboxRequestBuilder::AsStart(proto);
    ASSERT_NE(startReq, nullptr);
    // funcruntime must be populated (id is copied from container id)
    EXPECT_EQ(startReq->funcruntime().id(), "container-001");
}

// T11-6: portMappings non-empty → StartRequest.ports contains entries
TEST_F(SandboxRequestBuilderTest, PortMappingsAppliedToStartRequest)
{
    auto params = MakeMinimalParams();
    params.portMappings = {"8080", "9090"};

    auto [status, proto] = builder_->Build(params);

    ASSERT_TRUE(status.IsOk());
    auto startReq = SandboxRequestBuilder::AsStart(proto);
    ASSERT_NE(startReq, nullptr);
    EXPECT_EQ(startReq->ports().size(), 2);
}

// T11-7 (corner case): Build with invalid CONTAINER_ROOTFS JSON → returns error Status
TEST_F(SandboxRequestBuilderTest, BuildWithInvalidRootfsJsonReturnsError)
{
    auto params = MakeMinimalParams();
    // Add invalid JSON to the CONTAINER_ROOTFS deploy option
    (*params.request->mutable_runtimeinstanceinfo()
         ->mutable_deploymentconfig()
         ->mutable_deployoptions())["CONTAINER_ROOTFS"] = "not-valid-json";

    auto [status, proto] = builder_->Build(params);

    EXPECT_FALSE(status.IsOk());
}

}  // namespace functionsystem::test
