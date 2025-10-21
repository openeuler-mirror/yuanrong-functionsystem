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

#ifndef DOMAIN_DECISION_PRIORITY_QUEUE_H
#define DOMAIN_DECISION_PRIORITY_QUEUE_H

#include <queue>
#include "litebus.hpp"
#include "common/schedule_decision/queue/queue_item.h"

namespace functionsystem::schedule_decision {

class ScheduleQueue {
public:
    explicit ScheduleQueue(const int &maxPriority = 1) : maxPriority_(maxPriority) {
    };
    virtual ~ScheduleQueue() = default;

    virtual litebus::Future<Status> Enqueue(const std::shared_ptr<QueueItem> &queueItem);

    virtual std::shared_ptr<QueueItem> Front();

    virtual litebus::Future<Status> Dequeue();

    virtual void Swap(const std::shared_ptr<ScheduleQueue> &targetQueue);

    virtual void Extend(const std::shared_ptr<ScheduleQueue> &targetQueue);

    virtual bool CheckIsQueueEmpty();

    virtual size_t Size();

private:
    std::unordered_map<int, std::deque<std::shared_ptr<QueueItem>>> queueMap_; // priority, queue
    std::set<std::string> reqIndex_; // instance requestId or group requestId
    int maxPriority_{1};
};
}  // namespace functionsystem::schedule_decision
#endif // DOMAIN_DECISION_PRIORITY_QUEUE_H
