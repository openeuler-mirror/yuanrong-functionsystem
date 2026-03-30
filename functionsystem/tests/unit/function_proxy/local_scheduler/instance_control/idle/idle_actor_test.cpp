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

#include "function_proxy/local_scheduler/instance_control/idle/idle_actor.h"
#include "function_proxy/local_scheduler/instance_control/instance_ctrl_actor.h"

#include <atomic>
#include <chrono>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "async/async.hpp"
#include "mocks/mock_instance_control_view.h"
#include "mocks/mock_instance_state_machine.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {

using namespace local_scheduler;
using namespace ::testing;

class IdleActorTest : public ::testing::Test {
public:
    void SetUp() override
    {
        idleViewMock_ = std::make_shared<MockInstanceControlView>(NODE_ID);
        facadeViewMock_ = std::make_shared<MockInstanceControlView>(NODE_ID);

        facadeActor_ = std::make_shared<InstanceCtrlActor>("facade", NODE_ID, InstanceCtrlConfig{});
        facadeActor_->BindInstanceControlView(facadeViewMock_);
        litebus::Spawn(facadeActor_);

        idleActor_ = std::make_shared<IdleActor>("idle", NODE_ID, idleViewMock_, facadeActor_->GetAID());
        litebus::Spawn(idleActor_);
    }

    void TearDown() override
    {
        litebus::Terminate(idleActor_->GetAID());
        litebus::Await(idleActor_->GetAID());
        litebus::Terminate(facadeActor_->GetAID());
        litebus::Await(facadeActor_->GetAID());
    }

    /**
     * Build a MockInstanceStateMachine in RUNNING state on NODE_ID.
     * @param idleTimeoutSec  >= 1: sets "idleTimeout" createoption; -1: no key (no timer).
     */
    std::shared_ptr<MockInstanceStateMachine> MakeRunningInstance(int64_t idleTimeoutSec)
    {
        auto sm = std::make_shared<MockInstanceStateMachine>(NODE_ID);
        resources::InstanceInfo info;
        info.set_functionproxyid(NODE_ID);
        info.mutable_instancestatus()->set_code(static_cast<int32_t>(InstanceState::RUNNING));
        if (idleTimeoutSec >= 0) {
            (*info.mutable_createoptions())["idle_timeout"] = std::to_string(idleTimeoutSec);
        }
        EXPECT_CALL(*sm, GetInstanceInfo()).WillRepeatedly(Return(info));
        return sm;
    }

protected:
    static constexpr const char *NODE_ID = "test-node";
    static constexpr const char *INST_ID = "inst-001";

    std::shared_ptr<MockInstanceControlView> idleViewMock_;    // IdleActor's own view
    std::shared_ptr<MockInstanceControlView> facadeViewMock_;  // InstanceCtrlActor's view
    std::shared_ptr<InstanceCtrlActor> facadeActor_;
    std::shared_ptr<IdleActor> idleActor_;
};

/**
 * Feature: idle instance with no active sessions triggers eviction after timeout.
 * Steps:
 *   1. idleViewMock_.GetInstance returns RUNNING sm with idleTimeout=1s.
 *   2. TrafficReport(0) — traffic idle, no sessions → timer starts.
 *   3. After ~1s, HandleIdleTimeout fires → EvictByIdleTimeout dispatched.
 *   4. EvictByIdleTimeout calls facadeViewMock_.GetInstance as first side-effect.
 * Expectation: facadeViewMock_.GetInstance called exactly once.
 */
TEST_F(IdleActorTest, TrafficIdle_NoSessions_TimerFires_EvictsInstance)
{
    auto sm = MakeRunningInstance(1);
    EXPECT_CALL(*idleViewMock_, GetInstance(INST_ID)).WillRepeatedly(Return(sm));

    std::atomic<int> callCount{0};
    EXPECT_CALL(*facadeViewMock_, GetInstance(INST_ID))
        .WillOnce(Invoke([&](const std::string &) {
            callCount++;
            return nullptr;
        }));

    litebus::Async(idleActor_->GetAID(), &IdleActor::TrafficReport, std::string(INST_ID), static_cast<size_t>(0));

    ASSERT_AWAIT_TRUE([&]() { return callCount > 0; });
}

/**
 * Feature: busy traffic cancels the pending idle timer — no eviction.
 * Steps:
 *   1. TrafficReport(0) starts timer.
 *   2. TrafficReport(1) cancels timer before it fires.
 *   3. Wait 3s (> idleTimeout of 1s).
 * Expectation: facadeViewMock_.GetInstance never called.
 */
TEST_F(IdleActorTest, TrafficBusy_CancelsTimer)
{
    auto sm = MakeRunningInstance(1);
    EXPECT_CALL(*idleViewMock_, GetInstance(INST_ID)).WillRepeatedly(Return(sm));

    EXPECT_CALL(*facadeViewMock_, GetInstance(INST_ID)).Times(0);

    litebus::Async(idleActor_->GetAID(), &IdleActor::TrafficReport, std::string(INST_ID), static_cast<size_t>(0));
    litebus::Async(idleActor_->GetAID(), &IdleActor::TrafficReport, std::string(INST_ID), static_cast<size_t>(1));

    std::this_thread::sleep_for(std::chrono::seconds(3));
}

/**
 * Feature: a new exec session cancels the idle timer — no eviction.
 * Steps:
 *   1. TrafficReport(0) starts timer.
 *   2. SessionCountDelta(+1) → sessions 0→1 edge → CancelIdleTimer.
 *   3. Wait 3s.
 * Expectation: facadeViewMock_.GetInstance never called.
 */
TEST_F(IdleActorTest, SessionStart_CancelsTimer)
{
    auto sm = MakeRunningInstance(1);
    EXPECT_CALL(*idleViewMock_, GetInstance(INST_ID)).WillRepeatedly(Return(sm));

    EXPECT_CALL(*facadeViewMock_, GetInstance(INST_ID)).Times(0);

    litebus::Async(idleActor_->GetAID(), &IdleActor::TrafficReport, std::string(INST_ID), static_cast<size_t>(0));
    litebus::Async(idleActor_->GetAID(), &IdleActor::SessionCountDelta, std::string(INST_ID), 1);

    std::this_thread::sleep_for(std::chrono::seconds(3));
}

/**
 * Feature: session end with traffic already idle restarts the timer → eviction.
 * Steps:
 *   1. TrafficReport(0) — traffic idle.
 *   2. SessionCountDelta(+1) — sessions go 0→1, timer cancelled.
 *   3. SessionCountDelta(-1) — sessions go 1→0, traffic idle → StartIdleTimer.
 *   4. Timer fires → EvictByIdleTimeout.
 * Expectation: facadeViewMock_.GetInstance called once.
 */
TEST_F(IdleActorTest, SessionEnd_WithTrafficIdle_StartsTimer)
{
    auto sm = MakeRunningInstance(1);
    EXPECT_CALL(*idleViewMock_, GetInstance(INST_ID)).WillRepeatedly(Return(sm));

    std::atomic<int> callCount{0};
    EXPECT_CALL(*facadeViewMock_, GetInstance(INST_ID))
        .WillOnce(Invoke([&](const std::string &) {
            callCount++;
            return nullptr;
        }));

    litebus::Async(idleActor_->GetAID(), &IdleActor::TrafficReport, std::string(INST_ID), static_cast<size_t>(0));
    litebus::Async(idleActor_->GetAID(), &IdleActor::SessionCountDelta, std::string(INST_ID), 1);
    litebus::Async(idleActor_->GetAID(), &IdleActor::SessionCountDelta, std::string(INST_ID), -1);

    ASSERT_AWAIT_TRUE([&]() { return callCount > 0; });
}

/**
 * Feature: session end when traffic is NOT idle does not start the timer.
 * Steps:
 *   1. SessionCountDelta(+1) — session starts (no prior TrafficReport(0)).
 *   2. SessionCountDelta(-1) — session ends; traffic is not idle → no timer.
 *   3. Wait 3s.
 * Expectation: facadeViewMock_.GetInstance never called.
 */
TEST_F(IdleActorTest, SessionEnd_WithTrafficBusy_NoTimer)
{
    auto sm = MakeRunningInstance(1);
    EXPECT_CALL(*idleViewMock_, GetInstance(INST_ID)).WillRepeatedly(Return(sm));

    EXPECT_CALL(*facadeViewMock_, GetInstance(INST_ID)).Times(0);

    litebus::Async(idleActor_->GetAID(), &IdleActor::SessionCountDelta, std::string(INST_ID), 1);
    litebus::Async(idleActor_->GetAID(), &IdleActor::SessionCountDelta, std::string(INST_ID), -1);

    std::this_thread::sleep_for(std::chrono::seconds(3));
}

/**
 * Feature: stale-generation callback does not evict; only the newest timer evicts.
 * Steps:
 *   1. TrafficReport(0) → timer gen=1 starts.
 *   2. TrafficReport(1) → CancelIdleTimer increments gen to 2.
 *   3. TrafficReport(0) → new timer gen=3 starts.
 *   4. Gen=1 callback (if it fires) must be rejected (gen mismatch).
 *   5. Gen=3 fires → evicts.
 * Expectation: facadeViewMock_.GetInstance called exactly once.
 */
TEST_F(IdleActorTest, StaleGeneration_NewTimerFires_EvictsOnce)
{
    auto sm = MakeRunningInstance(1);
    EXPECT_CALL(*idleViewMock_, GetInstance(INST_ID)).WillRepeatedly(Return(sm));

    std::atomic<int> callCount{0};
    EXPECT_CALL(*facadeViewMock_, GetInstance(INST_ID))
        .WillOnce(Invoke([&](const std::string &) {
            callCount++;
            return nullptr;
        }));

    litebus::Async(idleActor_->GetAID(), &IdleActor::TrafficReport, std::string(INST_ID), static_cast<size_t>(0));
    litebus::Async(idleActor_->GetAID(), &IdleActor::TrafficReport, std::string(INST_ID), static_cast<size_t>(1));
    litebus::Async(idleActor_->GetAID(), &IdleActor::TrafficReport, std::string(INST_ID), static_cast<size_t>(0));

    ASSERT_AWAIT_TRUE([&]() { return callCount > 0; });

    // Allow extra time and verify no second call arrives
    std::this_thread::sleep_for(std::chrono::seconds(2));
    EXPECT_EQ(callCount.load(), 1);
}

/**
 * Feature: active sessions at the moment of timeout veto eviction in IdleActor.
 * Steps:
 *   1. TrafficReport(0) starts timer (idleTimeout=1s).
 *   2. SessionCountDelta(+1) cancels the timer before it fires.
 *   3. Wait 2s.
 * Expectation: facadeViewMock_.GetInstance never called (timer was cancelled).
 */
TEST_F(IdleActorTest, ActiveSessions_AtTimeout_VetoEviction)
{
    auto sm = MakeRunningInstance(1);
    EXPECT_CALL(*idleViewMock_, GetInstance(INST_ID)).WillRepeatedly(Return(sm));

    EXPECT_CALL(*facadeViewMock_, GetInstance(INST_ID)).Times(0);

    litebus::Async(idleActor_->GetAID(), &IdleActor::TrafficReport, std::string(INST_ID), static_cast<size_t>(0));
    // Inject session start quickly after — timer is cancelled via CancelIdleTimer
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    litebus::Async(idleActor_->GetAID(), &IdleActor::SessionCountDelta, std::string(INST_ID), 1);

    std::this_thread::sleep_for(std::chrono::seconds(2));
}

/**
 * Feature: instance with no idleTimeout configured never gets a timer.
 * Steps:
 *   1. idleViewMock_.GetInstance returns sm with NO "idleTimeout" createoption.
 *   2. TrafficReport(0) — StartIdleTimer calls GetIdleTimeout → returns -1 → no timer.
 *   3. Wait 3s.
 * Expectation: facadeViewMock_.GetInstance never called.
 */
TEST_F(IdleActorTest, NoIdleTimeout_NoTimer)
{
    auto sm = MakeRunningInstance(-1);  // no idleTimeout key
    EXPECT_CALL(*idleViewMock_, GetInstance(INST_ID)).WillRepeatedly(Return(sm));

    EXPECT_CALL(*facadeViewMock_, GetInstance(INST_ID)).Times(0);

    litebus::Async(idleActor_->GetAID(), &IdleActor::TrafficReport, std::string(INST_ID), static_cast<size_t>(0));

    std::this_thread::sleep_for(std::chrono::seconds(3));
}

}  // namespace functionsystem::test
