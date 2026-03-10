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

#define private public  // onle for test
#include "function_agent/driver/function_agent_driver.h"
#include "function_agent/flags/function_agent_flags.h"

using namespace functionsystem::function_agent;

namespace functionsystem::test {

class FunctionAgentDriverTest : public ::testing::Test {
public:
    void SetUp() override
    {
    }

    void TearDown() override
    {
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
        "--enable_hot_thresholds_config=true",
        "--code_package_thresholds_config_path=/tmp/download/config",
        R"(--log_config={"filepath": "/tmp/home/yr/log", "level": "DEBUG", "rolling": {"maxsize": 100, "maxfiles": 1},"alsologtostderr":true})"
    };
    flags.ParseFlags(13, argv, true);
    EXPECT_EQ(flags.GetIP(), "127.0.0.1");
    EXPECT_EQ(flags.GetAgentListenPort(), "500");
    EXPECT_TRUE(flags.GetEnableHotThresholdsCfg());
    EXPECT_STREQ(flags.GetCodePkgThresholdsCfgPath().c_str(), "/tmp/download/config");

    {
        FunctionAgentDriver driver("node1", {});
        EXPECT_EQ(driver.Start(), Status::OK());
        EXPECT_EQ(driver.Stop(), Status::OK());
        auto aid = driver.actor_->registerHelper_->actor_->GetAID();
        litebus::Terminate(aid);
        litebus::Await(aid);
        driver.Await();
    }
    {
        FunctionAgentStartParam param;
        param.enableHotThresholdsCfg = true;
        param.codePkgThresholdsCfgPath = "/tmp/download/config/tmp";
        FunctionAgentDriver driver("node2", param);
        if (litebus::os::ExistPath("/tmp/download/config/tmp")) {
            litebus::os::Rmdir("/tmp/download/config/tmp");
        }
        Status status = driver.Start();
        EXPECT_TRUE(status.IsError());
        EXPECT_STREQ(status.GetMessage().c_str(), "[failed to watch code package threshold config path event.]");
        auto aid = driver.actor_->registerHelper_->actor_->GetAID();
        litebus::Terminate(aid);
        litebus::Await(aid);
        driver.Await();
    }
}

}  // namespace functionsystem::test
