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

#include "function_master/global_scheduler/scheduler_manager/local_sched_mgr_actor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/utils/generate_message.h"
#include "mocks/mock_local_sched_srv_actor.h"
#include "utils/future_test_helper.h"
#include "utils/generate_info.h"

namespace functionsystem::test {

using namespace functionsystem::global_scheduler;
class LocalSchedMgrActorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }

public:
    static void LocalSchedulerRegister(std::string &&name, std::string &&responseMsg, std::string &&registerMsg)
    {
        auto actor = std::make_shared<functionsystem::global_scheduler::LocalSchedMgrActor>("TestLocalSchedMgrActor");
        litebus::Spawn(actor);

        litebus::Async(actor->GetAID(), &LocalSchedMgrActor::UpdateLeaderInfo, GetLeaderInfo(actor->GetAID()));

        auto scheduler = std::make_shared<MockLocalSchedSrvActor>("MockLocalSchedSrvActor");
        litebus::Spawn(scheduler);

        litebus::Future<std::string> funcName;
        litebus::Future<std::string> registeredResponse;
        if (name == "Registered") {
            EXPECT_CALL(*scheduler.get(), MockRegistered(testing::_, testing::_, testing::_))
                .WillOnce(testing::DoAll(FutureArg<1>(&funcName), FutureArg<2>(&registeredResponse)));

            litebus::Async(scheduler->GetAID(), &MockLocalSchedSrvActor::RegisterToGlobalScheduler, actor->GetAID(),
                           std::move(registerMsg));
        }
        if (name == "UnRegistered") {
            EXPECT_CALL(*scheduler.get(), MockUnRegistered(testing::_, testing::_, testing::_))
                .WillOnce(testing::DoAll(FutureArg<1>(&funcName), FutureArg<2>(&registeredResponse)));

            litebus::Async(scheduler->GetAID(), &MockLocalSchedSrvActor::UnRegisterToGlobalScheduler, actor->GetAID(),
                           std::move(registerMsg));
        }

        ASSERT_AWAIT_READY(funcName);
        EXPECT_TRUE(funcName.Get() == name);

        ASSERT_AWAIT_READY(registeredResponse);
        EXPECT_TRUE(registeredResponse.Get() == responseMsg);

        litebus::Terminate(actor->GetAID());
        litebus::Terminate(scheduler->GetAID());
        litebus::Await(actor);
        litebus::Await(scheduler);
    }
};

/**
 * Feature: LocalSchedMgrActor
 * Description: register to LocalSchedulerMgrActor with Invalid Request
 * Steps:
 * Expectation:
 * 1. message: invalid request message
 * 2. code: StatusCode::GS_REGISTER_REQUEST_INVALID
 */
TEST_F(LocalSchedMgrActorTest, LocalSchedulerRegisterWithInvalidRequest)
{
    // given
    std::string givens[] = {
        "",
        "~",
        GenRegister("", "TestAddress").SerializeAsString(),
        GenRegister("TestName", "").SerializeAsString(),
    };

    // want
    std::string wants[] = {
        GenRegistered(static_cast<int32_t>(StatusCode::GS_REGISTER_REQUEST_INVALID), "invalid request message")
            .SerializeAsString(),
        GenRegistered(static_cast<int32_t>(StatusCode::GS_REGISTER_REQUEST_INVALID), "invalid request message")
            .SerializeAsString(),
        GenRegistered(static_cast<int32_t>(StatusCode::GS_REGISTER_REQUEST_INVALID), "invalid request message")
            .SerializeAsString(),
        GenRegistered(static_cast<int32_t>(StatusCode::GS_REGISTER_REQUEST_INVALID), "invalid request message")
            .SerializeAsString(),
    };

    // got
    for (uint32_t i = 0; i < sizeof(givens) / sizeof(std::string); i++) {
        LocalSchedMgrActorTest::LocalSchedulerRegister("Registered", std::move(wants[i]), std::move(givens[i]));
    }
    // got
    for (uint32_t i = 0; i < sizeof(givens) / sizeof(std::string); i++) {
        LocalSchedMgrActorTest::LocalSchedulerRegister("UnRegistered", std::move(wants[i]), std::move(givens[i]));
    }
}

/**
 * Feature: LocalSchedMgrActor
 * Description: register to LocalSchedulerMgrActor with Valid Request
 * Steps:
 * Expectation: call callback with correct parameters
 */
TEST_F(LocalSchedMgrActorTest, LocalSchedulerRegisterWithValidRequest)
{
    // given
    std::string givens[] = {
        GenRegister("TestName", "127.0.0.1:7888").SerializeAsString(),
    };

    // want
    std::string wants[][2] = {
        { "TestName", "127.0.0.1:7888" },
    };

    // got
    auto actor = std::make_shared<functionsystem::global_scheduler::LocalSchedMgrActor>("TestLocalSchedMgrActor");
    actor->Init();
    actor->UpdateLeaderInfo(GetLeaderInfo(actor->GetAID()));
    auto scheduler = std::make_shared<MockLocalSchedSrvActor>("MockLocalSchedSrvActor");
    for (uint32_t i = 0; i < sizeof(givens) / sizeof(std::string); i++) {
        auto wantName = wants[i][0];
        auto wantAddress = wants[i][1];
        actor->AddLocalSchedCallback(
            [wantName, wantAddress](const litebus::AID &from, const std::string &name, const std::string &address) {
                EXPECT_TRUE(wantName == name);
                EXPECT_TRUE(wantAddress == address);
            });
        actor->Register(scheduler->GetAID(), "Register", std::move(givens[i]));
    }
    for (uint32_t i = 0; i < sizeof(givens) / sizeof(std::string); i++) {
        auto wantName = wants[i][0];
        auto wantAddress = wants[i][1];
        actor->DelLocalSchedCallback(
            [wantName, wantAddress](const std::string &name, const std::string &ip) {
                EXPECT_TRUE(wantName == name);
                EXPECT_TRUE(GetIPFromAddress(wantAddress) == ip);
            });
        actor->UnRegister(scheduler->GetAID(), "UnRegister", std::move(givens[i]));
    }
}

/**
 * Feature: LocalSchedMgrActor
 * Description: evict agent test
 * cases1: send evict request successful
 * case2: ack failed
 * case3: notify evict failed
 * case4: duplicate evict
 * case5: send to abnormal local
 */
TEST_F(LocalSchedMgrActorTest, EvictAgentOnLocal)
{
    auto actor = std::make_shared<functionsystem::global_scheduler::LocalSchedMgrActor>("TestLocalSchedMgrActor");
    auto scheduler = std::make_shared<MockLocalSchedSrvActor>("MockLocalSchedSrvActor");
    litebus::Spawn(actor);
    litebus::Spawn(scheduler);

    auto localAddress = scheduler->GetAID().Url();
    // cases1: send evict request successful
    {
        auto req = std::make_shared<messages::EvictAgentRequest>();
        req->set_agentid("agentID");
        req->set_timeoutsec(10);
        EXPECT_CALL(*scheduler.get(), MockEvictAgent(testing::_, testing::_, testing::_)).Times(1);
        auto future = litebus::Async(actor->GetAID(), &LocalSchedMgrActor::EvictAgentOnLocal, localAddress, req);
        auto ack = messages::EvictAgentAck();
        ack.set_agentid("agentID");
        scheduler->EvictAgentAck(actor->GetAID(), ack.SerializeAsString());
        auto result = messages::EvictAgentResult();
        result.set_agentid("agentID");
        scheduler->NotifyEvictResult(actor->GetAID(), result.SerializeAsString());
        EXPECT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
    }
    // case2: ack failed
    {
        auto req = std::make_shared<messages::EvictAgentRequest>();
        req->set_agentid("agentID");
        req->set_timeoutsec(10);
        EXPECT_CALL(*scheduler.get(), MockEvictAgent(testing::_, testing::_, testing::_)).Times(1);
        auto future = litebus::Async(actor->GetAID(), &LocalSchedMgrActor::EvictAgentOnLocal, localAddress, req);
        auto ack = messages::EvictAgentAck();
        ack.set_code(StatusCode::PARAMETER_ERROR);
        ack.set_agentid("agentID");
        scheduler->EvictAgentAck(actor->GetAID(), ack.SerializeAsString());
        EXPECT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        EXPECT_EQ(future.Get().StatusCode(), StatusCode::PARAMETER_ERROR);
    }
    // case3: notify evict failed
    {
        auto req = std::make_shared<messages::EvictAgentRequest>();
        req->set_agentid("agentID");
        req->set_timeoutsec(10);
        EXPECT_CALL(*scheduler.get(), MockEvictAgent(testing::_, testing::_, testing::_)).Times(1);
        auto future = litebus::Async(actor->GetAID(), &LocalSchedMgrActor::EvictAgentOnLocal, localAddress, req);
        auto ack = messages::EvictAgentAck();
        ack.set_agentid("agentID");
        scheduler->EvictAgentAck(actor->GetAID(), ack.SerializeAsString());
        auto result = messages::EvictAgentResult();
        result.set_agentid("agentID");
        result.set_code(StatusCode::PARAMETER_ERROR);
        scheduler->NotifyEvictResult(actor->GetAID(), result.SerializeAsString());
        EXPECT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        EXPECT_EQ(future.Get().StatusCode(), StatusCode::PARAMETER_ERROR);
    }
    // case4: duplicate evict
    {
        auto req = std::make_shared<messages::EvictAgentRequest>();
        req->set_agentid("agentID");
        req->set_timeoutsec(10);
        EXPECT_CALL(*scheduler.get(), MockEvictAgent(testing::_, testing::_, testing::_)).Times(1);
        auto future = litebus::Async(actor->GetAID(), &LocalSchedMgrActor::EvictAgentOnLocal, localAddress, req);
        auto future1 = litebus::Async(actor->GetAID(), &LocalSchedMgrActor::EvictAgentOnLocal, localAddress, req);
        auto ack = messages::EvictAgentAck();
        ack.set_agentid("agentID");
        scheduler->EvictAgentAck(actor->GetAID(), ack.SerializeAsString());
        auto result = messages::EvictAgentResult();
        result.set_agentid("agentID");
        scheduler->NotifyEvictResult(actor->GetAID(), result.SerializeAsString());
        EXPECT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
        EXPECT_AWAIT_READY(future1);
        EXPECT_EQ(future1.IsOK(), true);
    }
    // case5: send to abnormal local
    {
        auto req = std::make_shared<messages::EvictAgentRequest>();
        req->set_agentid("agentID");
        req->set_timeoutsec(10);
        auto future = litebus::Async(actor->GetAID(), &LocalSchedMgrActor::EvictAgentOnLocal, "abnormal address", req);
        litebus::Async(actor->GetAID(), &LocalSchedMgrActor::OnLocalAbnormal, "local", "empty");
        litebus::Async(actor->GetAID(), &LocalSchedMgrActor::OnLocalAbnormal, "local", "abnormal address");
        EXPECT_AWAIT_READY(future);
        EXPECT_EQ(future.IsOK(), true);
    }
    litebus::Terminate(actor->GetAID());
    litebus::Terminate(scheduler->GetAID());
    litebus::Await(actor->GetAID());
    litebus::Await(scheduler->GetAID());
}

}  // namespace functionsystem::test
