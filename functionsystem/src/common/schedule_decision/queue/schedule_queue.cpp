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

#include "schedule_queue.h"
#include "common/logs/logging.h"

namespace functionsystem::schedule_decision {

litebus::Future<Status> ScheduleQueue::Enqueue(const std::shared_ptr<QueueItem> &queueItem)
{
    if (queueItem == nullptr) {
        YRLOG_WARN("schedule queueItem is nullptr");
        return Status(StatusCode::FAILED, "queueItem is null");
    }
    if (queueItem->GetRequestId().empty()) {
        return Status(StatusCode::ERR_PARAM_INVALID, "get instance requestId failed");
    }
    uint16_t priority = queueItem->GetPriority();
    if (priority > maxPriority_) {
        return Status(StatusCode::ERR_PARAM_INVALID, "instance priority is greater than maxPriority");
    }
    reqIndex_.insert(queueItem->GetRequestId());
    queueMap_[priority].push_back(queueItem);
    return Status::OK();
}


litebus::Future<Status> ScheduleQueue::Dequeue()
{
    if (CheckIsQueueEmpty()) {
        return Status(StatusCode::FAILED, "queue is empty");
    }
    for (int i = maxPriority_; i >= 0; i--) {
        if (queueMap_.find(i) == queueMap_.end()) {
            continue;
        }
        if (!queueMap_[i].empty()) {
            auto item = queueMap_[i].front();
            queueMap_[i].pop_front();
            auto reqId = item->GetRequestId();
            reqIndex_.erase(reqId);
            return Status::OK();
        }
    }
    return Status::OK();
}

std::shared_ptr<QueueItem> ScheduleQueue::Front()
{
    if (CheckIsQueueEmpty()) {
        return nullptr;
    }
    for (int i = maxPriority_; i >= 0; i--) {
        if (queueMap_.find(i) == queueMap_.end()) {
            continue;
        }
        if (!queueMap_[i].empty()) {
            return queueMap_[i].front();
        }
    }
    return nullptr;
}

void ScheduleQueue::Swap(const std::shared_ptr<ScheduleQueue> &targetQueue)
{
    for (int i = maxPriority_; i >= 0; i--) {
        auto &targetMap = targetQueue->queueMap_;
        if (queueMap_.find(i) == queueMap_.end() && targetMap.find(i) == targetMap.end()) {
            continue;
        }
        if (queueMap_.find(i) == queueMap_.end()) {
            queueMap_[i] = std::deque<std::shared_ptr<QueueItem>>();
        }
        if (targetMap.find(i) == targetMap.end()) {
            targetMap[i] = std::deque<std::shared_ptr<QueueItem>>();
        }
        queueMap_[i].swap(targetMap[i]);
    }
    reqIndex_.swap(targetQueue->reqIndex_);
}

void ScheduleQueue::Extend(const std::shared_ptr<ScheduleQueue> &targetQueue)
{
    if (!targetQueue) {
        YRLOG_WARN("targetQueue is nullptr");
        return;
    }

    const auto &targetMap = targetQueue->queueMap_;
    for (int i = maxPriority_; i >= 0; i--) {
        if (targetMap.find(i) == targetMap.end()) {
            continue;
        }

        // Ensure the current queue has a deque for this priority level
        if (queueMap_.find(i) == queueMap_.end()) {
            queueMap_[i] = std::deque<std::shared_ptr<QueueItem>>();
        }

        // copy elements from targetQueue to the current queue
        for (const auto &item : targetMap.at(i)) {
            queueMap_[i].emplace_back(item);
            reqIndex_.insert(item->GetRequestId());
        }
    }
}

bool ScheduleQueue::CheckIsQueueEmpty()
{
    return reqIndex_.empty();
}

size_t ScheduleQueue::Size()
{
    return reqIndex_.size();
}

}  // namespace functionsystem::schedule_decision