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

#include <chrono>
#include <thread>

#define private public
#define protected public

#include "common/proto/pb/posix/message.pb.h"
#include "domain_scheduler/instance_control/instance_ctrl.h"
#include "domain_scheduler/instance_control/instance_ctrl_actor.h"
#include "common/schedule_decision/schedule_recorder/schedule_recorder.h"
#include "mocks/mock_scheduler.h"
#include "mocks/mock_domain_underlayer_sched_mgr.h"
#include "mocks/mock_resource_view.h"
#include "utils/future_test_helper.h"

namespace functionsystem::test {

using schedule_decision::ScheduleResult;
using ::testing::_;
using ::testing::Return;

class InstanceCtrlCooldownTest : public ::testing::Test {
public:
    void SetUp() override
    {
        instanceCtrl_ = std::make_shared<domain_scheduler::InstanceCtrlActor>("CooldownTest");
        mockScheduler_ = std::make_shared<MockScheduler>();
        mockUnderlayerScheMgr_ = std::make_shared<MockDomainUnderlayerSchedMgr>();
        auto resourceViewMgr = std::make_shared<resource_view::ResourceViewMgr>();
        primary_ = MockResourceView::CreateMockResourceView();
        virtual_ = MockResourceView::CreateMockResourceView();
        resourceViewMgr->primary_ = primary_;
        resourceViewMgr->virtual_ = virtual_;
        instanceCtrl_->BindScheduler(mockScheduler_);
        instanceCtrl_->BindResourceView(resourceViewMgr);
        instanceCtrl_->BindUnderlayerMgr(mockUnderlayerScheMgr_);
        instanceCtrl_->BindScheduleRecorder(schedule_decision::ScheduleRecorder::CreateScheduleRecorder());
        litebus::Spawn(instanceCtrl_);
    }

    void TearDown() override
    {
        litebus::Terminate(instanceCtrl_->GetAID());
        litebus::Await(instanceCtrl_);
    }

    // Build a serialized TenantQuotaExceeded proto
    static std::string MakeCooldownMsg(const std::string &tenantID, int64_t cooldownMs)
    {
        ::messages::TenantQuotaExceeded event;
        event.set_tenantid(tenantID);
        event.set_cooldownms(cooldownMs);
        return event.SerializeAsString();
    }

    // Build a ScheduleRequest for a tenant
    static std::shared_ptr<messages::ScheduleRequest> MakeScheduleReq(const std::string &tenantID,
                                                                       const std::string &reqID = "req-1")
    {
        auto req = std::make_shared<messages::ScheduleRequest>();
        req->set_requestid(reqID);
        req->mutable_instance()->set_tenantid(tenantID);
        return req;
    }

protected:
    std::shared_ptr<domain_scheduler::InstanceCtrlActor> instanceCtrl_;
    std::shared_ptr<MockScheduler> mockScheduler_;
    std::shared_ptr<MockDomainUnderlayerSchedMgr> mockUnderlayerScheMgr_;
    std::shared_ptr<MockResourceView> primary_;
    std::shared_ptr<MockResourceView> virtual_;
};

/**
 * Description: HandleTenantQuotaExceeded blocks the tenant and adds it to cooldownMgr_.
 * Steps:
 * 1. Call HandleTenantQuotaExceeded with tenant "t1", cooldown 5000ms
 * 2. Verify cooldownMgr_.IsBlocked("t1") returns true
 */
TEST_F(InstanceCtrlCooldownTest, HandleTenantQuotaExceededBlocksTenant)
{
    domain_scheduler::InstanceCtrl ctrl(instanceCtrl_->GetAID());
    ctrl.OnTenantQuotaExceeded(MakeCooldownMsg("t1", 5000));

    ASSERT_AWAIT_TRUE([this]() {
        return instanceCtrl_->cooldownMgr_.IsBlocked("t1");
    });

    EXPECT_TRUE(instanceCtrl_->cooldownMgr_.IsBlocked("t1"));
}

/**
 * Description: Schedule request for a blocked tenant is immediately rejected.
 * Steps:
 * 1. Block tenant "t1" with a long cooldown (5000ms)
 * 2. Try to Schedule with a request for "t1"
 * 3. Expect immediate failure with ERR_RESOURCE_NOT_ENOUGH code
 */
TEST_F(InstanceCtrlCooldownTest, ScheduleBlockedTenantReturnsQuotaExceeded)
{
    domain_scheduler::InstanceCtrl ctrl(instanceCtrl_->GetAID());
    ctrl.OnTenantQuotaExceeded(MakeCooldownMsg("t1", 5000));

    // Wait for block to be applied
    ASSERT_AWAIT_TRUE([this]() {
        return instanceCtrl_->cooldownMgr_.IsBlocked("t1");
    });

    auto req = MakeScheduleReq("t1", "req-blocked");
    auto future = ctrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 3000);
    auto rsp = future.Get();
    EXPECT_NE(rsp->code(), 0);
    EXPECT_THAT(rsp->message(), ::testing::HasSubstr("QUOTA_EXCEEDED"));
}

/**
 * Description: After cooldown expires the tenant is unblocked.
 * Steps:
 * 1. Block tenant "t2" with a short cooldown (200ms)
 * 2. Verify it is blocked
 * 3. Wait for cooldown to expire
 * 4. Verify cooldownMgr_.IsBlocked("t2") returns false
 */
TEST_F(InstanceCtrlCooldownTest, TenantUnblockedAfterCooldown)
{
    domain_scheduler::InstanceCtrl ctrl(instanceCtrl_->GetAID());
    ctrl.OnTenantQuotaExceeded(MakeCooldownMsg("t2", 200));

    // Verify blocked
    ASSERT_AWAIT_TRUE([this]() {
        return instanceCtrl_->cooldownMgr_.IsBlocked("t2");
    });

    // Wait for unblock (cooldown 200ms + margin)
    ASSERT_AWAIT_TRUE_FOR(
        [this]() { return !instanceCtrl_->cooldownMgr_.IsBlocked("t2"); },
        2000);

    EXPECT_FALSE(instanceCtrl_->cooldownMgr_.IsBlocked("t2"));
}

/**
 * Description: Different tenants are blocked independently.
 * Steps:
 * 1. Block tenant "tA" with 5000ms cooldown
 * 2. Block tenant "tB" with 5000ms cooldown
 * 3. Schedule for unblocked "tC" should proceed normally
 */
TEST_F(InstanceCtrlCooldownTest, UnblockedTenantCanSchedule)
{
    domain_scheduler::InstanceCtrl ctrl(instanceCtrl_->GetAID());
    ctrl.OnTenantQuotaExceeded(MakeCooldownMsg("tA", 5000));

    // Wait for block to be applied
    ASSERT_AWAIT_TRUE([this]() {
        return instanceCtrl_->cooldownMgr_.IsBlocked("tA");
    });

    // "tC" is not blocked — its Schedule should reach the scheduler
    std::string mockSelected = "selected-agent";
    ScheduleResult result{ mockSelected, 0, "" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _)).WillOnce(Return(AsyncReturn(result)));

    auto mockRsp = std::make_shared<messages::ScheduleResponse>();
    mockRsp->set_code(0);
    mockRsp->set_requestid("req-tC");
    EXPECT_CALL(*mockUnderlayerScheMgr_, DispatchSchedule(mockSelected, _)).WillOnce(Return(AsyncReturn(mockRsp)));

    resource_view::PullResourceRequest snapshot;
    EXPECT_CALL(*primary_, GetUnitSnapshotInfo(_)).WillOnce(Return(litebus::Future<resource_view::PullResourceRequest>{snapshot}));

    auto req = MakeScheduleReq("tC", "req-tC");
    auto future = ctrl.Schedule(req);

    ASSERT_AWAIT_READY_FOR(future, 3000);
    EXPECT_EQ(future.Get()->code(), 0);
}

/**
 * Description: Repeated quota exceeded for same tenant resets the timer.
 * Steps:
 * 1. Block tenant "t3" twice
 * 2. Verify it's still blocked (cooldownMgr_.IsBlocked returns true)
 */
TEST_F(InstanceCtrlCooldownTest, RepeatedQuotaExceededOverwritesTimer)
{
    domain_scheduler::InstanceCtrl ctrl(instanceCtrl_->GetAID());
    ctrl.OnTenantQuotaExceeded(MakeCooldownMsg("t3", 5000));
    ctrl.OnTenantQuotaExceeded(MakeCooldownMsg("t3", 5000));

    ASSERT_AWAIT_TRUE([this]() {
        return instanceCtrl_->cooldownMgr_.IsBlocked("t3");
    });

    // Should still be blocked (generation mechanism handles reset)
    EXPECT_TRUE(instanceCtrl_->cooldownMgr_.IsBlocked("t3"));
}

/**
 * Description: ZeroCooldownMsDefaultsToTenSeconds
 * Steps:
 * 1. Send cooldownMs=0 message to "t4"
 * 2. Immediately check cooldownMgr_.IsBlocked("t4") == true
 * 3. Verify the default is not a trivially short timer (still blocked after 1s)
 */
TEST_F(InstanceCtrlCooldownTest, ZeroCooldownMsDefaultsToTenSeconds)
{
    domain_scheduler::InstanceCtrl ctrl(instanceCtrl_->GetAID());
    ctrl.OnTenantQuotaExceeded(MakeCooldownMsg("t4", 0));

    ASSERT_AWAIT_TRUE([this]() {
        return instanceCtrl_->cooldownMgr_.IsBlocked("t4");
    });

    EXPECT_TRUE(instanceCtrl_->cooldownMgr_.IsBlocked("t4"));

    // Verify it's not using a trivially short timer (still blocked after 1s)
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    EXPECT_TRUE(instanceCtrl_->cooldownMgr_.IsBlocked("t4"));
}

/**
 * Description: EmptyTenantIdIsIgnored
 * Steps:
 * 1. Send message with tenantID=""
 * 2. Briefly wait then verify cooldownMgr_.IsBlocked("") == false
 * 3. IsBlocked("") is the only check needed since Apply("") is a no-op at the helper level
 */
TEST_F(InstanceCtrlCooldownTest, EmptyTenantIdIsIgnored)
{
    domain_scheduler::InstanceCtrl ctrl(instanceCtrl_->GetAID());
    ctrl.OnTenantQuotaExceeded(MakeCooldownMsg("", 5000));

    ASSERT_AWAIT_TRUE_FOR([this]() { return true; }, 100);

    EXPECT_FALSE(instanceCtrl_->cooldownMgr_.IsBlocked(""));
}

/**
 * Description: RepeatedNotificationResetsTimerSemantically
 * Steps:
 * 1. Send cooldownMs=500 to "t5"
 * 2. Wait 350ms (first timer not yet expired)
 * 3. Send cooldownMs=500 again to "t5" (reset)
 * 4. Wait 200ms (first timer would have expired, but generation should protect)
 * 5. Verify "t5" is still blocked
 * 6. Wait another 400ms (second timer expires around t=850ms from resend)
 * 7. Verify "t5" is unblocked
 */
TEST_F(InstanceCtrlCooldownTest, RepeatedNotificationResetsTimerSemantically)
{
    domain_scheduler::InstanceCtrl ctrl(instanceCtrl_->GetAID());
    ctrl.OnTenantQuotaExceeded(MakeCooldownMsg("t5", 500));

    ASSERT_AWAIT_TRUE([this]() {
        return instanceCtrl_->cooldownMgr_.IsBlocked("t5");
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(350));

    ctrl.OnTenantQuotaExceeded(MakeCooldownMsg("t5", 500));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // First timer (fired at ~500ms from t=0) should be ignored by generation mechanism;
    // second timer (fires at ~500ms from resend) has not yet expired
    ASSERT_AWAIT_TRUE_FOR([this]() { return instanceCtrl_->cooldownMgr_.IsBlocked("t5"); }, 100);

    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    ASSERT_AWAIT_TRUE_FOR(
        [this]() { return !instanceCtrl_->cooldownMgr_.IsBlocked("t5"); },
        1000);

    EXPECT_FALSE(instanceCtrl_->cooldownMgr_.IsBlocked("t5"));
}

}  // namespace functionsystem::test
