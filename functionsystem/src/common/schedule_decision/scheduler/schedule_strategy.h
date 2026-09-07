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

#ifndef DOMAIN_DECISION_SCHEDULE_STRATEGY_H
#define DOMAIN_DECISION_SCHEDULE_STRATEGY_H

#include "common/resource_view/resource_type.h"
#include "common/schedule_decision/performer/group_schedule_performer.h"
#include "common/schedule_decision/performer/instance_schedule_performer.h"
#include "common/schedule_decision/performer/aggregated_schedule_performer.h"
#include "common/schedule_plugin/common/preallocated_context.h"
#include "common/scheduler_framework/framework/framework_impl.h"

namespace functionsystem::schedule_decision {

enum class QueueStatus { WAITING, RUNNING, PENDING };

class ScheduleStrategy {
public:
    ScheduleStrategy() = default;
    virtual ~ScheduleStrategy() = default;

    virtual litebus::Future<Status> Enqueue(const std::shared_ptr<QueueItem> &item) = 0;
    virtual bool CheckIsRunningQueueEmpty() = 0;
    virtual bool CheckIsPendingQueueEmpty() = 0;
    virtual ScheduleType GetScheduleType() = 0;

    virtual void ConsumeRunningQueue() = 0;
    virtual size_t ConsumeRunningQueue(size_t maxRequests)
    {
        ConsumeRunningQueue();
        return maxRequests;
    }
    virtual void HandleResourceInfoUpdate(const resource_view::ResourceViewInfo &resourceInfo) = 0;
    virtual void ActivatePendingRequests() = 0;
    virtual bool UsesScheduleSnapshot() const
    {
        return false;
    }
    virtual Status BeginScheduleRound(const resource_view::ScheduleSnapshotPtr &)
    {
        return Status(StatusCode::ERR_SCHEDULE_PLUGIN_CONFIG, "scheduler does not support Unit snapshots");
    }
    virtual void HandleScheduleConflict(const std::string &, const resource_view::InstanceInfo &,
                                        const std::string &)
    {
    }
    void SetPlacementPolicy(const std::string &policy);

    litebus::Future<Status> RegisterPolicy(const std::string &policyName);
    virtual void RegisterSchedulePerformer(const std::shared_ptr<resource_view::ResourceView> &resourceView,
                                           const std::shared_ptr<schedule_framework::Framework> &framework,
                                           const PreemptInstancesFunc &func,
                                           const AllocateType &type = AllocateType::PRE_ALLOCATION);

    void RegisterSchedulePerformer(const std::shared_ptr<InstanceSchedulePerformer> &instancePerformer,
                                   const std::shared_ptr<GroupSchedulePerformer> &groupPerformer,
                                   const std::shared_ptr<AggregatedSchedulePerformer> &aggregatedPerformer
                                   );

protected:
    virtual void OnFrameworkPoliciesChanged() {}

    std::shared_ptr<InstanceSchedulePerformer> instancePerformer_;
    std::shared_ptr<GroupSchedulePerformer> groupPerformer_;
    std::shared_ptr<schedule_framework::Framework> framework_;
    std::shared_ptr<AggregatedSchedulePerformer> aggregatedPerformer_;
    std::string placementPolicy_{ "binpack" };
};
} // namespace functionsystem::schedule_decision
#endif // DOMAIN_DECISION_SCHEDULE_STRATEGY_H
