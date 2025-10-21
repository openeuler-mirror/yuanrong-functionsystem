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

#include "common/resource_view/view_utils.h"
#include "common/schedule_decision/scheduler.h"
#include "common/schedule_decision/schedule_queue_actor.h"
#include "common/schedule_decision/scheduler/priority_scheduler.h"
#include "mocks/mock_resource_view.h"
#include "mocks/mock_schedule_performer.h"
#include "utils/future_test_helper.h"
#include "async/async.hpp"

namespace functionsystem::test {
using namespace ::testing;
using namespace schedule_decision;

class ScheduleTest : public Test {
public:
    void SetUp() override
    {
        scheduleQueueActor_ = std::make_shared<schedule_decision::ScheduleQueueActor>("ScheduleQueueActor");
        // resource view
        mockResourceView_ = MockResourceView::CreateMockResourceView();
        EXPECT_CALL(*mockResourceView_, AddResourceUpdateHandler).WillOnce(Return());
        scheduleQueueActor_->RegisterResourceView(mockResourceView_);
        // scheduler
        mockInstancePerformer_ = std::make_shared<MockInstanceSchedulePerformer>();
        mockGroupPerformer_ = std::make_shared<MockGroupSchedulePerformer>();
        auto fairnessSchedule =
                std::make_shared<PriorityScheduler>(schedule_decision::ScheduleRecorder::CreateScheduleRecorder(),
                                                    10, PriorityPolicyType::FAIRNESS);
        mockAggregatedSchedulePerformer_ = std::make_shared<MockAggregatedSchedulePerformer>();
        fairnessSchedule->RegisterSchedulePerformer(mockInstancePerformer_, mockGroupPerformer_,mockAggregatedSchedulePerformer_);
        scheduleQueueActor_->RegisterScheduler(fairnessSchedule);
        litebus::Spawn(scheduleQueueActor_);
    }

    void TearDown() override
    {
        if (scheduleQueueActor_ != nullptr) {
            litebus::Terminate(scheduleQueueActor_->GetAID());
            litebus::Await(scheduleQueueActor_->GetAID());
        }
        scheduleQueueActor_ = nullptr;
        mockResourceView_ = nullptr;
        mockInstancePerformer_ = nullptr;
        mockGroupPerformer_ = nullptr;
        mockAggregatedSchedulePerformer_ = nullptr;
    }

protected:
    std::shared_ptr<schedule_decision::ScheduleQueueActor> scheduleQueueActor_;
    std::shared_ptr<MockResourceView> mockResourceView_;
    std::shared_ptr<MockInstanceSchedulePerformer> mockInstancePerformer_;
    std::shared_ptr<MockGroupSchedulePerformer> mockGroupPerformer_;
    std::shared_ptr<MockAggregatedSchedulePerformer> mockAggregatedSchedulePerformer_;
};

TEST_F(ScheduleTest, InstanceScheduleSuccess)
{
    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("ins");

    resource_view::ResourceViewInfo info;
    EXPECT_CALL(*mockResourceView_, GetResourceInfo).WillOnce(Return(info));
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule).WillOnce(Return(ScheduleResult{"", 0, ""}));

    auto scheduler = std::make_shared<Scheduler>(scheduleQueueActor_->GetAID(), scheduleQueueActor_->GetAID());
    auto future = scheduler->ScheduleDecision(req, litebus::Future<std::string>());
    EXPECT_AWAIT_READY_FOR(future, 1000);
    auto result = future.Get();
    EXPECT_EQ(result.code, 0);
}

TEST_F(ScheduleTest, GroupScheduleSuccess)
{
    std::string groupReqId = "groupReqId";
    std::vector<std::shared_ptr<messages::ScheduleRequest>> requests;
    for (int i = 0; i < 3; i++) {
        auto req = std::make_shared<messages::ScheduleRequest>();
        req->set_requestid("group-" + std::to_string(i));
        requests.emplace_back(req);
    }
    resource_view::ResourceViewInfo info;
    EXPECT_CALL(*mockResourceView_, GetResourceInfo).WillOnce(Return(info));
    EXPECT_CALL(*mockGroupPerformer_, DoSchedule)
        .WillOnce(Return(schedule_decision::GroupScheduleResult{StatusCode::SUCCESS, "", {}}));

    auto scheduler = std::make_shared<Scheduler>(scheduleQueueActor_->GetAID(), scheduleQueueActor_->GetAID());
    auto spec = std::make_shared<GroupSpec>();
    spec->requests = requests;
    spec->groupReqId = groupReqId;
    spec->cancelTag = litebus::Future<std::string>();
    spec->priority = false;
    auto future = scheduler->GroupScheduleDecision(spec);
    EXPECT_AWAIT_READY_FOR(future, 1000);
    auto res = future.Get();
    EXPECT_EQ(res.code, 0);
}

TEST_F(ScheduleTest, ScheduleConfirmSuccess)
{
    auto rsp = std::make_shared<messages::ScheduleResponse>();
    rsp->set_code(0);
    auto scheduler = std::make_shared<Scheduler>(scheduleQueueActor_->GetAID(), scheduleQueueActor_->GetAID());
    resource_view::InstanceInfo ins;
    auto future = scheduler->ScheduleConfirm(rsp, ins, ScheduleResult{});
    EXPECT_AWAIT_READY_FOR(future, 1000);
    EXPECT_TRUE(future.Get().IsOk());
    EXPECT_EQ(rsp->code(), 0);
}

TEST_F(ScheduleTest, ScheduleOnResourceUpdate)
{
    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("ins");
    req->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);

    resource_view::ResourceViewInfo info;
    EXPECT_CALL(*mockResourceView_, GetResourceInfo).Times(2).WillRepeatedly(Return(info));
    litebus::Future<bool> isScheduled;
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(DoAll(Assign(&isScheduled, true), Return(ScheduleResult{"", StatusCode::RESOURCE_NOT_ENOUGH, ""})))
        .WillOnce(Return(ScheduleResult{"", 0, ""}));

    auto scheduler = std::make_shared<Scheduler>(scheduleQueueActor_->GetAID(), scheduleQueueActor_->GetAID());
    auto future = scheduler->ScheduleDecision(req,  litebus::Future<std::string>());
    EXPECT_AWAIT_READY_FOR(isScheduled, 1000);
    EXPECT_TRUE(isScheduled.Get());
    EXPECT_TRUE(future.IsInit());
    ASSERT_AWAIT_TRUE([&](){
        return scheduleQueueActor_->GetQueueState() == QueueStatus::PENDING;
    });

    scheduleQueueActor_->ScheduleOnResourceUpdate();
    EXPECT_AWAIT_READY_FOR(future, 1000);
    EXPECT_EQ(future.Get().code, 0);
    ASSERT_AWAIT_TRUE([&](){
        return scheduleQueueActor_->GetQueueState() == QueueStatus::WAITING;
    });
}

TEST_F(ScheduleTest, ScheduleCancelOnPending)
{
    auto req = std::make_shared<messages::ScheduleRequest>();
    req->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);
    req->set_requestid("ins");

    resource_view::ResourceViewInfo info;
    EXPECT_CALL(*mockResourceView_, GetResourceInfo).Times(1).WillRepeatedly(Return(info));
    litebus::Future<bool> isScheduled;
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(DoAll(Assign(&isScheduled, true), Return(ScheduleResult{"", StatusCode::RESOURCE_NOT_ENOUGH, ""})));

    auto scheduler = std::make_shared<Scheduler>(scheduleQueueActor_->GetAID(), scheduleQueueActor_->GetAID());
    auto cancel = litebus::Future<std::string>();
    auto future = scheduler->ScheduleDecision(req, cancel);
    EXPECT_AWAIT_READY_FOR(isScheduled, 1000);
    EXPECT_TRUE(isScheduled.Get());
    EXPECT_EQ(scheduleQueueActor_->GetQueueState(), QueueStatus::PENDING);

    cancel.SetValue("cancel");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_TRUE(future.Get().code == StatusCode::ERR_SCHEDULE_CANCELED);
}

TEST_F(ScheduleTest, ScheduleTimeoutCancelOnPending)
{
    auto req = std::make_shared<messages::ScheduleRequest>();
    req->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);
    req->set_requestid("ins");

    resource_view::ResourceViewInfo info;
    EXPECT_CALL(*mockResourceView_, GetResourceInfo).Times(1).WillRepeatedly(Return(info));
    litebus::Future<bool> isScheduled;
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(DoAll(Assign(&isScheduled, true), Return(ScheduleResult{"", StatusCode::RESOURCE_NOT_ENOUGH, ""})));

    auto scheduler = std::make_shared<Scheduler>(scheduleQueueActor_->GetAID(), scheduleQueueActor_->GetAID());
    auto cancel = litebus::Future<std::string>();
    auto future = scheduler->ScheduleDecision(req, cancel);
    EXPECT_AWAIT_READY_FOR(isScheduled, 1000);
    EXPECT_TRUE(isScheduled.Get());
    EXPECT_EQ(scheduleQueueActor_->GetQueueState(), QueueStatus::PENDING);

    cancel.SetFailed(-1);
    EXPECT_TRUE(future.IsInit());
}

TEST_F(ScheduleTest, GroupScheduleOnCancel)
{
    std::string groupReqId = "groupReqId";
    std::vector<std::shared_ptr<messages::ScheduleRequest>> requests;
    for (int i = 0; i < 3; i++) {
        auto req = std::make_shared<messages::ScheduleRequest>();
        req->set_requestid("group-" + std::to_string(i));
        requests.emplace_back(req);
    }
    resource_view::ResourceViewInfo info;
    EXPECT_CALL(*mockResourceView_, GetResourceInfo).WillOnce(Return(info));
    litebus::Future<bool> isScheduled;
    EXPECT_CALL(*mockGroupPerformer_, DoSchedule)
        .WillOnce(DoAll(Assign(&isScheduled, true), Return
                        (schedule_decision::GroupScheduleResult{StatusCode::RESOURCE_NOT_ENOUGH, "", {}})));

    auto scheduler = std::make_shared<Scheduler>(scheduleQueueActor_->GetAID(), scheduleQueueActor_->GetAID());
    auto spec = std::make_shared<GroupSpec>();
    spec->requests = requests;
    spec->groupReqId = groupReqId;
    spec->cancelTag = litebus::Future<std::string>();
    spec->priority = false;
    spec->timeout = 100;
    auto future = scheduler->GroupScheduleDecision(spec);
    EXPECT_AWAIT_READY_FOR(isScheduled, 1000);
    EXPECT_TRUE(isScheduled.Get());
    EXPECT_EQ(scheduleQueueActor_->GetQueueState(), QueueStatus::PENDING);
    spec->cancelTag.SetValue("canceled");
    EXPECT_AWAIT_READY_FOR(future, 1000);
    auto res = future.Get();
    EXPECT_EQ(res.code, StatusCode::ERR_SCHEDULE_CANCELED);
}

TEST_F(ScheduleTest, FairnessScheduleOnResourceUpdate)
{
    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("ins");
    req->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);

    resource_view::ResourceViewInfo info;
    EXPECT_CALL(*mockResourceView_, GetResourceInfo).Times(2).WillRepeatedly(Return(info));
    litebus::Future<bool> isScheduled;
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(DoAll(Assign(&isScheduled, true), Return(ScheduleResult{"", StatusCode::RESOURCE_NOT_ENOUGH, ""})))
        .WillOnce(Return(ScheduleResult{"", 0, ""}));

    auto fairnessSchedule =
        std::make_shared<PriorityScheduler>(schedule_decision::ScheduleRecorder::CreateScheduleRecorder(), 10,
                                            PriorityPolicyType::FAIRNESS);
    fairnessSchedule->RegisterSchedulePerformer(mockInstancePerformer_, mockGroupPerformer_, mockAggregatedSchedulePerformer_);
    scheduleQueueActor_->RegisterScheduler(fairnessSchedule);
    auto scheduler = std::make_shared<Scheduler>(scheduleQueueActor_->GetAID(), scheduleQueueActor_->GetAID());
    auto future = scheduler->ScheduleDecision(req,  litebus::Future<std::string>());
    EXPECT_AWAIT_READY_FOR(isScheduled, 1000);
    EXPECT_TRUE(isScheduled.Get());
    EXPECT_TRUE(future.IsInit());
    EXPECT_EQ(fairnessSchedule->pendingQueue_->Size(), 1);
    ASSERT_AWAIT_TRUE([&](){
        return scheduleQueueActor_->GetQueueState() == QueueStatus::PENDING;
    });

    scheduleQueueActor_->ScheduleOnResourceUpdate();
    EXPECT_AWAIT_READY_FOR(future, 1000);
    EXPECT_EQ(future.Get().code, 0);
    ASSERT_AWAIT_TRUE([&](){
        return scheduleQueueActor_->GetQueueState() == QueueStatus::WAITING;
    });
}

TEST_F(ScheduleTest, FairnessScheduleCancelOnPending)
{
    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("ins");
    req->mutable_instance()->mutable_scheduleoption()->set_scheduletimeoutms(1);
    auto scheduler = std::make_shared<Scheduler>(scheduleQueueActor_->GetAID(), scheduleQueueActor_->GetAID());
    resource_view::ResourceViewInfo info;
    EXPECT_CALL(*mockResourceView_, GetResourceInfo).Times(1).WillRepeatedly(Return(info));

    // cancel, and then schedule complete
    bool isScheduled = false;
    auto cancel = std::make_shared<litebus::Promise<std::string>>();
    std::string reason = "cancel";
    litebus::Future<ScheduleResult> future;
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(DoAll(Assign(&isScheduled, true),
                        Invoke([&](const std::shared_ptr<schedule_framework::PreAllocatedContext> &_1,
                                   const resource_view::ResourceViewInfo &_2,
                                   const std::shared_ptr<schedule_decision::InstanceItem> &_3) -> void {
                            future.SetValue({ "", StatusCode::ERR_SCHEDULE_CANCELED, "" });
                        }),
                        Return(ScheduleResult{ "", StatusCode::FAILED, "" })));
    future = scheduler->ScheduleDecision(req, cancel->GetFuture());
    EXPECT_AWAIT_TRUE([&]() -> bool { return isScheduled; });
    EXPECT_TRUE(future.Get().code == StatusCode::ERR_SCHEDULE_CANCELED);

    // schedule failed, and then cancel
    isScheduled = false;
    cancel = std::make_shared<litebus::Promise<std::string>>();
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(DoAll(Assign(&isScheduled, true), Return(ScheduleResult{ "", StatusCode::FAILED, "" })));
    future = scheduler->ScheduleDecision(req, cancel->GetFuture());
    EXPECT_AWAIT_TRUE([&]() -> bool { return isScheduled; });
    cancel->SetValue(reason);
    EXPECT_TRUE(future.Get().code == StatusCode::FAILED);

    // schedule pending, and then cancel
    isScheduled = false;
    cancel = std::make_shared<litebus::Promise<std::string>>();
    EXPECT_CALL(*mockInstancePerformer_, DoSchedule)
        .WillOnce(DoAll(Assign(&isScheduled, true), Return(ScheduleResult{ "", StatusCode::RESOURCE_NOT_ENOUGH, "" })));
    future = scheduler->ScheduleDecision(req, cancel->GetFuture());
    EXPECT_AWAIT_TRUE([&]() -> bool { return isScheduled; });
    cancel->SetValue(reason);
    EXPECT_TRUE(future.Get().code == StatusCode::ERR_SCHEDULE_CANCELED);
}

}  // namespace functionsystem::test