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

#ifndef COMMON_SCHEDULE_DECISION_TIME_SORTED_QUEUE_H
#define COMMON_SCHEDULE_DECISION_TIME_SORTED_QUEUE_H
#include "schedule_queue.h"

#include "common/status/status.h"

namespace functionsystem::schedule_decision {
inline bool operator>(const std::shared_ptr<QueueItem> &r, const std::shared_ptr<QueueItem> &l)
{
    return r->CreatedTimestamp() > l->CreatedTimestamp();
}
using TimePriorityQueue = std::priority_queue<std::shared_ptr<QueueItem>, std::vector<std::shared_ptr<QueueItem>>,
                                          std::greater<std::shared_ptr<QueueItem>>>;
class TimeSortedQueue : public ScheduleQueue {
public:
    explicit TimeSortedQueue(const int &maxPriority = 1) : ScheduleQueue(maxPriority), maxPriority_(maxPriority)
    {
    }
    ~TimeSortedQueue() override = default;

    litebus::Future<Status> Enqueue(const std::shared_ptr<QueueItem> &queueItem) override;

    std::shared_ptr<QueueItem> Front() override;

    litebus::Future<Status> Dequeue() override;

    void Swap(const std::shared_ptr<ScheduleQueue> &targetQueue) override;

    void Extend(const std::shared_ptr<ScheduleQueue> &targetQueue) override;

    bool CheckIsQueueEmpty() override;

    size_t Size() override;

private:
    int maxPriority_{ 1 };
    std::set<std::string> reqIndex_; // instance requestId or group requestId
    std::unordered_map<int, TimePriorityQueue> queueMap_;
};
}  // namespace functionsystem::schedule_decision
#endif  // COMMON_SCHEDULE_DECISION_TIME_SORTED_QUEUE_H