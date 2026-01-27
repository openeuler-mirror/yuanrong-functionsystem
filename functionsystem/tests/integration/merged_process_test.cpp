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
#include <memory>
#include <string>
#include <thread>
#include <chrono>
#include <csignal>
#include <unistd.h>
#include <iostream>

#include <async/async.hpp>
#include <exec/exec.hpp>
#include <utils/os_utils.hpp>

#include "common/logs/logging.h"
#include "common/constants/constants.h"
#include "utils/port_helper.h"
#include "utils.h"
#include "common/utils/exec_utils.h"

using namespace functionsystem;

namespace functionsystem::test {

class MergedProcessTest : public ::testing::Test {
public:
    void SetUp() override
    {
        auto outputPath = litebus::os::GetEnv("BIN_PATH");
        if (outputPath.IsNone()) {
            FAIL() << "BIN_PATH environment variable is not set";
        }
        
        functionAgentBinPath_ = outputPath.Get() + "/function_agent";
        runtimeManagerBinPath_ = outputPath.Get() + "/runtime_manager";
        functionProxyBinPath_ = outputPath.Get() + "/function_proxy";

        functionAgentPort_ = FindAvailablePort();
        runtimeManagerPort_ = FindAvailablePort();
        functionProxyPort_ = FindAvailablePort();
        functionProxyGrpcPort_ = FindAvailablePort();

        testRuntimeDir_ = "/tmp/test_merged_process_runtime";
        testLogDir_ = "/tmp/test_merged_process_logs";
        litebus::os::Mkdir(testRuntimeDir_);
        litebus::os::Mkdir(testLogDir_);
        
        litebus::os::SetEnv("YR_BARE_MENTAL", "1");
        
    }

    void TearDown() override
    {
        litebus::os::Rmdir(testRuntimeDir_);
        litebus::os::Rmdir(testLogDir_);
    }
    
    std::shared_ptr<litebus::Exec> StartFunctionAgent()
    {
        YRLOG_INFO("start function_agent");
        std::string logConfig = R"({"filepath": ")" + testLogDir_ + 
                               R"(", "level": "DEBUG", "rolling": {"maxsize": 100, "maxfiles": 1},"alsologtostderr":true})";
        
        const std::vector<std::string> args = {
            " ",
            "--node_id=test_merged_node",
            "--ip=127.0.0.1",
            "--host_ip=127.0.0.1",
            "--agent_listen_port=" + std::to_string(functionAgentPort_),
            "--local_scheduler_address=127.0.0.1:" + std::to_string(functionProxyPort_),
            "--access_key=",
            "--secret_key=",
            "--s3_endpoint=",
            "--log_config=" + logConfig
        };
        
        auto functionAgentProcess = CreateProcess(functionAgentBinPath_, args);
        
        if (functionAgentProcess.IsError()) {
            EXPECT_TRUE(false) << "Failed to create function_agent process";
            return nullptr;
        }
        
        return functionAgentProcess.Get();
    }
    
    std::shared_ptr<litebus::Exec> StartRuntimeManager(const std::string& agentAddress)
    {
        YRLOG_INFO("start runtime_manager");
        std::string logConfig = R"({"filepath": ")" + testLogDir_ + 
                               R"(", "level": "DEBUG", "rolling": {"maxsize": 100, "maxfiles": 1},"alsologtostderr":true})";
        
        const std::vector<std::string> args = {
            " ",
            "--node_id=test_merged_node",
            "--ip=127.0.0.1",
            "--host_ip=127.0.0.1",
            "--port=" + std::to_string(runtimeManagerPort_),
            "--agent_address=" + agentAddress,
            "--runtime_dir=" + testRuntimeDir_,
            "--runtime_home_dir=" + testRuntimeDir_,
            "--runtime_logs_dir=" + testLogDir_,
            "--runtime_ld_library_path=" + testRuntimeDir_,
            "--log_config=" + logConfig
        };
        
        auto runtimeProcess = CreateProcess(runtimeManagerBinPath_, args);
        if (runtimeProcess.IsError()) {
            EXPECT_TRUE(false) << "Failed to create runtime_manager process";
            return nullptr;
        }
        return runtimeProcess.Get();
    }
    
    std::shared_ptr<litebus::Exec> StartFunctionProxy(bool enableMergeProcess)
    {
        YRLOG_INFO("start function_proxy{}", enableMergeProcess ? " with merged process" : "");
        std::string logConfig = R"({"filepath": ")" + testLogDir_ +
                               R"(", "level": "DEBUG", "rolling": {"maxsize": 100, "maxfiles": 1},"alsologtostderr":true})";

        std::string agentAddress = "127.0.0.1:" + std::to_string(functionAgentPort_);
        std::string proxyAddress = "127.0.0.1:" + std::to_string(functionProxyPort_);
        std::string metaStoreAddress = "127.0.0.1:22770";  // 使用 global_scheduler_address 作为 meta_store_address

        std::vector<std::string> args = {
            " ",
            "--node_id=test_merged_node",
            "--ip=127.0.0.1",
            "--host_ip=127.0.0.1",
            "--address=" + proxyAddress,
            "--meta_store_address=" + metaStoreAddress,
            "--etcd_address=" + metaStoreAddress,  // etcd_address 也可能需要
            "--grpc_listen_port=" + std::to_string(functionProxyGrpcPort_),
            "--global_scheduler_address=" + metaStoreAddress,
            "--cache_storage_host=127.0.0.1",
            "--cache_storage_port=31501",
            "--log_config=" + logConfig
        };

        if (enableMergeProcess) {
            args.push_back("--enable_merge_process=true");
            args.push_back("--agent_listen_port=" + std::to_string(functionAgentPort_));
            args.push_back("--local_scheduler_address=" + proxyAddress);
            args.push_back("--access_key=");
            args.push_back("--secret_key=");
            args.push_back("--s3_endpoint=");
            args.push_back("--runtime_dir=" + testRuntimeDir_);
            args.push_back("--runtime_home_dir=" + testRuntimeDir_);
            args.push_back("--runtime_logs_dir=" + testLogDir_);
            args.push_back("--runtime_ld_library_path=" + testRuntimeDir_);
            args.push_back("--port=" + std::to_string(runtimeManagerPort_));
            args.push_back("--agent_address=" + agentAddress);
            args.push_back("--runtime_initial_port=500");
            args.push_back("--port_num=2000");
        }

        auto functionProxyProcess = CreateProcess(functionProxyBinPath_, args);
        if (functionProxyProcess.IsError()) {
            EXPECT_TRUE(false) << "Failed to create function_proxy process";
            return nullptr;
        }
        return functionProxyProcess.Get();
    }

protected:
    std::string functionAgentBinPath_;
    std::string runtimeManagerBinPath_;
    std::string functionProxyBinPath_;
    int functionAgentPort_;
    int runtimeManagerPort_;
    int functionProxyPort_;
    int functionProxyGrpcPort_;
    std::string testRuntimeDir_;
    std::string testLogDir_;
};

TEST_F(MergedProcessTest, FunctionProxyWithAgentAndRuntimeManager_MergedProcess)
{
    auto functionProxyProcess = StartFunctionProxy(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_NE(functionProxyProcess->GetPid(), 0);
    KillProcess(functionProxyProcess->GetPid(), SIGTERM);
    (void)functionProxyProcess->GetStatus().Get();
}

}  // namespace functionsystem::test
