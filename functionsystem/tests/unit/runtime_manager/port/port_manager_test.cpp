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

#include <set>

#include "gtest/gtest.h"
#include "runtime_manager/port/port_manager.h"
#include "utils/port_helper.h"

using namespace functionsystem::runtime_manager;

namespace functionsystem::test {

class DISABLED_PortManagerTest : public ::testing::Test {
public:
    void SetUp() override
    {
        PortManager::GetInstance().InitPortResource(333, 1000);
    }

    void TearDown() override
    {
    }
};

TEST_F(DISABLED_PortManagerTest, RequestPort)
{
    std::string runtimeID = "test_runtimeID";
    std::string port = PortManager::GetInstance().RequestPort(runtimeID);
    EXPECT_EQ("333", port);
}

TEST_F(DISABLED_PortManagerTest, GetPort)
{
    std::string runtimeID = "test_runtimeID";
    std::string port = PortManager::GetInstance().RequestPort(runtimeID);
    EXPECT_EQ("333", port);

    std::string otherRuntimeID = "test_runtimeID_01";
    std::string otherPort = PortManager::GetInstance().RequestPort(otherRuntimeID);
    EXPECT_EQ("334", otherPort);

    std::string resPort = PortManager::GetInstance().GetPort(otherRuntimeID);
    EXPECT_EQ("334", resPort);

    std::string unknownRuntimeID = "test_unknown_runtimeID";
    std::string unknownPort = PortManager::GetInstance().GetPort(unknownRuntimeID);
    EXPECT_EQ("", unknownPort);
}

TEST_F(DISABLED_PortManagerTest, ReleasePort)
{
    std::string runtimeID = "test_runtimeID";
    std::string port = PortManager::GetInstance().RequestPort(runtimeID);
    EXPECT_EQ("333", port);

    std::string resPort = PortManager::GetInstance().GetPort(runtimeID);
    EXPECT_EQ("333", resPort);

    int successRelease = PortManager::GetInstance().ReleasePort(runtimeID);
    EXPECT_EQ(0, successRelease);

    int failRelease = PortManager::GetInstance().ReleasePort(runtimeID);
    EXPECT_EQ(-1, failRelease);

    std::string emptyPort = PortManager::GetInstance().GetPort(runtimeID);
    EXPECT_EQ("", emptyPort);
}

TEST_F(DISABLED_PortManagerTest, ClearTest)
{
    std::string runtimeID = "test_runtimeID";
    std::string port = PortManager::GetInstance().RequestPort(runtimeID);
    EXPECT_EQ("333", port);

    PortManager::GetInstance().Clear();

    std::string emptyPort = PortManager::GetInstance().GetPort(runtimeID);
    EXPECT_EQ("", emptyPort);
}

TEST_F(DISABLED_PortManagerTest, CheckPortInuse)
{
    uint16_t port = GetPortEnv("LITEBUS_PORT", 8080);
    bool isInuse = PortManager::GetInstance().CheckPortInUse(port);
    EXPECT_EQ(isInuse, true);

    isInuse = PortManager::GetInstance().CheckPortInUse(7777);
    EXPECT_EQ(isInuse, false);
}

TEST_F(DISABLED_PortManagerTest, RequestPorts_Success)
{
    std::string runtimeID = "test_runtime_ports";
    auto ports = PortManager::GetInstance().RequestPorts(runtimeID, 3);
    EXPECT_EQ(3u, ports.size());
    // All ports should be distinct
    std::set<int> portSet(ports.begin(), ports.end());
    EXPECT_EQ(3u, portSet.size());
    // All allocated ports should be discoverable via GetPort
    EXPECT_NE("", PortManager::GetInstance().GetPort(runtimeID));
}

TEST_F(DISABLED_PortManagerTest, RequestPorts_ZeroCount)
{
    auto ports = PortManager::GetInstance().RequestPorts("test_runtime_zero", 0);
    EXPECT_EQ(0u, ports.size());
}

TEST_F(DISABLED_PortManagerTest, RequestPorts_InsufficientPorts)
{
    // Request more ports than available (poolSize=1000)
    auto ports = PortManager::GetInstance().RequestPorts("test_runtime_many", 2000);
    // Should fail completely (rollback), return empty
    EXPECT_EQ(0u, ports.size());
    // No partial allocation should remain
    EXPECT_EQ("", PortManager::GetInstance().GetPort("test_runtime_many"));
}

TEST_F(DISABLED_PortManagerTest, ReleasePorts_Success)
{
    std::string runtimeID = "test_runtime_release_all";
    auto ports = PortManager::GetInstance().RequestPorts(runtimeID, 3);
    EXPECT_EQ(3u, ports.size());
    // Verify ports are allocated
    EXPECT_NE("", PortManager::GetInstance().GetPort(runtimeID));

    // Release all ports
    PortManager::GetInstance().ReleasePorts(runtimeID);
    // Verify all ports are freed
    EXPECT_EQ("", PortManager::GetInstance().GetPort(runtimeID));
}

TEST_F(DISABLED_PortManagerTest, ReleasePorts_ReAlloc)
{
    std::string runtimeID = "test_runtime_realloc";
    auto ports = PortManager::GetInstance().RequestPorts(runtimeID, 2);
    EXPECT_EQ(2u, ports.size());

    PortManager::GetInstance().ReleasePorts(runtimeID);
    EXPECT_EQ("", PortManager::GetInstance().GetPort(runtimeID));

    // Should be able to re-allocate after release
    auto ports2 = PortManager::GetInstance().RequestPorts(runtimeID, 2);
    EXPECT_EQ(2u, ports2.size());
}
}