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

#include "common/logs/logging.h"
#include "common/schedule_plugin/common/preallocated_context.h"
#include "common/scheduler_framework/framework/framework_impl.h"

namespace functionsystem::schedule_decision {

namespace {
bool CanReuseScalarCandidates(const messages::ScheduleRequest &request,
                              const std::shared_ptr<schedule_framework::Framework> &framework)
{
    const auto builtin = std::dynamic_pointer_cast<schedule_framework::FrameworkImpl>(framework);
    return builtin != nullptr && builtin->SupportsSemanticAggregation() && IsScalarAggregationEligible(request);
}
}  // namespace

std::shared_ptr<std::deque<ScheduleResult>> AggregatedSchedulePerformer::DoSchedule(
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
    const resource_view::ResourceViewInfo &resourceInfo, const std::shared_ptr<AggregatedItem> &aggregatedItem)
{
    return DoSchedule(context, resource_view::ScheduleResourceView(resourceInfo), aggregatedItem);
}

std::shared_ptr<std::deque<ScheduleResult>> AggregatedSchedulePerformer::DoSchedule(
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
    const resource_view::ScheduleResourceView &resourceView, const std::shared_ptr<AggregatedItem> &aggregatedItem)
{
    auto instanceItems = aggregatedItem->reqQueue;
    auto instanceItem = instanceItems->front();
    // Context maps are per-scan caches, not aggregation identity. Preserve
    // cross-level affinity mode while forcing this aggregate to recompute its
    // candidate results from the pinned view.
    schedule_framework::ClearContext(*instanceItem->scheduleReq->mutable_contexts());
    context->pluginCtx = instanceItem->scheduleReq->mutable_contexts();
    return DoMultiSchedule(context, resourceView, instanceItems);
}

std::shared_ptr<std::deque<ScheduleResult>> AggregatedSchedulePerformer::DoMultiSchedule(
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
    const resource_view::ResourceViewInfo &resourceInfo,
    const std::shared_ptr<std::deque<std::shared_ptr<InstanceItem>>> &items)
{
    return DoMultiSchedule(context, resource_view::ScheduleResourceView(resourceInfo), items);
}

std::shared_ptr<std::deque<ScheduleResult>> AggregatedSchedulePerformer::DoMultiSchedule(
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
    const resource_view::ScheduleResourceView &resourceView,
    const std::shared_ptr<std::deque<std::shared_ptr<InstanceItem>>> &items)
{
    auto schedResults = std::make_shared<std::deque<ScheduleResult>>();
    auto instanceItem = items->front();
    auto schedResult = ScheduleResult{};
    std::unordered_map<std::string, int32_t> _;
    ASSERT_IF_NULL(framework_);
    const bool reuseCandidateCapacity =
        !resourceView.IsSnapshot() || CanReuseScalarCandidates(*instanceItem->scheduleReq, framework_);
    YRLOG_INFO("start AggregatedItem schedule (reqId={}, priority={}, batchSize={}, reuseCandidateCapacity={})",
               instanceItem->GetRequestId(), instanceItem->GetPriority(), items->size(), reuseCandidateCapacity);
    auto results = framework_->SelectFeasible(context, instanceItem->scheduleReq->instance(), resourceView,
                                              items->size());
    if (results.code != static_cast<int32_t>(StatusCode::SUCCESS)) {
        schedResults->emplace_back(ScheduleResult{ "", results.code, results.reason, {}, "", {} });
        return schedResults;
    }

    const bool refreshSelectedUnit = !reuseCandidateCapacity;
    for (uint32_t i = 0; i < items->size(); i++) {
        auto item = (*items)[i];
        auto schedRes = SelectFromResults(
            context, resourceView, item, { results.sortedFeasibleNodes, _, refreshSelectedUnit });
        schedResults->emplace_back(schedRes);
        if (schedRes.code != static_cast<int32_t>(StatusCode::SUCCESS)) {
            break;
        }
    }
    return schedResults;
}

}  // namespace functionsystem::schedule_decision
