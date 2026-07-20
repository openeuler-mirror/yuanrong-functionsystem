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

#include "runtime_manager/executor/sandboxd/sandboxd_executor.h"
#include "common/status/status.h"

#include <gtest/gtest.h>

#include <string>

using namespace functionsystem::runtime_manager;

namespace functionsystem::test {

// ── SandboxdExecutor static helpers ───────────────────────────────────────────

TEST(SandboxdExecutorTest, ParseForwardPortsParsesPortForwardings)
{
    const std::string netJson = R"({"portForwardings":[{"port":8080},{"port":9090,"protocol":"TCP"}]})";
    auto configs = SandboxdExecutor::ParseForwardPorts(netJson);
    ASSERT_EQ(configs.size(), 2u);
    EXPECT_EQ(configs[0].containerPort, 8080u);
    EXPECT_EQ(configs[0].protocol, "tcp");
    EXPECT_EQ(configs[1].containerPort, 9090u);
    EXPECT_EQ(configs[1].protocol, "tcp");  // lowercased
}

TEST(SandboxdExecutorTest, ParseForwardPortsParsesRouteKinds)
{
    const std::string netJson = R"({"portForwardings":[)"
        R"({"port":50090,"protocol":"http","routeKind":"direct"},)"
        R"({"port":8765,"protocol":"http","routeKind":"tunnel"},)"
        R"({"port":8080,"protocol":"http"}]})";
    auto configs = SandboxdExecutor::ParseForwardPorts(netJson);
    ASSERT_EQ(configs.size(), 3);
    EXPECT_EQ(configs[0].routeKind, PortRouteKind::DIRECT);
    EXPECT_EQ(configs[1].routeKind, PortRouteKind::TUNNEL);
    EXPECT_EQ(configs[2].routeKind, PortRouteKind::PUBLIC);
}

TEST(SandboxdExecutorTest, ParseForwardPortsInvalidRouteKindIsSkipped)
{
    const std::string netJson = R"({"portForwardings":[)"
        R"({"port":8080,"routeKind":"unknown"},)"
        R"({"port":8081,"routeKind":42},)"
        R"({"port":9090,"routeKind":"public"}]})";
    const auto configs = SandboxdExecutor::ParseForwardPorts(netJson);
    ASSERT_EQ(configs.size(), 1u);
    EXPECT_EQ(configs[0].containerPort, 9090u);
    EXPECT_EQ(configs[0].routeKind, PortRouteKind::PUBLIC);
}

TEST(SandboxdExecutorTest, ParseForwardPortsEmptyOrInvalidReturnsEmpty)
{
    EXPECT_TRUE(SandboxdExecutor::ParseForwardPorts("").empty());
    EXPECT_TRUE(SandboxdExecutor::ParseForwardPorts("not-json").empty());
    EXPECT_TRUE(SandboxdExecutor::ParseForwardPorts(R"({"portForwardings":[]})").empty());
    // Out-of-range ports are skipped.
    auto configs = SandboxdExecutor::ParseForwardPorts(R"({"portForwardings":[{"port":0},{"port":70000}]})");
    EXPECT_TRUE(configs.empty());
}

TEST(SandboxdExecutorTest, IsRetryableWaitErrorClassifiesTransportErrors)
{
    EXPECT_TRUE(SandboxdExecutor::IsRetryableWaitError(Status(GRPC_UNAVAILABLE)));
    EXPECT_TRUE(SandboxdExecutor::IsRetryableWaitError(Status(GRPC_DEADLINE_EXCEEDED)));
    EXPECT_FALSE(SandboxdExecutor::IsRetryableWaitError(Status(GRPC_NOT_FOUND)));
    EXPECT_FALSE(SandboxdExecutor::IsRetryableWaitError(Status(SUCCESS)));
}

}  // namespace functionsystem::test
