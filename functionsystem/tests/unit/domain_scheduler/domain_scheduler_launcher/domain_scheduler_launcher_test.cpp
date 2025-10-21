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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "domain_scheduler/include/domain_scheduler_launcher.h"
#include "mocks/mock_module_driver.h"

namespace functionsystem::domain_scheduler::test {
using namespace functionsystem::test;
using ::testing::Return;

class DomainSchedLauncherTest : public ::testing::Test {
protected:
    void SetUp()
    {
        moduleDriver_ = std::make_shared<MockModuleDriver>();
        domainSchedulerLauncher_ = std::make_shared<DomainSchedulerLauncher>(moduleDriver_);
    }

    void TearDown()
    {
        moduleDriver_ = nullptr;
        domainSchedulerLauncher_ = nullptr;
    }
protected:
    std::shared_ptr<MockModuleDriver> moduleDriver_;
    std::shared_ptr<DomainSchedulerLauncher> domainSchedulerLauncher_;
};

TEST_F(DomainSchedLauncherTest, StartModuleOK)
{
    EXPECT_CALL(*moduleDriver_, Start()).WillRepeatedly(Return(Status::OK()));

    EXPECT_EQ(domainSchedulerLauncher_->Start(), Status::OK());
}

TEST_F(DomainSchedLauncherTest, StopModuleOK)
{
    EXPECT_CALL(*moduleDriver_, Stop()).WillRepeatedly(Return(Status::OK()));

    EXPECT_EQ(domainSchedulerLauncher_->Stop(), Status::OK());
}

TEST_F(DomainSchedLauncherTest, AwaitModuleOK)
{
    EXPECT_CALL(*moduleDriver_, Await()).WillRepeatedly(Return());

    domainSchedulerLauncher_->Await();
}

}