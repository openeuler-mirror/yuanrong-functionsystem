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

#include "function_agent/driver/function_agent_driver.h"

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>

#include "function_agent/flags/function_agent_flags.h"
#include "runtime_manager/config/flags.h"
#include "common/utils/module_switcher.h"
#include "common/logs/logging.h"

using namespace functionsystem::function_agent;
using namespace functionsystem::runtime_manager;

namespace functionsystem::test {

class FunctionAgentDriverTest : public ::testing::Test {
public:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }

    FunctionAgentStartParam CreateBasicStartParam()
    {
        FunctionAgentStartParam param{};
        param.ip = "127.0.0.1";
        param.localSchedulerAddress = "127.0.0.1:5600";
        param.nodeID = "test_node";
        param.alias = "test_alias";
        param.modelName = "function_agent";
        param.agentPort = "500";
        param.decryptAlgorithm = "NO_CRYPTO";
        param.s3Enable = false;
        param.heartbeatTimeoutMs = 30000;
        param.agentUid = "test_agent_uid";
        param.localNodeID = "";
        param.enableSignatureValidation = false;
        param.componentName = "function_agent";
        param.enableMergeProcess = false;
        param.runtimeManagerFlags = nullptr;
        return param;
    }

    std::shared_ptr<runtime_manager::Flags> CreateRuntimeManagerFlags()
    {
        auto flags = std::make_shared<runtime_manager::Flags>();
        const char *argv[] = {
            "/runtime_manager",
            "--node_id=test_node",
            "--ip=127.0.0.1",
            "--port=6000",
            "--agent_address=127.0.0.1:500",
            "--runtime_dir=/tmp",
            "--runtime_home_dir=/tmp",
            "--runtime_logs_dir=/tmp",
            "--runtime_ld_library_path=/tmp",
            R"(--log_config={"filepath": "/tmp/home/yr/log", "level": "DEBUG", "rolling": {"maxsize": 100, "maxfiles": 1},"alsologtostderr":true})"
        };
        flags->ParseFlags(10, argv, true);
        return flags;
    }
};

TEST_F(FunctionAgentDriverTest, DriverTest)
{
    functionsystem::function_agent::FunctionAgentFlags flags;
    const char *argv[] = {
        "/function_agent",
        "--node_id=node1",
        "--ip=127.0.0.1",
        "--host_ip=127.0.0.1",
        "--port=32233",
        "--agent_listen_port=500",
        "--local_scheduler_address=127.0.0.1:5600",
        "--access_key=",
        "--secret_key=",
        "--s3_endpoint=",
        R"(--log_config={"filepath": "/tmp/home/yr/log", "level": "DEBUG", "rolling": {"maxsize": 100, "maxfiles": 1},"alsologtostderr":true})"
    };
    flags.ParseFlags(11, argv, true);
    EXPECT_EQ(flags.GetIP(), "127.0.0.1");
    EXPECT_EQ(flags.GetAgentListenPort(), "500");

    FunctionAgentDriver driver("node1", {});
    EXPECT_EQ(driver.Start(), Status::OK());
    EXPECT_EQ(driver.Stop(), Status::OK());
    driver.Await();
}

TEST_F(FunctionAgentDriverTest, MergeProcessMode_StartSuccess)
{
    auto param = CreateBasicStartParam();
    param.enableMergeProcess = true;
    param.runtimeManagerFlags = CreateRuntimeManagerFlags();

    FunctionAgentDriver driver("test_node", param);
    Status status = driver.Start();
    EXPECT_EQ(status, Status::OK());

    Status stopStatus = driver.Stop();
    EXPECT_EQ(stopStatus, Status::OK());
    driver.Await();
}

TEST_F(FunctionAgentDriverTest, MergeProcessMode_WithoutRuntimeManagerFlags)
{
    auto param = CreateBasicStartParam();
    param.enableMergeProcess = true;
    param.runtimeManagerFlags = nullptr;

    FunctionAgentDriver driver("test_node", param);
    Status status = driver.Start();
    EXPECT_EQ(status, Status::OK());

    driver.Stop();
    driver.Await();
}

TEST_F(FunctionAgentDriverTest, MergeProcessModeGracefulShutdown)
{
    auto param = CreateBasicStartParam();
    param.enableMergeProcess = true;
    param.runtimeManagerFlags = CreateRuntimeManagerFlags();

    FunctionAgentDriver driver("test_node", param);
    ASSERT_EQ(driver.Start(), Status::OK());

    driver.GracefulShutdown();
    
    Status stopStatus = driver.Stop();
    EXPECT_EQ(stopStatus, Status::OK());

    driver.Await();
}

}  // namespace functionsystem::test
