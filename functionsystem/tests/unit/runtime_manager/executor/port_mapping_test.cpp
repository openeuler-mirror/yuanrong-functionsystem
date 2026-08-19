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
#include "runtime_manager/utils/utils.h"

namespace functionsystem::runtime_manager {
namespace {

TEST(PortMappingTest, ParseForwardPorts_ValidJson)
{
    auto configs = ParseForwardPorts(
        R"({"portForwardings": [{"port": 8888, "protocol": "TCP"}, {"port": 443, "protocol": "TCP"}]})");
    ASSERT_EQ(2u, configs.size());
    EXPECT_EQ(8888u, configs[0].containerPort);
    EXPECT_EQ("tcp", configs[0].protocol);  // Should be lowercase
    EXPECT_EQ(443u, configs[1].containerPort);
    EXPECT_EQ("tcp", configs[1].protocol);
}

TEST(PortMappingTest, ParseForwardPorts_EmptyString)
{
    auto configs = ParseForwardPorts("");
    EXPECT_EQ(0u, configs.size());
}

TEST(PortMappingTest, ParseForwardPorts_InvalidJson)
{
    auto configs = ParseForwardPorts("not-json");
    EXPECT_EQ(0u, configs.size());
}

TEST(PortMappingTest, ParseForwardPorts_NoPortForwardingsKey)
{
    auto configs = ParseForwardPorts(R"({"forward": [{"port": 8888}]})");
    EXPECT_EQ(0u, configs.size());
}

TEST(PortMappingTest, ParseForwardPorts_EmptyArray)
{
    auto configs = ParseForwardPorts(R"({"portForwardings": []})");
    EXPECT_EQ(0u, configs.size());
}

TEST(PortMappingTest, ParseForwardPorts_SinglePort)
{
    auto configs = ParseForwardPorts(R"({"portForwardings": [{"port": 8080, "protocol": "TCP"}]})");
    ASSERT_EQ(1u, configs.size());
    EXPECT_EQ(8080u, configs[0].containerPort);
    EXPECT_EQ("tcp", configs[0].protocol);
}

TEST(PortMappingTest, ParseForwardPorts_FilterOutOfRangePorts)
{
    // Out-of-range ports (0, 65536) are skipped; only port 80 remains.
    auto configs = ParseForwardPorts(
        R"({"portForwardings": [{"port": 0, "protocol": "TCP"}, {"port": 80, "protocol": "UDP"}, {"port": 65536, "protocol": "TCP"}]})");
    ASSERT_EQ(1u, configs.size());
    EXPECT_EQ(80u, configs[0].containerPort);
    EXPECT_EQ("udp", configs[0].protocol);
}

TEST(PortMappingTest, ParseForwardPorts_PassthroughUnknownProtocol)
{
    // Unknown protocols are not validated here: they are passed through verbatim
    // (lowercased) so the daemon that binds the ports can reject them. Only the
    // L7 schemes (http/https/ws/wss) are mapped to "tcp".
    auto configs = ParseForwardPorts(
        R"({"portForwardings": [{"port": 8080, "protocol": "FOO"}, {"port": 8443, "protocol": "HTTPS"}]})");
    ASSERT_EQ(2u, configs.size());
    EXPECT_EQ(8080u, configs[0].containerPort);
    EXPECT_EQ("foo", configs[0].protocol);
    EXPECT_EQ(8443u, configs[1].containerPort);
    EXPECT_EQ("tcp", configs[1].protocol);  // https maps to tcp
}

TEST(PortMappingTest, ParseForwardPorts_NotArrayValue)
{
    auto configs = ParseForwardPorts(R"({"portForwardings": "8888"})");
    EXPECT_EQ(0u, configs.size());
}

TEST(PortMappingTest, ParseForwardPorts_MissingPortField)
{
    // Entry without "port" key should be skipped
    auto configs =
        ParseForwardPorts(R"({"portForwardings": [{"protocol": "TCP"}, {"port": 9090, "protocol": "UDP"}]})");
    ASSERT_EQ(1u, configs.size());
    EXPECT_EQ(9090u, configs[0].containerPort);
    EXPECT_EQ("udp", configs[0].protocol);
}

TEST(PortMappingTest, ParseForwardPorts_UDPProtocol)
{
    auto configs = ParseForwardPorts(R"({"portForwardings": [{"port": 53, "protocol": "UDP"}]})");
    ASSERT_EQ(1u, configs.size());
    EXPECT_EQ(53u, configs[0].containerPort);
    EXPECT_EQ("udp", configs[0].protocol);  // Should be lowercase
}

TEST(PortMappingTest, ParseForwardPorts_DefaultProtocol)
{
    // When protocol is missing, default to "tcp"
    auto configs = ParseForwardPorts(R"({"portForwardings": [{"port": 8080}]})");
    ASSERT_EQ(1u, configs.size());
    EXPECT_EQ(8080u, configs[0].containerPort);
    EXPECT_EQ("tcp", configs[0].protocol);  // Default to tcp
}

TEST(PortMappingTest, HasInvalidPortForwardings_EmptyInput)
{
    EXPECT_FALSE(HasInvalidPortForwardings(""));
}

TEST(PortMappingTest, HasInvalidPortForwardings_NoArray)
{
    EXPECT_FALSE(HasInvalidPortForwardings(R"({"forward": [{"port": 8888}]})"));
}

TEST(PortMappingTest, HasInvalidPortForwardings_EmptyArray)
{
    EXPECT_FALSE(HasInvalidPortForwardings(R"({"portForwardings": []})"));
}

TEST(PortMappingTest, HasInvalidPortForwardings_AllValid)
{
    EXPECT_FALSE(HasInvalidPortForwardings(
        R"({"portForwardings": [{"port": 8080, "protocol": "tcp"}, {"port": 443, "protocol": "udp"}]})"));
}

TEST(PortMappingTest, HasInvalidPortForwardings_AllOutOfRange)
{
    // Non-empty array with no valid entry (all ports out of range) -> invalid.
    EXPECT_TRUE(HasInvalidPortForwardings(
        R"({"portForwardings": [{"port": 0, "protocol": "tcp"}, {"port": 65536, "protocol": "tcp"}]})"));
}

TEST(PortMappingTest, HasInvalidPortForwardings_NegativePort)
{
    // Negative port is non-unsigned and gets skipped -> array non-empty with no valid entry.
    EXPECT_TRUE(HasInvalidPortForwardings(R"({"portForwardings": [{"port": -1, "protocol": "tcp"}]})"));
}

TEST(PortMappingTest, HasInvalidPortForwardings_PartiallyInvalid)
{
    // Strict: any invalid entry (99999 out of range) -> reject even if others valid.
    EXPECT_TRUE(HasInvalidPortForwardings(
        R"({"portForwardings": [{"port": 8080, "protocol": "tcp"}, {"port": 99999, "protocol": "tcp"}]})"));
}

}  // namespace
}  // namespace functionsystem::runtime_manager
