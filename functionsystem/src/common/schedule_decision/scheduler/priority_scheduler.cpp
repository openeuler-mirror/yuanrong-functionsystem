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
#include "priority_scheduler.h"

#include "common/create_agent_decision/create_agent_decision.h"
#include "common/schedule_decision/scheduler/priority_policy/fairness_policy.h"
#include "common/schedule_decision/scheduler/priority_policy/fifo_policy.h"
#include "common/schedule_decision/queue/time_sorted_queue.h"
#include "common/scheduler_framework/utils/label_affinity_utils.h"

namespace functionsystem::schedule_decision {

PriorityScheduler::PriorityScheduler(const std::shared_ptr<ScheduleRecorder> &recorder,
                                uint16_t maxPriority, PriorityPolicyType priorityPolicyType,
                                const std::string &aggregatedStrategy)
    : ScheduleStrategy(), recorder_(recorder), maxPriority_(maxPriority)
{
    YRLOG_DEBUG("priorityScheduler has created，maxPriority:{},aggregatedStrategy:{}", maxPriority, aggregatedStrategy);
    if (aggregatedStrategy ==  NO_AGGREGATE_STRATEGY) {
        runningQueue_ = std::make_shared<TimeSortedQueue>(maxPriority);
        pendingQueue_ = std::make_shared<TimeSortedQueue>(maxPriority);
    } else {
        runningQueue_ = std::make_shared<AggregatedQueue>(maxPriority, aggregatedStrategy);
        pendingQueue_ = std::make_shared<AggregatedQueue>(maxPriority, aggregatedStrategy);
    }
    RegistPriorityPolicy(priorityPolicyType);
}


void PriorityScheduler::RegistPriorityPolicy(PriorityPolicyType priorityPolicyType)
{
    switch (priorityPolicyType) {
        case PriorityPolicyType::FIFO:
            priorityPolicy_ = std::make_shared<FifoPolicy>();
            break;
        case PriorityPolicyType::FAIRNESS:
            priorityPolicy_ = std::make_shared<FairnessPolicy>();
            break;
        default:
            priorityPolicy_ = std::make_shared<FifoPolicy>();
            break;
    }
}

ScheduleType PriorityScheduler::GetScheduleType()
{
    return ScheduleType::PRIORITY;
}

litebus::Future<Status> PriorityScheduler::Enqueue(const std::shared_ptr<QueueItem> &item)
{
    ASSERT_IF_NULL(runningQueue_);
    ASSERT_IF_NULL(pendingQueue_);

    if (!priorityPolicy_->CanSchedule(item)) {
        YRLOG_DEBUG("{}|Exists a similar pending request, push it to pending queue", item->GetRequestId());
        return pendingQueue_->Enqueue(item);
    } else {
        return runningQueue_->Enqueue(item);
    }
}

/*
 * Moves requests from the pending queue to the running queue, activating them for processing.
 * The priority of requests in the pending queue is considered higher than those in the running queue.
 */
void PriorityScheduler::ActivatePendingRequests()
{
    ASSERT_IF_NULL(runningQueue_);
    ASSERT_IF_NULL(pendingQueue_);
    if (pendingQueue_->CheckIsQueueEmpty()) {
        YRLOG_DEBUG("pending queue is empty");
        return;
    }
    pendingQueue_->Extend(runningQueue_);
    runningQueue_ = std::move(pendingQueue_);
    pendingQueue_ = std::make_shared<TimeSortedQueue>(maxPriority_);
    priorityPolicy_->ClearPendingInfos();
}

void PriorityScheduler::HandleResourceInfoUpdate(const resource_view::ResourceViewInfo &resourceInfo)
{
    resourceInfo_ = resourceInfo;
    preContext_ = std::make_shared<schedule_framework::PreAllocatedContext>();
    preContext_->schedulerLevel = resourceInfo_.schedulerLevel;
    preContext_->allLocalLabels = resourceInfo_.allLocalLabels;
}

void PriorityScheduler::ConsumeRunningQueue()
{
    ASSERT_IF_NULL(runningQueue_);
    if (runningQueue_->CheckIsQueueEmpty()) {
        YRLOG_WARN("running queue is empty");
        return;
    }

    while (!runningQueue_->CheckIsQueueEmpty()) {
        DoConsume();
    }
}

void PriorityScheduler::DoConsume()
{
    ASSERT_IF_NULL(runningQueue_);
    auto item = runningQueue_->Front();
    if (item == nullptr) {
        YRLOG_WARN("item is null");
        return;
    }
    // if cancel, skip
    if (item->cancelTag.IsOK()) {
        YRLOG_WARN("{}|schedule is canceled, reason: {}", item->GetRequestId(), item->cancelTag.Get());
        item->AssociateFailure(StatusCode::ERR_SCHEDULE_CANCELED, item->cancelTag.Get());
        runningQueue_->Dequeue();
        return;
    }
    ASSERT_IF_NULL(pendingQueue_);
    ASSERT_IF_NULL(priorityPolicy_);
    if (!priorityPolicy_->CanSchedule(item)) {
        YRLOG_DEBUG("{}|Exists a similar pending request, push it to pending queue", item->GetRequestId());
        pendingQueue_->Enqueue(item);
        runningQueue_->Dequeue();
        return;
    }
    // schedule
    if (item->GetItemType() == QueueItemType::INSTANCE) {
        YRLOG_INFO("{}|start instance schedule", item->GetRequestId());
        auto instance = std::dynamic_pointer_cast<InstanceItem>(item);
        ASSERT_IF_NULL(instancePerformer_);
        priorityPolicy_->PrepareForScheduling(instance);
        auto future = instancePerformer_->DoSchedule(preContext_, resourceInfo_, instance);
        OnScheduleDone(future, instance);
        runningQueue_->Dequeue();
    } else if (item->GetItemType() == QueueItemType::GROUP) {
        YRLOG_INFO("{}|start group schedule", item->GetRequestId());
        auto group = std::dynamic_pointer_cast<GroupItem>(item);
        if (group->groupReqs.empty()) {
            YRLOG_WARN("{}|schedule requests are empty", item->GetRequestId());
            group->groupPromise->SetValue(GroupScheduleResult{ 0, "", {} });
            runningQueue_->Dequeue();
            return;
        }
        ASSERT_IF_NULL(groupPerformer_);
        priorityPolicy_->PrepareForScheduling(group);
        auto future = groupPerformer_->DoSchedule(preContext_, resourceInfo_, group);
        OnScheduleDone(future, group);
        runningQueue_->Dequeue();
    } else if (item->GetItemType() == QueueItemType::AGGREGATED_ITEM) {
        YRLOG_INFO("start AggregatedItem schedule (reqId={}, priority={})", item->GetRequestId(), item->GetPriority());
        auto aggregatedItem = std::dynamic_pointer_cast<AggregatedItem>(item);
        // if cancel, skip
        auto items = aggregatedItem->reqQueue;
        while (!items->empty()) {
            auto instanceItem = items->front();
            if (!instanceItem->cancelTag.IsOK()) {
                break; // obtains the first non-cancel req
            }
            YRLOG_WARN("schedule (reqId={}) is canceled, reason: {}", instanceItem->GetRequestId(),
                instanceItem->cancelTag.Get());
            items->pop_front();
            if (items->empty()) {
                runningQueue_->Dequeue();
                return; // all reqs in aggregateItem are canceled,no need to go into the following scheduling process
            }
        }
        ASSERT_IF_NULL(aggregatedPerformer_);
        priorityPolicy_->PrepareForScheduling(items->front());
        auto scheduleResults = aggregatedPerformer_->DoSchedule(preContext_, resourceInfo_, aggregatedItem);
        for (uint32_t i = 0; i < scheduleResults->size(); ++i) {
            auto scheResult = (*scheduleResults)[i];
            auto instance = items->front();
            OnScheduleDone(scheResult, instance);
            items->pop_front();
        }
        if (items->empty()) {
            runningQueue_->Dequeue();
        }
    }
}

void PriorityScheduler::OnScheduleDone(const litebus::Future<ScheduleResult> &future,
                                       const std::shared_ptr<InstanceItem> &instance)
{
    auto &result = future.Get();
    if (!instance->cancelTag.IsInit()) {
        std::string reason = instance->cancelTag.IsOK() ? instance->cancelTag.Get() : "timeout";
        YRLOG_WARN("{}|instance schedule is canceled (reason: {}), but schedule has completed, need to rollback",
                   instance->GetRequestId(), reason);
        instance->AssociateFailure(StatusCode::ERR_SCHEDULE_CANCELED, reason);
        ASSERT_IF_NULL(instancePerformer_);
        instancePerformer_->RollBack(preContext_, instance, result);
        EraseRecord(instance);
        return;
    }
    auto &resCode = result.code;
    const auto &timeout = instance->scheduleReq->instance().scheduleoption().scheduletimeoutms();
    ASSERT_IF_NULL(priorityPolicy_);
    if (priorityPolicy_->NeedSuspend(resCode, timeout)
        && !NeedCreateAgentInDomain(instance->scheduleReq->instance(), resCode) && recorder_ != nullptr) {
        YRLOG_WARN("{}|instance schedule resource not enough (resCode: {}), push it to pending queue",
                   instance->GetRequestId(), resCode);
        recorder_->RecordScheduleErr(instance->GetRequestId(), Status(static_cast<StatusCode>(resCode), result.reason));
        instance->TagFailure();
        pendingQueue_->Enqueue(instance);
        priorityPolicy_->StorePendingInfo(instance);
    } else {
        YRLOG_INFO("{}|instance schedule complete, resCode: {}", instance->GetRequestId(), resCode);
        EraseRecord(instance);
        instance->schedulePromise->Associate(future);
    }
}

void PriorityScheduler::OnScheduleDone(const litebus::Future<GroupScheduleResult> &future,
                                       const std::shared_ptr<GroupItem> &group)
{
    auto &result = future.Get();
    ASSERT_IF_NULL(groupPerformer_);
    if (!group->cancelTag.IsInit()) {
        std::string reason = group->cancelTag.IsOK() ? group->cancelTag.Get() : "timeout";
        YRLOG_WARN("{}|group schedule is canceled (reason: {}), but schedule has completed, need to rollback",
                   group->GetRequestId(), reason);
        group->AssociateFailure(StatusCode::ERR_SCHEDULE_CANCELED, reason);
        groupPerformer_->RollBack(preContext_, group, result);
        EraseRecord(group);
        return;
    }
    auto &resCode = result.code;
    ASSERT_IF_NULL(priorityPolicy_);
    if (priorityPolicy_->NeedSuspend(resCode, group->GetTimeout()) && recorder_ != nullptr)  {
        YRLOG_WARN("{}|group schedule resource not enough (resCode: {}), push it to pending queue",
                   group->GetRequestId(), resCode);
        groupPerformer_->RollBack(preContext_, group, result);
        recorder_->RecordScheduleErr(group->GetRequestId(), Status(static_cast<StatusCode>(resCode), result.reason));
        group->TagFailure();
        pendingQueue_->Enqueue(group);
        priorityPolicy_->StorePendingInfo(group);
    } else {
        YRLOG_INFO("{}|group schedule complete, resCode: {}", group->GetRequestId(), resCode);
        EraseRecord(group);
        group->groupPromise->Associate(result);
        if (resCode != static_cast<int32_t>(StatusCode::SUCCESS)) {
            groupPerformer_->RollBack(preContext_, group, result);
        }
    }
}

bool PriorityScheduler::CheckIsRunningQueueEmpty()
{
    ASSERT_IF_NULL(runningQueue_);
    return runningQueue_->CheckIsQueueEmpty();
}

bool PriorityScheduler::CheckIsPendingQueueEmpty()
{
    ASSERT_IF_NULL(pendingQueue_);
    return pendingQueue_->CheckIsQueueEmpty();
}

void PriorityScheduler::EraseRecord(const std::shared_ptr<QueueItem> &item)
{
    if (recorder_ == nullptr) {
        return;
    }
    if (item->HasFailed()) {
        recorder_->EraseScheduleErr(item->GetRequestId());
    }
}
}  // namespace functionsystem::schedule_decision