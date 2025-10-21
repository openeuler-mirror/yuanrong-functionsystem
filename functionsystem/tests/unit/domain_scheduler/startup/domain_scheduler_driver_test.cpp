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

#include "domain_scheduler/startup/domain_scheduler_driver.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/constants/actor_name.h"
#include "mocks/mock_scheduler.h"

namespace functionsystem::test {
using namespace ::testing;
using ::testing::_;

class DomainSchedulerDriverTest : public Test {};

class GlobalActor : public litebus::ActorBase {
public:
    GlobalActor(const std::string &name) : ActorBase(name)
    {
    }
    ~GlobalActor() = default;
    void Register(const litebus::AID &from, std::string &&name, std::string &&msg)
    {
        messages::Registered rsp;
        rsp.set_code(0);
        Send(from, "Registered", rsp.SerializeAsString());
    }
    void Init() override
    {
        Receive("Register", &GlobalActor::Register);
    }
};

class DomainSchedulerDriverHelper : public domain_scheduler::DomainSchedulerDriver {
public:
    DomainSchedulerDriverHelper(const domain_scheduler::DomainSchedulerParam &param)
        : domain_scheduler::DomainSchedulerDriver(param)
    {
    }
    ~DomainSchedulerDriverHelper() = default;
    Status RegisterPolicyHepler(std::shared_ptr<schedule_decision::Scheduler> scheduler)
    {
        return RegisterPolicy(scheduler);
    }
};

/**
 * Description: Domain Scheduler StartUp Test
 * Expectation: normal start
 */
TEST_F(DomainSchedulerDriverTest, StartUpTest)
{
    auto global = std::make_shared<GlobalActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(global);
    auto identity = "node123-127.0.0.1:8080";
    domain_scheduler::DomainSchedulerParam param{ identity, "127.0.0.1:8080" };
    domain_scheduler::DomainSchedulerDriver driver(param);
    ASSERT_EQ(driver.Start(), Status::OK());
    ASSERT_EQ(driver.Start(), Status::OK());
    ASSERT_EQ(driver.Stop(), Status::OK());
    driver.Await();
    litebus::Terminate(global->GetAID());
    litebus::Await(global->GetAID());
}

/**
 * Description: Domain Scheduler StartUpWithPriorityScheduler Test
 * Expectation: normal start
 */
TEST_F(DomainSchedulerDriverTest, StartUpWithPrioritySchedulerTest)
{
    auto global = std::make_shared<GlobalActor>(DOMAIN_SCHED_MGR_ACTOR_NAME);
    litebus::Spawn(global);
    auto identity = "node123-127.0.0.1:8080";
    domain_scheduler::DomainSchedulerParam param{ identity, "127.0.0.1:8080" };
    param.maxPriority = 10;
    domain_scheduler::DomainSchedulerDriver driver(param);
    ASSERT_EQ(driver.Start(), Status::OK());
    ASSERT_EQ(driver.Start(), Status::OK());
    ASSERT_EQ(driver.Stop(), Status::OK());
    driver.Await();
    litebus::Terminate(global->GetAID());
    litebus::Await(global->GetAID());
}

TEST_F(DomainSchedulerDriverTest, RegisterFilterPolicyTest)
{
    auto identity = "node123-127.0.0.1:8080";
    domain_scheduler::DomainSchedulerParam param{ identity, "127.0.0.1:8080" };
    DomainSchedulerDriverHelper driver(param);

    driver.SetSchedulePlugins("fake_json");
    auto mockScheduler = std::make_shared<MockScheduler>();
    auto status = driver.RegisterPolicyHepler(mockScheduler);
    EXPECT_EQ(status.StatusCode(), StatusCode::FAILED);
    EXPECT_THAT(status.GetMessage(), HasSubstr("failed to register policy, not a valid json"));

    driver.SetSchedulePlugins("{}");
    status = driver.RegisterPolicyHepler(mockScheduler);
    EXPECT_EQ(status.StatusCode(), StatusCode::FAILED);
    EXPECT_THAT(status.GetMessage(), HasSubstr("failed to register policy, invalid format"));

    driver.SetSchedulePlugins("[\"plugin\"]");
    EXPECT_CALL(*mockScheduler, RegisterPolicy(_)).WillOnce(Return(Status(StatusCode::SUCCESS)));
    status = driver.RegisterPolicyHepler(mockScheduler);
    EXPECT_EQ(status.StatusCode(), StatusCode::SUCCESS);

    driver.SetSchedulePlugins("[\"plugin\", \"plugin2\"]");
    EXPECT_CALL(*mockScheduler, RegisterPolicy(_))
        .WillOnce(Return(Status(StatusCode::SUCCESS)))
        .WillOnce(Return(Status(StatusCode::SUCCESS)));
    status = driver.RegisterPolicyHepler(mockScheduler);
    EXPECT_EQ(status.StatusCode(), StatusCode::SUCCESS);
}
}  // namespace functionsystem::test