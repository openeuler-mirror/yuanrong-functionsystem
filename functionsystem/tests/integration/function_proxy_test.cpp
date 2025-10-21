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

#include <async/try.hpp>
#include <exec/exec.hpp>
#include <utils/os_utils.hpp>

#include "logs/logging.h"
#include "stubs/etcd_service/etcd_service_driver.h"
#include "utils.h"  // for CreateProcess
#include "utils/port_helper.h"

namespace functionsystem::test::function_proxy {
const std::string HOST_IP = "127.0.0.1";      // NOLINT
const std::string DATA_SYSTEM_PORT = "5400";  // NOLINT
const std::string LOG_CONFIG =                // NOLINT
    R"(--log_config={"filepath": "/home/yr/log","level": "DEBUG","rolling": {"maxsize": 100,"maxfiles": 1}})";

const std::string MASTER_NODE_ID = "it_function_master_long_123455656_lsrjt-34211";  // NOLINT
const std::string MASTER_ADDRESS = "127.0.0.1:5500";      // NOLINT

const std::string PROXY_NODE_ID = "it_function_proxy";  // NOLINT
const std::string PROXY_ADDRESS = "127.0.0.1:5600";     // NOLINT
const std::string PROXY_GRPC_PORT = "5601";             // NOLINT

const std::string AGENT_NODE_ID = "it_function_agent";        // NOLINT
const std::string AGENT_PORT = "5700";                        // NOLINT
const std::string AGENT_ADDRESS = "127.0.0.1:" + AGENT_PORT;  // NOLINT

const std::string MANAGER_NODE_ID = "it_runtime_manager";  // NOLINT
const std::string MANAGER_PORT = "5800";                   // NOLINT

const std::string RUNTIME_INITIAL_PORT = "500";  // NOLINT
const std::string RUNTIME_PORT_COUNT = "2000";   // NOLINT

const std::string ACCESSOR_NODE_ID = "it_function_accessor";  // NOLINT
const std::string ACCESSOR_PORT = "5900";                     // NOLINT
const std::string ACCESSOR_GRPC_PORT = "5901";                // NOLINT

class FunctionProxyTest : public ::testing::Test {
public:
    [[maybe_unused]] void SetUp() override
    {
        metaStoreServerPort_ = GetPortEnv("META_STORE_SERVER_PORT", 60000);
    }

    [[maybe_unused]] void TearDown() override
    {
    }

    void StartFunctionMaster()
    {
        YRLOG_INFO("start function_master process");
        const std::string path = binDir_ + "/function_master";
        const std::vector<std::string> args = { "",
                                                "--node_id=" + MASTER_NODE_ID,  // do not modify
                                                "--ip=" + MASTER_ADDRESS,
                                                "--meta_store_address=127.0.0.1:"
                                                    + std::to_string(metaStoreServerPort_),
                                                "--sys_func_retry_period=5000",
                                                "--sys_func_custom_args=",
                                                LOG_CONFIG };

        masterProcess_ = CreateProcess(path, args);
        ASSERT_TRUE(masterProcess_.IsOK());
    }

    void StartFunctionProxy()
    {
        YRLOG_INFO("start function_proxy process");
        const std::string path = binDir_ + "/function_proxy";
        const std::vector<std::string> args = { "",
                                                "--address=" + PROXY_ADDRESS,
                                                "--meta_store_address=127.0.0.1:"
                                                    + std::to_string(metaStoreServerPort_),
                                                "--services_path=",
                                                "--lib_path=",
                                                "--node_id=" + PROXY_NODE_ID,
                                                "--ip=" + HOST_IP,
                                                "--grpc_listen_port=" + PROXY_GRPC_PORT,
                                                "--runtime_heartbeat_enable=false",
                                                "--runtime_max_heartbeat_timeout_times=5",
                                                "--runtime_heartbeat_timeout_ms=5000",
                                                "--global_scheduler_address=" + MASTER_ADDRESS,
                                                "--cache_storage_host=" + HOST_IP,
                                                "--cache_storage_port=" + DATA_SYSTEM_PORT,
                                                "--enable_trace=false",
                                                LOG_CONFIG };

        proxyProcess_ = CreateProcess(path, args);
        ASSERT_TRUE(proxyProcess_.IsOK());
    }

    void StartFunctionAgent()
    {
        YRLOG_INFO("start function_agent process");
        const std::string path = binDir_ + "/function_agent";
        const std::vector<std::string> args = { "",
                                                "--node_id=" + AGENT_NODE_ID,
                                                "--ip=" + HOST_IP,
                                                "--agent_listen_port=" + AGENT_PORT,
                                                "--local_scheduler_address=" + PROXY_ADDRESS,
                                                "--access_key=",
                                                "--secret_key=",
                                                "--s3_endpoint=",
                                                LOG_CONFIG };

        agentProcess_ = CreateProcess(path, args);
        ASSERT_TRUE(agentProcess_.IsOK());
    }

    void StartFunctionAccessor()
    {
        YRLOG_INFO("start function_accessor process");
        const std::string path = binDir_ + "/function_accessor";
        const std::vector<std::string> args = { "",
                                                "--node_id=" + ACCESSOR_NODE_ID,
                                                "--ip=" + HOST_IP,
                                                "--http_listen_port=" + ACCESSOR_PORT,
                                                "--grpc_listen_port=" + ACCESSOR_GRPC_PORT,
                                                "--select_scheduler_policy=TopKRandom",
                                                "--min_instance_memory_size=128",
                                                "--min_instance_cpu_size=300",
                                                "--meta_store_address=127.0.0.1:"
                                                    + std::to_string(metaStoreServerPort_),
                                                "--enable_trace=false",
                                                LOG_CONFIG };

        accessorProcess_ = CreateProcess(path, args);
        ASSERT_TRUE(accessorProcess_.IsOK());
    }

protected:
    std::string binDir_;
    uint16_t metaStoreServerPort_;

    litebus::Try<std::shared_ptr<litebus::Exec>> masterProcess_;    // NOLINT
    litebus::Try<std::shared_ptr<litebus::Exec>> proxyProcess_;     // NOLINT
    litebus::Try<std::shared_ptr<litebus::Exec>> agentProcess_;     // NOLINT
    litebus::Try<std::shared_ptr<litebus::Exec>> managerProcess_;   // NOLINT
    litebus::Try<std::shared_ptr<litebus::Exec>> accessorProcess_;  // NOLINT

    std::unique_ptr<meta_store::test::EtcdServiceDriver> etcdSrvDriver_;
};

TEST_F(FunctionProxyTest, StartTest)  // NOLINT
{
    ASSERT_TRUE(true);
}
}  // namespace functionsystem::test::function_proxy
