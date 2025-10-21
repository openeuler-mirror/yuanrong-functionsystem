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

#include "logs/sdk/log_param_parser.h"
#include "logs/sdk/logger_provider.h"

const std::string LITEBUS_TCP_URL("tcp://127.0.0.1:8080");  // NOLINT
const std::string LITEBUS_UDP_URL("udp://127.0.0.1:8080");  // NOLINT

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
  "alsologtostderr": true
}
)";

const std::string LOG_GLOBAL_CONFIG_JSON = R"(
{
  "async": {
    "logBufSecs": 30,
    "maxQueueSize": 1048510,
    "threadCount": 1
  }
}
)";

namespace LogsSdk = observability::sdk::logs;
namespace LogsApi = observability::api::logs;

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);

    auto param = LogsSdk::GetLogParam(LOG_CONFIG_JSON, NODE_NAME, MODEL_NAME, false);
    param.loggerName = "CoreLogger";
    auto globalLogParam = LogsSdk::GetGlobalLogParam(LOG_GLOBAL_CONFIG_JSON);
    auto lp = std::make_shared<LogsSdk::LoggerProvider>(globalLogParam);
    (void)lp->CreateYrLogger(param);
    LogsApi::Provider::SetLoggerProvider(lp);

    auto res = litebus::Initialize(LITEBUS_TCP_URL, "", LITEBUS_UDP_URL);
    if (res != BUS_OK) {
        std::cerr << "failed to initialize litebus!" << std::endl;
        return -1;
    }

    int code = RUN_ALL_TESTS();
    litebus::TerminateAll();
    litebus::Finalize();
    return code;
}