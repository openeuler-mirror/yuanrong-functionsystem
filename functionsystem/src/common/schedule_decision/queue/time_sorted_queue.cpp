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
#include "time_sorted_queue.h"
namespace functionsystem::schedule_decision {

litebus::Future<Status> TimeSortedQueue::Enqueue(const std::shared_ptr<QueueItem> &queueItem)
{
    RETURN_STATUS_IF_NULL(queueItem, StatusCode::FAILED, "queue is nullptr");
    RETURN_STATUS_IF_TRUE(queueItem->GetRequestId().empty(), StatusCode::ERR_PARAM_INVALID,
                          "invalid request without id");
    auto priority = queueItem->GetPriority();
    RETURN_STATUS_IF_TRUE(priority > maxPriority_, StatusCode::ERR_PARAM_INVALID,
                          "priority of request is greater than maxPriority");
    reqIndex_.insert(queueItem->GetRequestId());
    queueMap_[priority].push(queueItem);
    return Status::OK();
}
std::shared_ptr<QueueItem> TimeSortedQueue::Front()
{
    for (int i = maxPriority_; i >= 0; i--) {
        auto curPriority = queueMap_.find(i);
        if (curPriority == queueMap_.end()) {
            continue;
        }
        if (!curPriority->second.empty()) {
            return curPriority->second.top();
        }
    }
    return nullptr;
}
litebus::Future<Status> TimeSortedQueue::Dequeue()
{
    for (int i = maxPriority_; i >= 0; i--) {
        auto curPriority = queueMap_.find(i);
        if (curPriority == queueMap_.end()) {
            continue;
        }
        if (!curPriority->second.empty()) {
            auto item = curPriority->second.top();
            curPriority->second.pop();
            auto reqId = item->GetRequestId();
            reqIndex_.erase(reqId);
            return Status::OK();
        }
    }
    return Status::OK();
}
void TimeSortedQueue::Swap(const std::shared_ptr<ScheduleQueue> &targetQueue)
{
    auto target = std::dynamic_pointer_cast<TimeSortedQueue>(targetQueue);
    if (target == nullptr) {
        YRLOG_WARN("failed to swap TimeSortedQueue, target can not be dynamic cast to TimeSortedQueue");
        return;
    }
    queueMap_.swap(target->queueMap_);
    maxPriority_ = target->maxPriority_;
    reqIndex_.swap(target->reqIndex_);
}
void TimeSortedQueue::Extend(const std::shared_ptr<ScheduleQueue> &targetQueue)
{
    RETURN_IF_NULL(targetQueue);
    auto target = std::dynamic_pointer_cast<TimeSortedQueue>(targetQueue);
    RETURN_IF_NULL(target);
    auto &targetMap = target->queueMap_;
    for (int i = maxPriority_; i >= 0; i--) {
        if (targetMap.find(i) == targetMap.end()) {
            continue;
        }

        // Ensure the current map has a queue for this priority level
        if (queueMap_.find(i) == queueMap_.end()) {
            queueMap_[i] = TimePriorityQueue();
        }

        // move elements from targetQueue to the current queue
        auto &cur = targetMap.at(i);
        while (!cur.empty()) {
            auto item = cur.top();
            queueMap_[i].push(item);
            cur.pop();
            reqIndex_.insert(item->GetRequestId());
        }
    }
}
bool TimeSortedQueue::CheckIsQueueEmpty()
{
    return reqIndex_.empty();
}
size_t TimeSortedQueue::Size()
{
    return reqIndex_.size();
}
}  // namespace functionsystem::schedule_decision