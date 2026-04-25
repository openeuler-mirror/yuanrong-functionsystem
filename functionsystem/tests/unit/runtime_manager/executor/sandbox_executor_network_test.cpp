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
#include "runtime_manager/executor/sandbox/sandbox_executor.h"

namespace functionsystem::runtime_manager {
namespace {

TEST(SandboxExecutorNetworkTest, ParseForwardPorts_ValidJson)
{
    auto configs = SandboxExecutor::ParseForwardPorts(
        R"({"portForwardings": [{"port": 8888, "protocol": "TCP"}, {"port": 443, "protocol": "TCP"}]})");
    ASSERT_EQ(2u, configs.size());
    EXPECT_EQ(8888u, configs[0].containerPort);
    EXPECT_EQ("tcp", configs[0].protocol);
    EXPECT_EQ(443u, configs[1].containerPort);
    EXPECT_EQ("tcp", configs[1].protocol);
}

TEST(SandboxExecutorNetworkTest, ParseForwardPorts_EmptyString)
{
    auto configs = SandboxExecutor::ParseForwardPorts("");
    EXPECT_EQ(0u, configs.size());
}

TEST(SandboxExecutorNetworkTest, ParseForwardPorts_InvalidJson)
{
    auto configs = SandboxExecutor::ParseForwardPorts("not-json");
    EXPECT_EQ(0u, configs.size());
}

TEST(SandboxExecutorNetworkTest, ParseForwardPorts_NoPortForwardingsKey)
{
    auto configs = SandboxExecutor::ParseForwardPorts(R"({"forward": [{"port": 8888}]})");
    EXPECT_EQ(0u, configs.size());
}

TEST(SandboxExecutorNetworkTest, ParseForwardPorts_EmptyArray)
{
    auto configs = SandboxExecutor::ParseForwardPorts(R"({"portForwardings": []})");
    EXPECT_EQ(0u, configs.size());
}

TEST(SandboxExecutorNetworkTest, ParseForwardPorts_SinglePort)
{
    auto configs = SandboxExecutor::ParseForwardPorts(
        R"({"portForwardings": [{"port": 8080, "protocol": "TCP"}]})");
    ASSERT_EQ(1u, configs.size());
    EXPECT_EQ(8080u, configs[0].containerPort);
    EXPECT_EQ("tcp", configs[0].protocol);
}

TEST(SandboxExecutorNetworkTest, ParseForwardPorts_FilterInvalidPortValues)
{
    auto configs = SandboxExecutor::ParseForwardPorts(
        R"({"portForwardings": [{"port": 0, "protocol": "TCP"}, {"port": 80, "protocol": "UDP"}, {"port": 65536, "protocol": "TCP"}]})");
    ASSERT_EQ(1u, configs.size());
    EXPECT_EQ(80u, configs[0].containerPort);
    EXPECT_EQ("udp", configs[0].protocol);
}

TEST(SandboxExecutorNetworkTest, ParseForwardPorts_NotArrayValue)
{
    auto configs = SandboxExecutor::ParseForwardPorts(R"({"portForwardings": "8888"})");
    EXPECT_EQ(0u, configs.size());
}

TEST(SandboxExecutorNetworkTest, ParseForwardPorts_MissingPortField)
{
    auto configs = SandboxExecutor::ParseForwardPorts(
        R"({"portForwardings": [{"protocol": "TCP"}, {"port": 9090, "protocol": "UDP"}]})");
    ASSERT_EQ(1u, configs.size());
    EXPECT_EQ(9090u, configs[0].containerPort);
    EXPECT_EQ("udp", configs[0].protocol);
}

TEST(SandboxExecutorNetworkTest, ParseForwardPorts_UDPProtocol)
{
    auto configs = SandboxExecutor::ParseForwardPorts(
        R"({"portForwardings": [{"port": 53, "protocol": "UDP"}]})");
    ASSERT_EQ(1u, configs.size());
    EXPECT_EQ(53u, configs[0].containerPort);
    EXPECT_EQ("udp", configs[0].protocol);
}

TEST(SandboxExecutorNetworkTest, ParseForwardPorts_DefaultProtocol)
{
    auto configs = SandboxExecutor::ParseForwardPorts(
        R"({"portForwardings": [{"port": 8080}]})");
    ASSERT_EQ(1u, configs.size());
    EXPECT_EQ(8080u, configs[0].containerPort);
    EXPECT_EQ("tcp", configs[0].protocol);
}

TEST(ContainerExecutorNetworkTest, PortMappingsJsonToEnvVarString_SingleEntry)
{
    // Simulate the JSON stored in runtime2portMappings_ → env var format
    nlohmann::json portJson = nlohmann::json::array();
    portJson.push_back("tcp:40001:8080");

    std::string pfStr;
    for (size_t i = 0; i < portJson.size(); ++i) {
        if (i > 0) pfStr += ";";
        pfStr += portJson[i].get<std::string>();
    }
    EXPECT_EQ("tcp:40001:8080", pfStr);
}

TEST(ContainerExecutorNetworkTest, PortMappingsJsonToEnvVarString_MultipleEntries)
{
    nlohmann::json portJson = nlohmann::json::array();
    portJson.push_back("tcp:40001:8080");
    portJson.push_back("tcp:40002:9090");
    portJson.push_back("https:40003:443");

    std::string pfStr;
    for (size_t i = 0; i < portJson.size(); ++i) {
        if (i > 0) pfStr += ";";
        pfStr += portJson[i].get<std::string>();
    }
    EXPECT_EQ("tcp:40001:8080;tcp:40002:9090;https:40003:443", pfStr);
}

TEST(ContainerExecutorNetworkTest, PortMappingsJsonToEnvVarString_EmptyArray)
{
    nlohmann::json portJson = nlohmann::json::array();

    std::string pfStr;
    for (size_t i = 0; i < portJson.size(); ++i) {
        if (i > 0) pfStr += ";";
        pfStr += portJson[i].get<std::string>();
    }
    EXPECT_EQ("", pfStr);
}

}  // namespace
}  // namespace functionsystem::runtime_manager
