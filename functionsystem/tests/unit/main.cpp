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

#include <litebus.hpp>

#include "logs/logging.h"
#include "function_proxy/common/state_machine/instance_state_machine.h"
#include "common/utils/exception.h"
#include "logs/sdk/log_param_parser.h"
#include "utils/port_helper.h"

using namespace functionsystem;

const std::string NODE_NAME = "node";
const std::string MODEL_NAME = "model";
const std::string LOG_CONFIG_JSON = R"(
{
  "filepath": ".",
  "level": "DEBUG",
  "rolling": {
    "maxsize": 100,
    "maxfiles": 1
  },
  "async": {
    "logBufSecs": 30,
    "maxQueueSize": 1048510,
    "threadCount": 1
  },
  "alsologtostderr": true,
  "stdLogLevel": "DEBUG"
}
)";

namespace LogsSdk = observability::sdk::logs;
namespace LogsApi = observability::api::logs;

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);

    auto globalParam = LogsSdk::GetGlobalLogParam(LOG_CONFIG_JSON);
    auto param = LogsSdk::GetLogParam(LOG_CONFIG_JSON, NODE_NAME, MODEL_NAME, false);
    auto lp = std::make_shared<LogsSdk::LoggerProvider>(globalParam);
    lp->CreateYrLogger(param);
    LogsApi::Provider::SetLoggerProvider(lp);

    RegisterSigHandler();

    int port = functionsystem::test::FindAvailablePort();
    litebus::os::SetEnv("LITEBUS_PORT", std::to_string(port));
    std::cout << "port: " << port << std::endl;
    auto res =
        litebus::Initialize("tcp://127.0.0.1:" + std::to_string(port), "", "udp://127.0.0.1:" + std::to_string(port));
    if (res != BUS_OK) {
        YRLOG_ERROR("failed to initialize litebus!");
        return -1;
    }

    int code = RUN_ALL_TESTS();
    InstanceStateMachine::UnBindControlPlaneObserver();
    litebus::TerminateAll();
    litebus::Finalize();
    return code;
}