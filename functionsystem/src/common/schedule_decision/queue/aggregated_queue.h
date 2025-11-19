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
#ifndef AGGRAVATED_QUEUE_H
#define AGGRAVATED_QUEUE_H

#include "common/constants/constants.h"
#include "common/resource_view/resource_type.h"
#include "common/schedule_decision/queue/queue_item.h"
#include "schedule_queue.h"

namespace functionsystem::schedule_decision {

enum class AggregatedStrategy { NO_AGGREGATE = 0, STRICTLY = 1, RELAXED = 2 };

class AggregatedQueue : public ScheduleQueue {
public:
    explicit AggregatedQueue(const int &maxPriority, const std::string &strategy)
        : strategy_(strategy), maxPriority_(maxPriority), frontItem_(nullptr){};

    ~AggregatedQueue() override = default;

    litebus::Future<Status> Enqueue(const std::shared_ptr<QueueItem> &queueItem) override;

    std::shared_ptr<QueueItem> Front() override;

    litebus::Future<Status> Dequeue() override;

    void Swap(const std::shared_ptr<ScheduleQueue> &targetQueue) override;

    void Extend(const std::shared_ptr<ScheduleQueue> &targetQueue) override;

    bool CheckIsQueueEmpty() override;

    size_t Size() override;

    std::string GenerateAggregatedKey(const std::shared_ptr<InstanceItem> &instance);

    bool IsItemNeedAggregate(const std::shared_ptr<QueueItem> &queueItem);

    Status CheckItemValid(const std::shared_ptr<QueueItem> &queueItem);

private:
    int queueSize_{ 0 };  // item number not req number
    std::string strategy_;
    int maxPriority_;
    std::shared_ptr<QueueItem> frontItem_;  // avoid repeatedly traversing aggregatedReqs
    int frontPriority_;                     // when aggregateItem.reqQueue is empty,the value is used
    std::unordered_map<uint16_t, std::deque<std::shared_ptr<QueueItem>>> aggregatedReqs;
    std::unordered_map<std::string, std::shared_ptr<AggregatedItem>> aggregatedItemIndex;  // AggregatedItem index
};
}  // namespace functionsystem::schedule_decision
#endif  // AGGRAVATED_QUEUE_H
