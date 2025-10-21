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

#include "instance_schedule_performer.h"

namespace functionsystem::schedule_decision {

ScheduleResult InstanceSchedulePerformer::DoSchedule(
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
    const resource_view::ResourceViewInfo &resourceInfo,
    const std::shared_ptr<schedule_decision::InstanceItem> &instanceItem)
{
    auto result = DoSelectOne(context, resourceInfo, instanceItem);
    if (IsScheduleResultNeedPreempt(result)) {
        YRLOG_INFO("{}|{}|start to check preempt result", instanceItem->scheduleReq->traceid(),
                   instanceItem->scheduleReq->requestid());
        ASSERT_IF_NULL(preemptController_);
        auto preemptRes = preemptController_->PreemptDecision(context, instanceItem->scheduleReq->instance(),
                                                              resourceInfo.resourceUnit);
        if (preemptRes.status.IsOk()) {
            YRLOG_INFO("{}|{}|start to trigger preempt instance", instanceItem->scheduleReq->traceid(),
                       instanceItem->scheduleReq->requestid());

            preemptInstanceCallback_({ std::move(preemptRes) });
        } else {
            YRLOG_ERROR("{}|{}|failed to preempt instance, err is {}", instanceItem->scheduleReq->traceid(),
                        instanceItem->scheduleReq->requestid(), preemptRes.status.ToString());
        }
    }
    return result;
}

Status InstanceSchedulePerformer::RollBack(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                           const std::shared_ptr<InstanceItem> &instanceItem,
                                           const ScheduleResult &scheduleResuslt)
{
    if (scheduleResuslt.code != static_cast<int32_t>(StatusCode::SUCCESS)) {
        return Status::OK();
    }
    auto selected = scheduleResuslt.unitID;
    RollBackAllocated(context, selected, instanceItem->scheduleReq->instance(), resourceView_);
    return Status::OK();
}

}
