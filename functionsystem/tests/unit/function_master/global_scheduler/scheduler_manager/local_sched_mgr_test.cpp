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
#include <gmock/gmock.h>
#include "function_master/global_scheduler/scheduler_manager/local_sched_mgr.h"
#include "mocks/mock_local_sched_srv_actor.h"
#include "utils/future_test_helper.h"
#include "common/constants/actor_name.h"
#include "common/utils/generate_message.h"

namespace functionsystem::test {

class LocalSchedMgrTest : public ::testing::Test {
public:
    void SetUp() override
    {}

    void TearDown() override
    {}

    static void
    Registered(litebus::Option<::messages::ScheduleTopology> topology, std::string &name, std::string &responseMsg)
    {
        auto actor = std::make_shared<functionsystem::global_scheduler::LocalSchedMgrActor>(
            "TestLocalSchedMgrActor");
        auto schedMgr = std::make_shared<functionsystem::global_scheduler::LocalSchedMgr>(actor);
        schedMgr->Start();

        auto localScheduler = std::make_shared<MockLocalSchedSrvActor>("MockLocalSchedSrvActor");
        litebus::Spawn(localScheduler);

        litebus::Future<std::string> funcName;
        litebus::Future<std::string> registeredResponse;
        EXPECT_CALL(*localScheduler.get(), MockRegistered(testing::_, testing::_, testing::_))
            .WillOnce(testing::DoAll(FutureArg<1>(&funcName), FutureArg<2>(&registeredResponse)));

        schedMgr->Registered(localScheduler->GetAID(), topology);

        ASSERT_AWAIT_READY(funcName);
        EXPECT_TRUE(funcName.Get() == name);

        ASSERT_AWAIT_READY(registeredResponse);
        EXPECT_TRUE(registeredResponse.Get() == responseMsg);

        litebus::Terminate(localScheduler->GetAID());
        litebus::Await(localScheduler->GetAID());
        schedMgr->Stop();
    }
};

/**
 * Feature: LocalSchedMgr
 * Description: add localSchedCallback function to LocalSchedMgr
 * Steps:
 * 1. give nullptr
 * 2. give correct function
 * Expectation:
 * 1. StatusCode::FAILED
 * 2. StatusCode::SUCCESS
 */
TEST_F(LocalSchedMgrTest, AddLocalSchedCallback)
{
    auto actor = std::make_shared<functionsystem::global_scheduler::LocalSchedMgrActor>(
        "TestLocalSchedMgrActor");
    auto schedMgr = std::make_shared<functionsystem::global_scheduler::LocalSchedMgr>(actor);
    schedMgr->Start();

    // given
    global_scheduler::CallbackAddFunc givens[] = {
        nullptr,
        [](const litebus::AID from, const std::string &name, const std::string &address) {},
    };

    // want
    Status wants[] = {
        Status(StatusCode::FAILED),
        Status(StatusCode::SUCCESS),
    };

    // got
    for (uint32_t i = 0; i < sizeof(givens) / sizeof(global_scheduler::CallbackAddFunc); i++) {
        EXPECT_EQ(schedMgr->AddLocalSchedCallback(givens[i]), wants[i]);
    }
    schedMgr->Stop();
}

/**
 * Feature: LocalSchedMgr
 * Description: call Registered to inform local scheduler
 * Steps:
 * 1. give none topology
 * 2. give topology1
 * 3. give topology2
 * Expectation:
 * 1. StatusCode::GS_REGISTERED_SCHEDULER_TOPOLOGY_IS_NONE
 * 2. StatusCode::SUCCESS
 * 3. StatusCode::SUCCESS
 */
TEST_F(LocalSchedMgrTest, Registered)
{
    // given
    litebus::Option<messages::ScheduleTopology> givens[] = {
        {},
        {messages::ScheduleTopology()},
        []() -> litebus::Option<messages::ScheduleTopology> {
            messages::ScheduleTopology topology;
            topology.mutable_leader();
            topology.mutable_members();
            return {topology};
        }(),
    };

    // want
    std::string wants[] = {
        []() -> std::string {
            return GenRegistered(static_cast<int32_t>(StatusCode::GS_REGISTERED_SCHEDULER_TOPOLOGY_IS_NONE),
                                 "topology message is none").SerializeAsString();
        }(),
        []() -> std::string {
            auto response = GenRegistered(static_cast<int32_t>(StatusCode::SUCCESS),
                                          "registered success");
            response.mutable_topo()->CopyFrom(messages::ScheduleTopology());
            return response.SerializeAsString();
        }(),
        []() -> std::string {
            messages::ScheduleTopology topology;
            topology.mutable_members();
            topology.mutable_leader();
            auto response = GenRegistered(static_cast<int32_t>(StatusCode::SUCCESS),
                                          "registered success");
            response.mutable_topo()->CopyFrom(topology);
            return response.SerializeAsString();
        }(),
    };

    // got
    for (uint32_t i = 0; i < sizeof(wants) / sizeof(std::string); i++) {
        std::string funcName = "Registered";
        LocalSchedMgrTest::Registered(givens[i], funcName, wants[i]);
    }
}

/**
 * Feature: LocalSchedMgr
 * Description: call UpdateSchedTopoView to inform local scheduler update topo
 * Steps:
 * give none topology
 * Expectation:
 * local scheduler receive the correct topology
 */
TEST_F(LocalSchedMgrTest, UpdateSchedTopoView)
{
    auto actor = std::make_shared<functionsystem::global_scheduler::LocalSchedMgrActor>(
        "TestLocalSchedMgrActor");
    auto schedMgr = std::make_shared<functionsystem::global_scheduler::LocalSchedMgr>(actor);
    schedMgr->Start();

    auto localScheduler = std::make_shared<MockLocalSchedSrvActor>(LOCAL_SCHED_SRV_ACTOR_NAME);
    litebus::Spawn(localScheduler);

    litebus::Future<std::string> funcName;
    litebus::Future<std::string> registeredResponse;
    EXPECT_CALL(*localScheduler.get(), MockUpdateSchedTopoView(testing::_, testing::_, testing::_))
        .WillOnce(testing::DoAll(FutureArg<1>(&funcName), FutureArg<2>(&registeredResponse)));

    schedMgr->UpdateSchedTopoView(localScheduler->GetAID().Url(), messages::ScheduleTopology());

    ASSERT_AWAIT_READY(funcName);
    EXPECT_TRUE(funcName.Get() == "UpdateSchedTopoView");

    ASSERT_AWAIT_READY(registeredResponse);
    EXPECT_TRUE(registeredResponse.Get() == ::messages::ScheduleTopology().SerializeAsString());

    litebus::Terminate(localScheduler->GetAID());
    litebus::Await(localScheduler->GetAID());
    schedMgr->Stop();
}

}