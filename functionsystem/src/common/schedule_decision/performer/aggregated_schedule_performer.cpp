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

#include "aggregated_schedule_performer.h"

#include <memory>

namespace functionsystem::schedule_decision {

std::shared_ptr<std::deque<ScheduleResult>> AggregatedSchedulePerformer::DoSchedule(
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
    const resource_view::ResourceViewInfo &resourceInfo, const std::shared_ptr<AggregatedItem> &aggregatedItem)
{
    auto instanceItems = aggregatedItem->reqQueue;
    auto instanceItem = instanceItems->front();
    context->pluginCtx = instanceItem->scheduleReq->mutable_contexts();
    return DoMultiSchedule(context, resourceInfo, instanceItems);
}

std::shared_ptr<std::deque<ScheduleResult>> AggregatedSchedulePerformer::DoMultiSchedule(
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
    const resource_view::ResourceViewInfo &resourceInfo,
    const std::shared_ptr<std::deque<std::shared_ptr<InstanceItem>>> &items)
{
    auto schedResults = std::make_shared<std::deque<ScheduleResult>>();
    auto instanceItem = items->front();
    auto schedResult = ScheduleResult{};
    std::unordered_map<std::string, int32_t> _;
    ASSERT_IF_NULL(framework_);
    auto results = framework_->SelectFeasible(context, instanceItem->scheduleReq->instance(),
	                                          resourceInfo.resourceUnit, items->size());
    if (results.code != static_cast<int32_t>(StatusCode::SUCCESS)) {
        schedResults->emplace_back(ScheduleResult{ "", results.code, results.reason, {}, "", {} });
        return schedResults;
    }

    for (uint32_t i = 0; i < items->size(); i++) {
        auto item = (*items)[i];
        auto schedRes = SelectFromResults(context, resourceInfo, item, results.sortedFeasibleNodes, _);
        schedResults->emplace_back(schedRes);
        if (schedRes.code != static_cast<int32_t>(StatusCode::SUCCESS)) {
            break;
        }
    }
    return schedResults;
}

}  // namespace functionsystem::schedule_decision
