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

#ifndef DOMAIN_DECISION_FAIRNESS_SCHEDULER_H
#define DOMAIN_DECISION_FAIRNESS_SCHEDULER_H

#include "common/resource_view/resource_type.h"
#include "common/schedule_decision/performer/group_schedule_performer.h"
#include "common/schedule_decision/performer/instance_schedule_performer.h"
#include "common/schedule_decision/queue/aggregated_queue.h"
#include "common/schedule_decision/schedule_recorder/schedule_recorder.h"
#include "common/schedule_decision/scheduler/priority_policy/priority_policy.h"
#include "common/schedule_decision/scheduler/schedule_strategy.h"
#include "common/schedule_plugin/common/preallocated_context.h"
#include "common/scheduler_framework/framework/framework_impl.h"

namespace functionsystem::schedule_decision {
class PriorityScheduler : public ScheduleStrategy {
public:
    explicit PriorityScheduler(const std::shared_ptr<ScheduleRecorder> &recorder = nullptr, uint16_t maxPriority = 0,
                               PriorityPolicyType priorityPolicyType = PriorityPolicyType::FIFO,
                               const std::string &aggregatedStrategy = "no_aggregate");

    ~PriorityScheduler() override = default;

    litebus::Future<Status> Enqueue(const std::shared_ptr<QueueItem> &item) override;
    bool CheckIsRunningQueueEmpty() override;
    bool CheckIsPendingQueueEmpty() override;
    ScheduleType GetScheduleType() override;
    void ConsumeRunningQueue() override;
    void HandleResourceInfoUpdate(const resource_view::ResourceViewInfo &resourceInfo) override;
    void ActivatePendingRequests() override;

    void RegistPriorityPolicy(PriorityPolicyType priorityPolicyType);

private:
    void DoConsume();

    void OnScheduleDone(const litebus::Future<ScheduleResult> &future, const std::shared_ptr<InstanceItem> &instance);

    void OnScheduleDone(const litebus::Future<GroupScheduleResult> &future, const std::shared_ptr<GroupItem> &group);

    void EraseRecord(const std::shared_ptr<QueueItem> &item);

    std::shared_ptr<PriorityPolicy> priorityPolicy_;
    std::shared_ptr<ScheduleQueue> runningQueue_;
    std::shared_ptr<ScheduleQueue> pendingQueue_;
    std::shared_ptr<schedule_framework::PreAllocatedContext> preContext_;
    resource_view::ResourceViewInfo resourceInfo_;
    std::shared_ptr<ScheduleRecorder> recorder_;
    int maxPriority_;
};
}  // namespace functionsystem::schedule_decision
#endif  // DOMAIN_DECISION_FAIRNESS_SCHEDULER_H
