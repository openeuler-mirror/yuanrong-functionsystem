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

#ifndef COMMON_SCHEDULE_DECISION_INSTANCE_SCHEDULE_PERFORMER_H
#define COMMON_SCHEDULE_DECISION_INSTANCE_SCHEDULE_PERFORMER_H

#include <litebus.hpp>
#include "async/future.hpp"

#include "schedule_performer.h"

namespace functionsystem::schedule_decision {

class InstanceSchedulePerformer
    : public SchedulePerformer {
public:
    explicit InstanceSchedulePerformer(const AllocateType &type) : SchedulePerformer(type){};
    ~InstanceSchedulePerformer() override  = default;

    virtual schedule_decision::ScheduleResult DoSchedule(
        const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
        const resource_view::ResourceViewInfo &resourceInfo,
        const std::shared_ptr<schedule_decision::InstanceItem> &scheduleItem);

    virtual Status RollBack(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                            const std::shared_ptr<schedule_decision::InstanceItem> &instanceItem,
                            const ScheduleResult &scheduleResuslt);
};

}
#endif  // COMMON_SCHEDULE_DECISION_INSTANCE_SCHEDULE_PERFORMER_H

