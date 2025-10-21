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

#include "common/utils/module_switcher.h"

#include <gtest/gtest.h>

#include <litebus.hpp>

#include "domain_scheduler/flags/flags.h"

namespace functionsystem::test {

const std::string ADDRESS = "127.0.0.1:5500";  // NOLINT
std::shared_ptr<litebus::Promise<bool>> stopSignal = nullptr;

void Stop(int signum)
{
    YRLOG_INFO("receive signal: {}", signum);
    stopSignal->SetValue(true);
}

void SetStop(const std::shared_ptr<functionsystem::ModuleSwitcher> &switcher_)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    switcher_->SetStop();
}

class ModuleSwitcherTest : public ::testing::Test {
public:
    void SetUp() override
    {
        switcher_ = std::make_shared<functionsystem::ModuleSwitcher>(componentName_, nodeID_);
    }
    void TearDown() override
    {
        switcher_ = nullptr;
    }

protected:
    std::shared_ptr<functionsystem::ModuleSwitcher> switcher_;
    const std::string componentName_ = "domain_scheduler";
    const std::string nodeID_ = "nodeID";
};

/**
 * Feature: SwitcherFailedTest
 * Description: start an module with li
 * Steps:
 * 1. Register stop signal
 * 2. InitLiteBus, always return true, because litebus has already initialized
 * 3. Set stop signal and wait stop
 * Expectation:
 * No error occurs during the process.
 */
TEST_F(ModuleSwitcherTest, SwitcherStartTest)
{
    switcher_->RegisterHandler(Stop, stopSignal);
    auto result = switcher_->InitLiteBus("127.0.0.1", 3);
    EXPECT_TRUE(result);
    switcher_->SetStop();
    switcher_->WaitStop();
}

TEST_F(ModuleSwitcherTest, SwitcherStartNoUDPTest)
{
    switcher_->RegisterHandler(Stop, stopSignal);
    auto result = switcher_->InitLiteBus("127.0.0.1", 3, false);
    EXPECT_TRUE(result);
    switcher_->SetStop();
    switcher_->WaitStop();
}
}  // namespace functionsystem::test
