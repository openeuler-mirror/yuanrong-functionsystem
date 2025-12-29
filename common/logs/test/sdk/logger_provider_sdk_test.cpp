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

#include "gtest/gtest.h"
#include "logs/api/null.h"
#include "logs/api/provider.h"
#include "logs/sdk/log_param_parser.h"
#include "logs/sdk/logger_provider.h"

namespace observability::test::sdk {

using namespace observability::sdk::logs;
namespace LogsApi = observability::api::logs;
namespace LogsSdk = observability::sdk::logs;

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

const std::string LOG_ASYNC_CONFIG_JSON = R"(
{
  "async": {
    "logBufSecs": 10,
    "maxQueueSize": 1048510,
    "threadCount": 2
  }
}
)";

class LoggerProviderSDKTest : public ::testing::Test {
protected:
    void TearDown() override
    {
        auto null = std::make_shared<LogsApi::NullLoggerProvider>();
        LogsApi::Provider::SetLoggerProvider(null);
    }
};

#define SDK_DEBUG(...) LOGS_CORE_LOGGER(LOGS_LEVEL_DEBUG, __VA_ARGS__)
#define SDK_INFO(...) LOGS_CORE_LOGGER(LOGS_LEVEL_INFO, __VA_ARGS__)
#define SDK_WARN(...) LOGS_CORE_LOGGER(LOGS_LEVEL_WARN, __VA_ARGS__)
#define SDK_ERROR(...) LOGS_CORE_LOGGER(LOGS_LEVEL_ERROR, __VA_ARGS__)
#define SDK_FATAL(...) LOGS_CORE_LOGGER(LOGS_LEVEL_FATAL, __VA_ARGS__)

using LoggerProviderSDKDeathTest = LoggerProviderSDKTest;

TEST_F(LoggerProviderSDKTest, AsycInit)
{
    auto globalLogParam = GetGlobalLogParam(LOG_ASYNC_CONFIG_JSON);
    EXPECT_EQ(globalLogParam.logBufSecs, 10);
    EXPECT_EQ(globalLogParam.maxAsyncQueueSize, 1048510);
    EXPECT_EQ(globalLogParam.asyncThreadCount, 2);
    auto lp = std::make_shared<LoggerProvider>(globalLogParam);
    LogsApi::Provider::SetLoggerProvider(lp);
    auto provider = LogsApi::Provider::GetLoggerProvider();
    EXPECT_EQ(provider, lp);
    EXPECT_EQ(provider->GetYrLogger("CoreLogger"), nullptr);
}

TEST_F(LoggerProviderSDKTest, GetCoreLogger)
{
    auto lp = std::make_shared<LoggerProvider>();
    LogsApi::Provider::SetLoggerProvider(lp);
    auto provider = LogsApi::Provider::GetLoggerProvider();
    EXPECT_EQ(provider, lp);

    EXPECT_EQ(provider->GetYrLogger("CoreLogger"), nullptr);
}

TEST_F(LoggerProviderSDKTest, GetLogger)
{
    auto lp = std::make_shared<LoggerProvider>();
    LogsApi::Provider::SetLoggerProvider(lp);
    auto provider = LogsApi::Provider::GetLoggerProvider();
    EXPECT_EQ(provider, lp);

    EXPECT_EQ(provider->GetLogger(""), nullptr);
    EXPECT_NE(provider->GetLogger("meter_one"), nullptr);
    EXPECT_NE(provider->GetLogger("meter_two"), nullptr);

    EXPECT_EQ(provider->GetLogger("meter_one"), provider->GetLogger("meter_one"));
    EXPECT_NE(provider->GetLogger("meter_one"), provider->GetLogger("meter_two"));
    EXPECT_EQ(provider->GetLogger("meter_one")->GetName(), "meter_one");
}

TEST_F(LoggerProviderSDKDeathTest, UseLogMarcoWithSetProvider)
{
    auto param = GetLogParam(LOG_CONFIG_JSON, NODE_NAME, MODEL_NAME, false);
    auto globalLogParam = GetGlobalLogParam(LOG_CONFIG_JSON);
    auto lp = std::make_shared<LoggerProvider>(globalLogParam);
    LogsApi::Provider::SetLoggerProvider(lp);
    auto logger = lp->CreateYrLogger(param);
    EXPECT_NE(logger, nullptr);
    SDK_DEBUG("debug message");
    SDK_INFO("info message");
    SDK_WARN("warn message");
    SDK_ERROR("error message");
    enum TestEnum { FIRST, SECOND };
    EXPECT_EXIT(SDK_FATAL("fatal message"), testing::KilledBySignal(SIGINT), "");
}

TEST_F(LoggerProviderSDKTest, DropLogger)
{
    const std::string loggerName2 = "Logger2";
    auto lp1 = std::make_shared<LoggerProvider>();

    auto param2 = GetLogParam(LOG_CONFIG_JSON, NODE_NAME, MODEL_NAME, false);
    param2.loggerName = loggerName2;
    (void)lp1->CreateYrLogger(param2);
    auto logger2 = lp1->GetYrLogger(loggerName2);
    EXPECT_EQ(logger2->name(), loggerName2);

    lp1->DropYrLogger(loggerName2);
    logger2 = lp1->GetYrLogger(loggerName2);
    EXPECT_EQ(logger2, nullptr);
}

TEST_F(LoggerProviderSDKTest, MultiLogger)
{
    const std::string loggerName1 = "Logger1";
    const std::string loggerName2 = "Logger2";
    const std::string loggerName3 = "Logger3";

    auto lp1 = std::make_shared<LoggerProvider>();
    auto param1 = GetLogParam(LOG_CONFIG_JSON, NODE_NAME, MODEL_NAME, false);
    param1.loggerName = loggerName1;
    (void)lp1->CreateYrLogger(param1);
    auto logger1 = lp1->GetYrLogger(loggerName1);
    EXPECT_EQ(logger1->name(), loggerName1);

    auto param2 = GetLogParam(LOG_CONFIG_JSON, NODE_NAME, MODEL_NAME, false);
    param2.loggerName = loggerName2;
    (void)lp1->CreateYrLogger(param2);
    auto logger2 = lp1->GetYrLogger(loggerName2);
    EXPECT_EQ(logger2->name(), loggerName2);
    EXPECT_NE(logger1, logger2);

    auto param3 = GetLogParam(LOG_CONFIG_JSON, "", "", false);
    param3.loggerName = loggerName3;
    auto lp2 = std::make_shared<LoggerProvider>();
    (void)lp2->CreateYrLogger(param3);
    auto logger3 = lp2->GetYrLogger(loggerName3);
    EXPECT_EQ(logger3->name(), loggerName3);
    EXPECT_NE(logger1, logger3);
}

TEST_F(LoggerProviderSDKTest, OnExit)
{
    auto provider = std::make_shared<LogsSdk::LoggerProvider>();
    EXPECT_TRUE(provider->Shutdown());
    EXPECT_TRUE(provider->ForceFlush());
}
}  // namespace observability::test::sdk