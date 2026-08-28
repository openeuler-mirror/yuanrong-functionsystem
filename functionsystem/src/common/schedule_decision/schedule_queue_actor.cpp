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
#include "schedule_queue_actor.h"

#include "async/asyncafter.hpp"
#include "async/collect.hpp"
#include "async/defer.hpp"
#include "common/logs/logging.h"

namespace functionsystem::schedule_decision {

/*
 * The state transition map defines the rules for transitioning between queue states based on
 * whether the running queue and pending queue are empty or not.
 * Each entry specifies the following:
 * 1. Condition 1: Whether the running queue is empty (`isRunningQueueEmpty`).
 * 2. Condition 2: Whether the pending queue is empty (`isPendingQueueEmpty`).
 * 3. The new state (`QueueStatus`) after the transition.
 * 4. Whether a consumer request is needed for the new state (`needRequestConsumer`).
 *
 * The transitions are as follows:
 * 1. Both queues are empty: Transition to `WAITING` state (no consumer request needed).
 * 2. Running queue is empty, pending queue is not: Transition to `PENDING` state (no consumer request needed).
 * 3. Both queues are non-empty: Transition to `RUNNING` state (consumer request needed).
 * 4. Running queue is non-empty, pending queue is empty: Transition to `RUNNING` state (consumer request needed).
 */
static const QueueStateTransition STATE_TRANSITIONS_MAP[] = {
    {true,  true,  QueueStatus::WAITING,  false},
    {true,  false, QueueStatus::PENDING,  false},
    {false, false, QueueStatus::RUNNING,  true},
    {false, true,  QueueStatus::RUNNING,  true},
};

const int64_t RESOURCE_IDLE_TIME = 30000;
const int64_t SNAPSHOT_INIT_RETRY_TIME = 1;
const size_t SNAPSHOT_ROUND_REQUEST_LIMIT = 256;
const auto SNAPSHOT_ROUND_TIME_LIMIT = std::chrono::milliseconds(10);

ScheduleQueueActor::ScheduleQueueActor(const std::string &name)
    : ActorBase(name + SCHEDULE_QUEUE_ACTOR_NAME_POSTFIX)
{
    status_ = QueueStatus::WAITING;
}

void ScheduleQueueActor::RegisterResourceView(const std::shared_ptr<resource_view::ResourceView> &resourceView)
{
    resourceView_ = resourceView;
    if (resourceView == nullptr) {
        return;
    }
    scheduleSnapshotStore_ = resourceView->GetScheduleSnapshotStore();
    if (type_ == AllocateType::ALLOCATION) {
        return;
    }
    resourceView_->AddResourceUpdateHandler([aid(GetAID())]() {
        litebus::Async(aid, &ScheduleQueueActor::ScheduleOnResourceUpdate);
    });
}

void ScheduleQueueActor::RegisterScheduler(const std::shared_ptr<ScheduleStrategy> &scheduler)
{
    scheduleStrategy_ = scheduler;
}

litebus::Future<Status> ScheduleQueueActor::RegisterPolicy(const std::string &policyName)
{
    ASSERT_IF_NULL(scheduleStrategy_);
    return scheduleStrategy_->RegisterPolicy(policyName);
}

void ScheduleQueueActor::TransitionSchedulerQueueState()
{
    bool isRunningQueueEmpty = scheduleStrategy_->CheckIsRunningQueueEmpty();
    bool isPendingQueueEmpty = scheduleStrategy_->CheckIsPendingQueueEmpty();

    auto stateTransition = STATE_TRANSITIONS_MAP[0];
    for (const auto &transition : STATE_TRANSITIONS_MAP) {
        if (transition.isRunningQueueEmpty == isRunningQueueEmpty &&
            transition.isPendingQueueEmpty == isPendingQueueEmpty) {
            stateTransition = transition;
            break;
        }
    }

    if (status_ == stateTransition.newStatus) {
        return;
    }
    YRLOG_INFO("ScheduleQueueActor has changed its status from {} to {}.",
               std::to_string(static_cast<int32_t>(status_)),
               std::to_string(static_cast<int32_t>(stateTransition.newStatus)));
    status_ = stateTransition.newStatus;
    // Only when a status change occurs and the new status requires a request consumption,
    // will a consumer request be triggered.
    if (stateTransition.needRequestConsumer) {
        litebus::Async(GetAID(), &ScheduleQueueActor::RequestConsumer);
    }
}

void ScheduleQueueActor::HandlePendingRequests()
{
    if (status_ == QueueStatus::PENDING && isNewResourceAvailable_) {
        YRLOG_DEBUG("Activate pending requests, waiting for processing.");
        ASSERT_IF_NULL(scheduleStrategy_);
        scheduleStrategy_->ActivatePendingRequests();
        TransitionSchedulerQueueState();
    }
}

void ScheduleQueueActor::ScheduleOnResourceUpdate()
{
    litebus::TimerTools::Cancel(idleTimer_);
    isNewResourceAvailable_ = true;
    HandlePendingRequests();
    // If resources are not updated for a long time, the pending queue cannot be consumed and cancel requests may remain
    // in the queue.
    idleTimer_ = litebus::AsyncAfter(RESOURCE_IDLE_TIME, GetAID(), &ScheduleQueueActor::ScheduleOnResourceUpdate);
}

void ScheduleQueueActor::UpdateResourceInfo(const litebus::Future<resource_view::ResourceViewInfo> &resourceFuture)
{
    if (resourceFuture.IsError()) {
        YRLOG_WARN("Resource future is error");
        return;
    }
    auto &resourceInfo = resourceFuture.Get();
    ASSERT_IF_NULL(scheduleStrategy_);
    scheduleStrategy_->HandleResourceInfoUpdate(resourceInfo);
    isNewResourceAvailable_ = false;
}

litebus::Future<ScheduleResult> ScheduleQueueActor::ScheduleDecision(
    const std::shared_ptr<messages::ScheduleRequest> &req, const litebus::Future<std::string> &cancelTag)
{
    return EnqueueScheduleRequest(req, cancelTag, "");
}

litebus::Future<ScheduleResult> ScheduleQueueActor::RetryScheduleDecision(
    const std::shared_ptr<messages::ScheduleRequest> &req,
    const litebus::Future<std::string> &cancelTag,
    const std::string &conflictedUnitID)
{
    ASSERT_IF_NULL(scheduleStrategy_);
    scheduleStrategy_->HandleScheduleConflict(req->requestid(), req->instance(), conflictedUnitID);
    return EnqueueScheduleRequest(req, cancelTag, conflictedUnitID);
}

litebus::Future<ScheduleResult> ScheduleQueueActor::EnqueueScheduleRequest(
    const std::shared_ptr<messages::ScheduleRequest> &req,
    const litebus::Future<std::string> &cancelTag,
    const std::string &conflictedUnitID)
{
    auto promise = std::make_shared<litebus::Promise<ScheduleResult>>();
    auto item = std::make_shared<InstanceItem>(req, promise, cancelTag);
    item->conflictedUnitID = conflictedUnitID;
    ASSERT_IF_NULL(scheduleStrategy_);
    auto res = scheduleStrategy_->Enqueue(item).Get();
    if (res.IsError()) {
        YRLOG_ERROR("enqueue failed, reason is {}", res.GetMessage());
        return ScheduleResult{"", res.StatusCode(), res.GetMessage(), {}, "", {}, {}};
    }
    // update schedule status, to avoid multiple get resource, until this consume is completed.
    TransitionSchedulerQueueState();
    item->cancelTag.OnComplete(litebus::Defer(GetAID(), &ScheduleQueueActor::OnCancelInstanceSchedule,
                                              std::placeholders::_1, promise));
    return promise->GetFuture();
}

void ScheduleQueueActor::OnCancelInstanceSchedule(const litebus::Future<std::string> &cancelReason,
                                                  const std::shared_ptr<litebus::Promise<ScheduleResult>> &promise)
{
    if (cancelReason.IsError()) {
        return;
    }
    promise->Associate(ScheduleResult{ "", StatusCode::ERR_SCHEDULE_CANCELED, cancelReason.Get(), {}, "", {} });
}

void ScheduleQueueActor::OnCancelGroupSchedule(const litebus::Future<std::string> &cancelReason,
                                               const std::shared_ptr<litebus::Promise<GroupScheduleResult>> &promise)
{
    if (cancelReason.IsError()) {
        return;
    }
    promise->Associate(GroupScheduleResult{ StatusCode::ERR_SCHEDULE_CANCELED, cancelReason.Get(), {} });
}

litebus::Future<GroupScheduleResult> ScheduleQueueActor::GroupScheduleDecision(const std::shared_ptr<GroupSpec> &spec)
{
    const auto &requests = spec->requests;
    auto cancelTag = spec->cancelTag;
    auto groupReqId = spec->groupReqId;
    if (spec->requests.empty()) {
        return GroupScheduleResult{ 0, "", {} };
    }
    auto promise = std::make_shared<litebus::Promise<GroupScheduleResult>>();
    std::vector<std::shared_ptr<InstanceItem>> instanceItems;
    for (const auto &request : requests) {
        auto instancePromise = std::make_shared<litebus::Promise<ScheduleResult>>();
        auto item = std::make_shared<InstanceItem>(request, instancePromise, cancelTag);
        instanceItems.push_back(item);
    }
    auto item = std::make_shared<GroupItem>(std::move(instanceItems), promise, groupReqId, cancelTag, spec->rangeOpt,
                                            spec->timeout);
    item->groupSchedulePolicy = spec->groupSchedulePolicy;
    ASSERT_IF_NULL(scheduleStrategy_);
    auto res = scheduleStrategy_->Enqueue(item).Get();
    if (res.IsError()) {
        YRLOG_INFO("{}|enqueue failed, reason is {}", groupReqId, res.GetMessage());
        return GroupScheduleResult{ StatusCode::FAILED, res.GetMessage(), {} };
    }
    // update schedule status, to avoid multiple get resource, until this consume is completed.
    TransitionSchedulerQueueState();
    item->cancelTag.OnComplete(
        litebus::Defer(GetAID(), &ScheduleQueueActor::OnCancelGroupSchedule, std::placeholders::_1, promise));
    return promise->GetFuture();
}

litebus::Future<Status> ScheduleQueueActor::ScheduleConfirm(const std::shared_ptr<messages::ScheduleResponse> &rsp,
                                                            const resource_view::InstanceInfo &ins)
{
    if (rsp == nullptr) {
        return Status(StatusCode::FAILED, "null schedule response");
    }
    // should be replaced by more specific errcode
    if (rsp->code() == static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH) ||
        rsp->code() == static_cast<int32_t>(StatusCode::ERR_RESOURCE_NOT_ENOUGH)) {
        rsp->set_code(static_cast<int32_t>(StatusCode::SCHEDULE_CONFLICTED));
    }

    if (type_ == AllocateType::ALLOCATION) {
        if (rsp->code() == static_cast<int32_t>(StatusCode::SUCCESS)) {
            return Status::OK();
        }
        auto instanceID = ins.instanceid();
        YRLOG_WARN("req({}) schedule instance({}) failed code({}) message({}). to delete instance from resource view",
                   rsp->requestid(), instanceID, rsp->code(), rsp->message());
        ASSERT_IF_NULL(resourceView_);
        (void)resourceView_->DeleteInstances({ instanceID })
            .OnComplete([rsp, instanceID](const litebus::Future<Status> &status) {
                if (status.IsError()) {
                    YRLOG_WARN("req({}) schedule instance({}) failed code({}) message({}). delete instance failed.{}",
                               rsp->requestid(), instanceID, rsp->code(), rsp->message(), status.GetErrorCode());
                    return;
                }
                YRLOG_WARN("req({}) schedule instance({}) failed code({}) message({}). delete instance {}",
                           rsp->requestid(), instanceID, rsp->code(), rsp->message(), status.Get().ToString());
            });
        return Status::OK();
    }
    return Status::OK();
}

void ScheduleQueueActor::RequestConsumer()
{
    ASSERT_IF_NULL(scheduleStrategy_);
    if (scheduleStrategy_->UsesScheduleSnapshot()) {
        return DoConsumeWithSnapshot();
    }
    // resource not updated, directly to consume
    if (!isNewResourceAvailable_ && type_ == AllocateType::PRE_ALLOCATION) {
        return DoConsumeWithCurrentInfo();
    }
    ASSERT_IF_NULL(resourceView_);
    (void)resourceView_->GetResourceInfo().OnComplete(
        litebus::Defer(GetAID(), &ScheduleQueueActor::DoConsumeWithLatestInfo, std::placeholders::_1));
}

void ScheduleQueueActor::DoConsumeWithSnapshot()
{
    ASSERT_IF_NULL(scheduleStrategy_);
    ASSERT_IF_NULL(scheduleSnapshotStore_);
    const auto now = std::chrono::steady_clock::now();
    if (snapshotRoundActive_ &&
        (snapshotRoundConsumed_ >= SNAPSHOT_ROUND_REQUEST_LIMIT ||
         now - snapshotRoundStarted_ >= SNAPSHOT_ROUND_TIME_LIMIT)) {
        snapshotRoundActive_ = false;
    }
    // A completed aggregate can make its futures ready before the client
    // replenishes the fixed-inflight window. Do not open an empty snapshot
    // round in that gap; the next request either reuses the still-bounded
    // round or refreshes it after N requests / T time.
    if (scheduleStrategy_->CheckIsRunningQueueEmpty()) {
        TransitionSchedulerQueueState();
        HandlePendingRequests();
        return;
    }
    if (!snapshotRoundActive_) {
        auto snapshot = scheduleSnapshotStore_->Load();
        if (snapshot == nullptr) {
            // ResourceViewActor publishes the initial snapshot asynchronously.
            // Keep the queued request in place and retry without issuing a
            // ResourceView mailbox read.
            litebus::AsyncAfter(SNAPSHOT_INIT_RETRY_TIME, GetAID(), &ScheduleQueueActor::RequestConsumer);
            return;
        }
        auto status = scheduleStrategy_->BeginScheduleRound(snapshot);
        if (!status.IsOk()) {
            YRLOG_ERROR("failed to begin Unit schedule round: {}", status.ToString());
            return;
        }
        snapshotRoundActive_ = true;
        snapshotRoundConsumed_ = 0;
        snapshotRoundStarted_ = now;
        isNewResourceAvailable_ = false;
    }
    const auto remaining = SNAPSHOT_ROUND_REQUEST_LIMIT - snapshotRoundConsumed_;
    snapshotRoundConsumed_ += scheduleStrategy_->ConsumeRunningQueue(remaining);
    if (snapshotRoundConsumed_ >= SNAPSHOT_ROUND_REQUEST_LIMIT ||
        std::chrono::steady_clock::now() - snapshotRoundStarted_ >= SNAPSHOT_ROUND_TIME_LIMIT) {
        snapshotRoundActive_ = false;
    }
    if (scheduleStrategy_->CheckIsRunningQueueEmpty()) {
        TransitionSchedulerQueueState();
        HandlePendingRequests();
        return;
    }
    // Yield to resource notifications/cancellation and pin the latest
    // immutable snapshot for the next bounded round.
    if (diagnosticsEnabled_) {
        snapshotSelfYields_.fetch_add(1, std::memory_order_relaxed);
    }
    litebus::Async(GetAID(), &ScheduleQueueActor::DoConsumeWithSnapshot);
}

void ScheduleQueueActor::DoConsumeWithLatestInfo(
    const litebus::Future<resource_view::ResourceViewInfo> &resourceFuture)
{
    YRLOG_INFO("Use the latest resourceview for scheduling");
    UpdateResourceInfo(resourceFuture);
    ASSERT_IF_NULL(scheduleStrategy_);
    scheduleStrategy_->ConsumeRunningQueue(legacyRoundRequestLimit_);

    // After the current queue consumption is complete, a consumption is initiated asynchronously to prevent new
    // queue requests from using new scheduling contexts during the consumption period and reduce domain scheduling
    // conflicts in concurrent scenarios.
    if (diagnosticsEnabled_) {
        legacySelfYields_.fetch_add(1, std::memory_order_relaxed);
    }
    litebus::Async(GetAID(), legacyRoundRequestLimit_ == std::numeric_limits<size_t>::max()
                                 ? &ScheduleQueueActor::DoConsumeWithCurrentInfo
                                 : &ScheduleQueueActor::RequestConsumer);
}

void ScheduleQueueActor::DoConsumeWithCurrentInfo()
{
    ASSERT_IF_NULL(scheduleStrategy_);

    // In a consumption queue request initiated asynchronously, if the queue is still empty which means no new
    // request enters the queue during the previous round of scheduling. In this case, the system exits recursively.
    if (scheduleStrategy_->CheckIsRunningQueueEmpty()) {
        snapshotRoundActive_ = false;
        TransitionSchedulerQueueState();
        // Process pending requests before exiting
        HandlePendingRequests();
        return;
    }

    YRLOG_INFO("schedule queue is not empty. continue to consuming schedule request");
    scheduleStrategy_->ConsumeRunningQueue(legacyRoundRequestLimit_);

    if (diagnosticsEnabled_) {
        legacySelfYields_.fetch_add(1, std::memory_order_relaxed);
    }
    litebus::Async(GetAID(), legacyRoundRequestLimit_ == std::numeric_limits<size_t>::max()
                                 ? &ScheduleQueueActor::DoConsumeWithCurrentInfo
                                 : &ScheduleQueueActor::RequestConsumer);
}

}  // namespace functionsystem::schedule_decision
