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

#include "function_proxy/local_scheduler/gc_actor/local_gc_actor.h"

#include <atomic>
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <string>
#include <unordered_map>

#include "async/async.hpp"
#include "common/constants/actor_name.h"
#include "common/types/instance_state.h"
#include "mocks/mock_instance_control_view.h"
#include "mocks/mock_instance_ctrl.h"
#include "mocks/mock_instance_state_machine.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {

using namespace local_scheduler;
using namespace ::testing;

// Test constants
static const std::string TEST_NODE_ID = "test-node";
static const std::string TEST_INSTANCE_ID = "inst-001";
static const std::string TEST_INSTANCE_ID_2 = "inst-002";

// Short intervals for tests (milliseconds)
static const uint32_t GC_INTERVAL_MS = 50;
static const uint32_t TERMINAL_RETENTION_MS = 75;
static const uint32_t STUCK_TIMEOUT_MS = 175;

/**
 * Test fixture for LocalGcActor.
 *
 * Uses short GC interval and retention times so tests run quickly:
 *   gcInterval=50ms, terminalRetention=75ms, stuckTimeout=175ms
 *
 * Dependency injection via mock objects:
 *   - MockInstanceControlView  for enumerating instances
 *   - MockInstanceCtrl         for ForceDeleteInstance
 */
class LocalGcActorTest : public ::testing::Test {
public:
    void SetUp() override
    {
        mockInstanceCtrl_ = std::make_shared<MockInstanceCtrl>();
        mockInstanceControlView_ = std::make_shared<MockInstanceControlView>(TEST_NODE_ID);

        gcActor_ = std::make_shared<LocalGcActor>(
            LOCAL_GC_ACTOR_NAME, TEST_NODE_ID,
            GC_INTERVAL_MS, TERMINAL_RETENTION_MS, STUCK_TIMEOUT_MS);

        gcActor_->BindInstanceControlView(mockInstanceControlView_);
        gcActor_->BindInstanceCtrl(mockInstanceCtrl_);
        litebus::Spawn(gcActor_);
    }

    void TearDown() override
    {
        if (gcActor_ != nullptr) {
            litebus::Terminate(gcActor_->GetAID());
            litebus::Await(gcActor_->GetAID());
            gcActor_ = nullptr;
        }
        mockInstanceCtrl_ = nullptr;
        mockInstanceControlView_ = nullptr;
    }

    /**
     * Helper: build a single-entry instances map with the given state.
     */
    static std::unordered_map<std::string, std::shared_ptr<InstanceStateMachine>>
    MakeInstances(const std::string &instanceID, InstanceState state)
    {
        auto sm = std::make_shared<MockInstanceStateMachine>(TEST_NODE_ID);
        ON_CALL(*sm, GetInstanceState()).WillByDefault(Return(state));
        return {{instanceID, sm}};
    }

protected:
    std::shared_ptr<MockInstanceCtrl> mockInstanceCtrl_;
    std::shared_ptr<MockInstanceControlView> mockInstanceControlView_;
    std::shared_ptr<LocalGcActor> gcActor_;
};

// ---------------------------------------------------------------------------

/**
 * Feature: GC cleans up FAILED instances after stuck timeout expires.
 * Description:
 *   FAILED is treated as a retriable-failure state (not a hard terminal state).
 *   An instance stuck in FAILED state longer than stuckTimeoutMs (175ms)
 *   should be force-deleted.
 * Steps:
 *   1. Return FAILED instance from GetInstances() on every call.
 *   2. Expect ForceDeleteInstance to be invoked after the stuck timeout.
 *   3. Use an atomic flag + ASSERT_AWAIT_TRUE to wait for the call.
 * Expectation:
 *   ForceDeleteInstance("inst-001") is called within a reasonable timeout.
 */
TEST_F(LocalGcActorTest, FailedInstance_CleanedAfterStuckTimeout)
{
    auto instances = MakeInstances(TEST_INSTANCE_ID, InstanceState::FAILED);
    EXPECT_CALL(*mockInstanceControlView_, GetInstances())
        .WillRepeatedly(Return(instances));

    std::atomic<bool> deleted{false};
    EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance(TEST_INSTANCE_ID))
        .WillRepeatedly(Invoke([&deleted](const std::string &) {
            deleted = true;
            return AsyncReturn(Status::OK());
        }));

    ASSERT_AWAIT_TRUE([&deleted]() { return deleted.load(); });
}

/**
 * Feature: GC cleans up EXITED instances after the terminal retention window.
 * Description:
 *   EXITED is a terminal abnormal state cleaned after terminalRetentionMs (75ms).
 * Expectation:
 *   ForceDeleteInstance is called after the retention window.
 */
TEST_F(LocalGcActorTest, ExitedInstance_CleanedAfterRetentionWindow)
{
    auto instances = MakeInstances(TEST_INSTANCE_ID, InstanceState::EXITED);
    EXPECT_CALL(*mockInstanceControlView_, GetInstances())
        .WillRepeatedly(Return(instances));

    std::atomic<bool> deleted{false};
    EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance(TEST_INSTANCE_ID))
        .WillRepeatedly(Invoke([&deleted](const std::string &) {
            deleted = true;
            return AsyncReturn(Status::OK());
        }));

    ASSERT_AWAIT_TRUE([&deleted]() { return deleted.load(); });
}

/**
 * Feature: GC cleans up EVICTED instances after the terminal retention window.
 * Description:
 *   EVICTED is a terminal abnormal state cleaned after terminalRetentionMs (75ms).
 * Expectation:
 *   ForceDeleteInstance is called after the retention window.
 */
TEST_F(LocalGcActorTest, EvictedInstance_CleanedAfterRetentionWindow)
{
    auto instances = MakeInstances(TEST_INSTANCE_ID, InstanceState::EVICTED);
    EXPECT_CALL(*mockInstanceControlView_, GetInstances())
        .WillRepeatedly(Return(instances));

    std::atomic<bool> deleted{false};
    EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance(TEST_INSTANCE_ID))
        .WillRepeatedly(Invoke([&deleted](const std::string &) {
            deleted = true;
            return AsyncReturn(Status::OK());
        }));

    ASSERT_AWAIT_TRUE([&deleted]() { return deleted.load(); });
}

/**
 * Feature: GC cleans up FATAL instances after the terminal retention window.
 * Description:
 *   FATAL is a hard-crash terminal state cleaned after terminalRetentionMs (75ms).
 * Expectation:
 *   ForceDeleteInstance is called after the retention window.
 */
TEST_F(LocalGcActorTest, FatalInstance_CleanedAfterRetentionWindow)
{
    auto instances = MakeInstances(TEST_INSTANCE_ID, InstanceState::FATAL);
    EXPECT_CALL(*mockInstanceControlView_, GetInstances())
        .WillRepeatedly(Return(instances));

    std::atomic<bool> deleted{false};
    EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance(TEST_INSTANCE_ID))
        .WillRepeatedly(Invoke([&deleted](const std::string &) {
            deleted = true;
            return AsyncReturn(Status::OK());
        }));

    ASSERT_AWAIT_TRUE([&deleted]() { return deleted.load(); });
}

/**
 * Feature: GC cleans up SCHEDULE_FAILED instances after stuck timeout expires.
 * Description:
 *   SCHEDULE_FAILED is treated like a stuck transient state (retriable failure),
 *   cleaned after stuckTimeoutMs (175ms), not the terminal retention window.
 * Expectation:
 *   ForceDeleteInstance is called after the stuck timeout.
 */
TEST_F(LocalGcActorTest, ScheduleFailedInstance_CleanedAfterStuckTimeout)
{
    auto instances = MakeInstances(TEST_INSTANCE_ID, InstanceState::SCHEDULE_FAILED);
    EXPECT_CALL(*mockInstanceControlView_, GetInstances())
        .WillRepeatedly(Return(instances));

    std::atomic<bool> deleted{false};
    EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance(TEST_INSTANCE_ID))
        .WillRepeatedly(Invoke([&deleted](const std::string &) {
            deleted = true;
            return AsyncReturn(Status::OK());
        }));

    ASSERT_AWAIT_TRUE([&deleted]() { return deleted.load(); });
}

/**
 * Feature: GC cleans up stuck CREATING instances after timeout.
 * Description:
 *   An instance stuck in CREATING state longer than stuckTimeoutMs (175ms)
 *   should be force-deleted.
 * Steps:
 *   1. Return CREATING instance; stuckTimeout=175ms means ~4 cycles needed.
 *   2. Wait for ForceDeleteInstance to be called.
 * Expectation:
 *   ForceDeleteInstance is called after roughly 175ms + GC interval overhead.
 */
TEST_F(LocalGcActorTest, StuckCreatingInstance_CleanedAfterTimeout)
{
    auto instances = MakeInstances(TEST_INSTANCE_ID, InstanceState::CREATING);
    EXPECT_CALL(*mockInstanceControlView_, GetInstances())
        .WillRepeatedly(Return(instances));

    std::atomic<bool> deleted{false};
    EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance(TEST_INSTANCE_ID))
        .WillRepeatedly(Invoke([&deleted](const std::string &) {
            deleted = true;
            return AsyncReturn(Status::OK());
        }));

    ASSERT_AWAIT_TRUE([&deleted]() { return deleted.load(); });
}

/**
 * Feature: GC cleans up stuck SCHEDULING instances after timeout.
 * Description:
 *   Same as CREATING but for SCHEDULING state.
 * Expectation:
 *   ForceDeleteInstance is called after the stuck timeout.
 */
TEST_F(LocalGcActorTest, StuckSchedulingInstance_CleanedAfterTimeout)
{
    auto instances = MakeInstances(TEST_INSTANCE_ID, InstanceState::SCHEDULING);
    EXPECT_CALL(*mockInstanceControlView_, GetInstances())
        .WillRepeatedly(Return(instances));

    std::atomic<bool> deleted{false};
    EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance(TEST_INSTANCE_ID))
        .WillRepeatedly(Invoke([&deleted](const std::string &) {
            deleted = true;
            return AsyncReturn(Status::OK());
        }));

    ASSERT_AWAIT_TRUE([&deleted]() { return deleted.load(); });
}

/**
 * Feature: Healthy (RUNNING) instances are never cleaned up.
 * Description:
 *   A RUNNING instance must not be force-deleted regardless of how many GC
 *   cycles pass.
 * Steps:
 *   1. Return RUNNING instance; mock ForceDeleteInstance to set a flag.
 *   2. Run for ~300ms (6 GC cycles).
 *   3. Verify ForceDeleteInstance was never called.
 * Expectation:
 *   deleted flag remains false.
 */
TEST_F(LocalGcActorTest, RunningInstance_NeverCleaned)
{
    auto instances = MakeInstances(TEST_INSTANCE_ID, InstanceState::RUNNING);
    EXPECT_CALL(*mockInstanceControlView_, GetInstances())
        .WillRepeatedly(Return(instances));

    EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance(_)).Times(0);

    // Let 6 GC cycles pass without any cleanup
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

/**
 * Feature: Empty instance map produces no ForceDeleteInstance calls.
 * Description:
 *   When GetInstances() returns an empty map, GC should be a no-op.
 * Expectation:
 *   ForceDeleteInstance is never called.
 */
TEST_F(LocalGcActorTest, EmptyInstanceMap_NoCleaning)
{
    EXPECT_CALL(*mockInstanceControlView_, GetInstances())
        .WillRepeatedly(Return(
            std::unordered_map<std::string, std::shared_ptr<InstanceStateMachine>>{}));

    EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance(_)).Times(0);

    // Let a few GC cycles pass
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

/**
 * Feature: GC tracks only instances that are observed as abnormal.
 * Description:
 *   When two instances coexist — one RUNNING, one FAILED — only the FAILED
 *   one should be cleaned (after stuckTimeoutMs, since FAILED is a retriable
 *   failure state).
 * Expectation:
 *   ForceDeleteInstance(TEST_INSTANCE_ID_2) called; never called for
 *   TEST_INSTANCE_ID (RUNNING).
 */
TEST_F(LocalGcActorTest, MixedInstances_OnlyAbnormalCleaned)
{
    auto healthySm = std::make_shared<MockInstanceStateMachine>(TEST_NODE_ID);
    ON_CALL(*healthySm, GetInstanceState()).WillByDefault(Return(InstanceState::RUNNING));

    auto failedSm = std::make_shared<MockInstanceStateMachine>(TEST_NODE_ID);
    ON_CALL(*failedSm, GetInstanceState()).WillByDefault(Return(InstanceState::FAILED));

    std::unordered_map<std::string, std::shared_ptr<InstanceStateMachine>> instances{
        {TEST_INSTANCE_ID, healthySm},
        {TEST_INSTANCE_ID_2, failedSm},
    };
    EXPECT_CALL(*mockInstanceControlView_, GetInstances())
        .WillRepeatedly(Return(instances));

    // Healthy instance must never be deleted
    EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance(TEST_INSTANCE_ID)).Times(0);

    // Failed instance must be deleted
    std::atomic<bool> deleted{false};
    EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance(TEST_INSTANCE_ID_2))
        .WillRepeatedly(Invoke([&deleted](const std::string &) {
            deleted = true;
            return AsyncReturn(Status::OK());
        }));

    ASSERT_AWAIT_TRUE([&deleted]() { return deleted.load(); });
}

/**
 * Feature: GC purges stale tracking entries for vanished instances.
 * Description:
 *   An instance appears in FAILED state for one cycle (recorded), then
 *   disappears from the view.  No ForceDeleteInstance should be called since
 *   the instance is already gone.
 * Steps:
 *   1. First few GetInstances() calls return the FAILED instance.
 *   2. Subsequent calls return an empty map.
 *   3. Verify ForceDeleteInstance is never called.
 * Expectation:
 *   No deletion triggered; stale entry is silently removed.
 */
TEST_F(LocalGcActorTest, VanishedInstance_StaleEntryPurged)
{
    auto failedSm = std::make_shared<MockInstanceStateMachine>(TEST_NODE_ID);
    ON_CALL(*failedSm, GetInstanceState()).WillByDefault(Return(InstanceState::FAILED));

    std::unordered_map<std::string, std::shared_ptr<InstanceStateMachine>> instancesWithFailed{
        {TEST_INSTANCE_ID, failedSm}};
    std::unordered_map<std::string, std::shared_ptr<InstanceStateMachine>> emptyInstances{};

    // Return the failed instance for the first GC cycle, then return empty
    EXPECT_CALL(*mockInstanceControlView_, GetInstances())
        .WillOnce(Return(instancesWithFailed))
        .WillRepeatedly(Return(emptyInstances));

    // ForceDeleteInstance must NEVER be called
    EXPECT_CALL(*mockInstanceCtrl_, ForceDeleteInstance(_)).Times(0);

    // Wait for multiple cycles to confirm the stale entry is never acted upon
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

}  // namespace functionsystem::test
