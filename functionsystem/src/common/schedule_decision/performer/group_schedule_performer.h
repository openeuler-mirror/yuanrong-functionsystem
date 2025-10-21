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

#ifndef COMMON_SCHEDULE_DECISION_GROUP_SCHEDULE_PERFORMER_H
#define COMMON_SCHEDULE_DECISION_GROUP_SCHEDULE_PERFORMER_H

#include <litebus.hpp>
#include "async/future.hpp"

#include "schedule_performer.h"

namespace functionsystem::schedule_decision {

class GroupSchedulePerformer
    : public SchedulePerformer {
public:
    explicit GroupSchedulePerformer(const AllocateType &type) : SchedulePerformer(type) {};
    ~GroupSchedulePerformer() override = default;

    virtual schedule_decision::GroupScheduleResult DoSchedule(
        const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
        const resource_view::ResourceViewInfo &resourceInfo,
        const std::shared_ptr<schedule_decision::GroupItem> &scheduleItem);

    virtual Status RollBack(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
        const std::shared_ptr<schedule_decision::GroupItem> &scheduleItem, const GroupScheduleResult &groupResult);

    void PrePreemptFromResourceView(const resources::InstanceInfo &instance, resources::ResourceUnit &unit);

    GroupScheduleResult DoCollectGroupResult(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                             const std::shared_ptr<schedule_decision::GroupItem> &scheduleItem,
                                             const std::list<ScheduleResult> &results, const uint32_t &successCount);

private:
    schedule_decision::GroupScheduleResult Schedule(
        const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
        const resource_view::ResourceViewInfo &resourceInfo,
        const std::shared_ptr<schedule_decision::GroupItem> &scheduleItem);
    schedule_decision::ScheduleResult Selected(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                               const resource_view::ResourceViewInfo &resourceInfo,
                                               const std::shared_ptr<InstanceItem> &instanceItem,
                                               schedule_framework::NodeScore &nodeScore);
    schedule_decision::GroupScheduleResult DoStrictPackSchedule(
        const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
        const resource_view::ResourceViewInfo &resourceInfo,
        const std::shared_ptr<schedule_decision::GroupItem> &scheduleItem);
};

}
#endif  // COMMON_SCHEDULE_DECISION_GROUP_SCHEDULE_PERFORMER_H

