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

#ifndef TEST_UNIT_MOCKS_MOCK_SCHEDULER_PERFORMER_H
#define TEST_UNIT_MOCKS_MOCK_SCHEDULER_PERFORMER_H

#include <gmock/gmock.h>

#include "common/schedule_decision/performer/instance_schedule_performer.h"
#include "common/schedule_decision/performer/group_schedule_performer.h"
#include "common/schedule_decision/performer/aggregated_schedule_performer.h"

namespace functionsystem::test {
class MockInstanceSchedulePerformer : public schedule_decision::InstanceSchedulePerformer {
public:
    MockInstanceSchedulePerformer() : schedule_decision::InstanceSchedulePerformer(schedule_decision::PRE_ALLOCATION)
    {
    }
    ~MockInstanceSchedulePerformer() override = default;
    MOCK_METHOD(schedule_decision::ScheduleResult, DoSchedule,
                (const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                 const resource_view::ResourceViewInfo &resource,
                 const std::shared_ptr<schedule_decision::InstanceItem> &instanceItem
                 ),
                (override));

    MOCK_METHOD(Status, RollBack,
                (const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                 const std::shared_ptr<schedule_decision::InstanceItem> &instanceItem,
                 const schedule_decision::ScheduleResult &scheduleResuslt),
                 (override));
};

class MockGroupSchedulePerformer : public schedule_decision::GroupSchedulePerformer {
public:
    MockGroupSchedulePerformer() : schedule_decision::GroupSchedulePerformer(schedule_decision::PRE_ALLOCATION)
    {
    }
    ~MockGroupSchedulePerformer() override = default;

    MOCK_METHOD(schedule_decision::GroupScheduleResult, DoSchedule,
                (const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                 const resource_view::ResourceViewInfo &resource,
                 const std::shared_ptr<schedule_decision::GroupItem> &groupItem),
                (override));

    MOCK_METHOD(Status, RollBack,
                (const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                 const std::shared_ptr<schedule_decision::GroupItem> &scheduleItem,
                 const schedule_decision::GroupScheduleResult &groupResult),
                 (override));
};

class MockAggregatedSchedulePerformer : public schedule_decision::AggregatedSchedulePerformer {
public:
    MockAggregatedSchedulePerformer() : AggregatedSchedulePerformer(schedule_decision::PRE_ALLOCATION)
    {
    }
    ~MockAggregatedSchedulePerformer() override = default;

    MOCK_METHOD(std::shared_ptr<std::deque<schedule_decision::ScheduleResult>>, DoSchedule,
        (const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
        const resource_view::ResourceViewInfo &resourceInfo,
        const std::shared_ptr<schedule_decision::AggregatedItem> &aggregatedItem),
        (override));

};

}  // namespace functionsystem::test

#endif  // TEST_UNIT_MOCKS_MOCK_SCHEDULER_PERFORMER_H
