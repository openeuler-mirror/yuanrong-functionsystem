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

#include "gtest/gtest.h"
#include "runtime_manager/executor/container_executor.h"

namespace functionsystem::runtime_manager {
namespace {

TEST(ContainerExecutorNetworkTest, ParseForwardPorts_ValidJson)
{
    auto configs = ContainerExecutor::ParseForwardPorts(
        R"({"portForwardings": [{"port": 8888, "protocol": "TCP"}, {"port": 443, "protocol": "TCP"}]})");
    ASSERT_EQ(2u, configs.size());
    EXPECT_EQ(8888u, configs[0].containerPort);
    EXPECT_EQ("tcp", configs[0].protocol);  // Should be lowercase
    EXPECT_EQ(443u, configs[1].containerPort);
    EXPECT_EQ("tcp", configs[1].protocol);
}

TEST(ContainerExecutorNetworkTest, ParseForwardPorts_EmptyString)
{
    auto configs = ContainerExecutor::ParseForwardPorts("");
    EXPECT_EQ(0u, configs.size());
}

TEST(ContainerExecutorNetworkTest, ParseForwardPorts_InvalidJson)
{
    auto configs = ContainerExecutor::ParseForwardPorts("not-json");
    EXPECT_EQ(0u, configs.size());
}

TEST(ContainerExecutorNetworkTest, ParseForwardPorts_NoPortForwardingsKey)
{
    auto configs = ContainerExecutor::ParseForwardPorts(R"({"forward": [{"port": 8888}]})");
    EXPECT_EQ(0u, configs.size());
}

TEST(ContainerExecutorNetworkTest, ParseForwardPorts_EmptyArray)
{
    auto configs = ContainerExecutor::ParseForwardPorts(R"({"portForwardings": []})");
    EXPECT_EQ(0u, configs.size());
}

TEST(ContainerExecutorNetworkTest, ParseForwardPorts_SinglePort)
{
    auto configs = ContainerExecutor::ParseForwardPorts(
        R"({"portForwardings": [{"port": 8080, "protocol": "TCP"}]})");
    ASSERT_EQ(1u, configs.size());
    EXPECT_EQ(8080u, configs[0].containerPort);
    EXPECT_EQ("tcp", configs[0].protocol);
}

TEST(ContainerExecutorNetworkTest, ParseForwardPorts_FilterInvalidPortValues)
{
    // Port 0 and 65536+ are invalid, only 80 is valid
    auto configs = ContainerExecutor::ParseForwardPorts(
        R"({"portForwardings": [{"port": 0, "protocol": "TCP"}, {"port": 80, "protocol": "UDP"}, {"port": 65536, "protocol": "TCP"}]})");
    ASSERT_EQ(1u, configs.size());
    EXPECT_EQ(80u, configs[0].containerPort);
    EXPECT_EQ("udp", configs[0].protocol);  // Should be lowercase
}

TEST(ContainerExecutorNetworkTest, ParseForwardPorts_NotArrayValue)
{
    auto configs = ContainerExecutor::ParseForwardPorts(R"({"portForwardings": "8888"})");
    EXPECT_EQ(0u, configs.size());
}

TEST(ContainerExecutorNetworkTest, ParseForwardPorts_MissingPortField)
{
    // Entry without "port" key should be skipped
    auto configs = ContainerExecutor::ParseForwardPorts(
        R"({"portForwardings": [{"protocol": "TCP"}, {"port": 9090, "protocol": "UDP"}]})");
    ASSERT_EQ(1u, configs.size());
    EXPECT_EQ(9090u, configs[0].containerPort);
    EXPECT_EQ("udp", configs[0].protocol);
}

TEST(ContainerExecutorNetworkTest, ParseForwardPorts_UDPProtocol)
{
    auto configs = ContainerExecutor::ParseForwardPorts(
        R"({"portForwardings": [{"port": 53, "protocol": "UDP"}]})");
    ASSERT_EQ(1u, configs.size());
    EXPECT_EQ(53u, configs[0].containerPort);
    EXPECT_EQ("udp", configs[0].protocol);  // Should be lowercase
}

TEST(ContainerExecutorNetworkTest, ParseForwardPorts_DefaultProtocol)
{
    // When protocol is missing, default to "tcp"
    auto configs = ContainerExecutor::ParseForwardPorts(
        R"({"portForwardings": [{"port": 8080}]})");
    ASSERT_EQ(1u, configs.size());
    EXPECT_EQ(8080u, configs[0].containerPort);
    EXPECT_EQ("tcp", configs[0].protocol);  // Default to tcp
}

}  // namespace
}  // namespace functionsystem::runtime_manager
