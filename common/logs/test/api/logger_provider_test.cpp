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

#include "logs/api/null.h"
#include "logs/api/provider.h"

using namespace observability::api::logs;

namespace observability::test::api {

class LoggerProviderTest : public ::testing::Test {};

#define API_DEBUG(...) LOGS_CORE_LOGGER(LOGS_LEVEL_DEBUG, __VA_ARGS__)
#define API_INFO(...) LOGS_CORE_LOGGER(LOGS_LEVEL_INFO, __VA_ARGS__)
#define API_WARN(...) LOGS_CORE_LOGGER(LOGS_LEVEL_WARN, __VA_ARGS__)
#define API_ERROR(...) LOGS_CORE_LOGGER(LOGS_LEVEL_ERROR, __VA_ARGS__)
#define API_FATAL(...) LOGS_CORE_LOGGER(LOGS_LEVEL_FATAL, __VA_ARGS__)

using LoggerProviderDeathTest = LoggerProviderTest;

TEST_F(LoggerProviderTest, GetDefaultLoggerProvider)
{
    auto provider = Provider::GetLoggerProvider();
    EXPECT_NE(provider, nullptr);
}

TEST_F(LoggerProviderTest, SetNullLoggerProvider)
{
    auto lp = std::make_shared<NullLoggerProvider>();
    Provider::SetLoggerProvider(lp);
    auto provider = Provider::GetLoggerProvider();
    EXPECT_EQ(provider, lp);
}

TEST_F(LoggerProviderTest, ResetLoggerProvider)
{
    std::shared_ptr<LoggerProvider> lp;
    Provider::SetLoggerProvider(lp);
    EXPECT_EQ(Provider::GetLoggerProvider(), nullptr);
}

TEST_F(LoggerProviderTest, SetLoggerProviderDuplicate)
{
    auto lp1 = std::make_shared<NullLoggerProvider>();
    Provider::SetLoggerProvider(lp1);
    auto lp2 = std::make_shared<NullLoggerProvider>();
    Provider::SetLoggerProvider(lp2);
    auto provider = Provider::GetLoggerProvider();
    EXPECT_EQ(provider, lp2);
}

TEST_F(LoggerProviderDeathTest, UseLogMarcoWithoutSetProvider)
{
    API_DEBUG("debug message");
    API_INFO("info message");
    API_WARN("warn message");
    API_ERROR("error message");
    EXPECT_NO_FATAL_FAILURE(API_FATAL("fatal message"));  // will not raise signal
}

}  // namespace observability::test::api