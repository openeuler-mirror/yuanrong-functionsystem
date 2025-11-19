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
#include "group_schedule_performer.h"
#include "common/schedule_plugin/common/affinity_utils.h"
namespace functionsystem::schedule_decision {

bool CheckGroupCanBatch(const std::shared_ptr<schedule_decision::GroupItem> &scheduleItem)
{
    if (!scheduleItem->GetRangeOpt().isRange) {
        return false;
    }
    auto &instance = scheduleItem->groupReqs[0]->scheduleReq->instance();
    const auto &affinity = instance.scheduleoption().affinity();
    if (!affinity.has_instance()) {
        return true;
    }
    if (!affinity.instance().has_requiredantiaffinity()) {
        return true;
    }
    if (!schedule_plugin::RequiredAntiAffinityFilter("", affinity.instance().requiredantiaffinity(),
                                                     ToLabelKVs(instance.labels()) + ToLabelKVs(instance.kvlabels()))) {
        return false;
    }
    return true;
}

ScheduleResult GroupSchedulePerformer::Selected(
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
    const resource_view::ResourceViewInfo &resourceInfo, const std::shared_ptr<InstanceItem> &instanceItem,
    schedule_framework::NodeScore &nodeScore)
{
    auto scheReq = instanceItem->scheduleReq;
    auto requestID = scheReq->requestid();
    auto traceID = scheReq->traceid();
    // reuse spec context
    schedule_framework::CopyPluginContext(*scheReq->mutable_contexts(), *context->pluginCtx);
    // availableForRequest should never be 0
    ASSERT_FS(nodeScore.availableForRequest == -1 || nodeScore.availableForRequest > 0);
    auto result = ScheduleResult{ nodeScore.name, 0, "", nodeScore.realIDs, nodeScore.heteroProductName,
                             nodeScore.allocatedVectors };
    // while scheduled to bundle, the id from result is logical bundle id which should be transferred to owner
    // on local: ownerid() == real agent id
    // on domain: ownerid == localid
    result.unitID = result.id;
    result.id = resourceInfo.resourceUnit.fragment().at(result.id).ownerid();

    PreAllocated(scheReq->instance(), context, requestID, traceID, result);
    return result;
}

GroupScheduleResult GroupSchedulePerformer::DoStrictPackSchedule(
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
    const resource_view::ResourceViewInfo &resourceInfo,
    const std::shared_ptr<schedule_decision::GroupItem> &scheduleItem)
{
    // Construct a virtual item that includes all item resources.
    auto promise = std::make_shared<litebus::Promise<ScheduleResult>>();
    ASSERT_FS(scheduleItem->groupReqs.size() != 0);
    auto totalItem = std::make_shared<InstanceItem>(
        std::make_shared<messages::ScheduleRequest>(*scheduleItem->groupReqs[0]->scheduleReq), promise,
        litebus::Future<std::string>());
    totalItem->scheduleReq->set_requestid(totalItem->GetRequestId());
    for (size_t i = 1; i < scheduleItem->groupReqs.size(); i++) {
        auto pre = totalItem->scheduleReq->mutable_instance()->resources();
        *totalItem->scheduleReq->mutable_instance()->mutable_resources() =
            pre + scheduleItem->groupReqs[i]->scheduleReq->instance().resources();
    }
    YRLOG_DEBUG("{} | pack group as one instance to schedule {}", scheduleItem->GetRequestId(),
                resource_view::ToString(totalItem->scheduleReq->instance().resources()));
    ASSERT_IF_NULL(framework_);
    context->pluginCtx = totalItem->scheduleReq->mutable_contexts();
    auto result = DoSelectOne(context, resourceInfo, totalItem);
    GroupScheduleResult groupResult{ result.code, result.reason, {} };
    if (result.code != static_cast<int32_t>(StatusCode::SUCCESS)) {
        return groupResult;
    }
    // strict pack does not to check duplicated
    // while failure by conflict all reserved resource would be rollback
    for (auto instanceItem : scheduleItem->groupReqs) {
        // reuse spec context
        schedule_framework::CopyPluginContext(*instanceItem->scheduleReq->mutable_contexts(), *context->pluginCtx);
        groupResult.results.emplace_back(result);
    }
    return groupResult;
}

GroupScheduleResult GroupSchedulePerformer::DoSchedule(
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
    const resource_view::ResourceViewInfo &resourceInfo,
    const std::shared_ptr<schedule_decision::GroupItem> &scheduleItem)
{
    if (scheduleItem->groupSchedulePolicy == common::GroupPolicy::StrictPack) {
        return DoStrictPackSchedule(context, resourceInfo, scheduleItem);
    }
    return Schedule(context, resourceInfo, scheduleItem);
}

GroupScheduleResult GroupSchedulePerformer::Schedule(
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
    const resource_view::ResourceViewInfo &resourceInfo,
    const std::shared_ptr<schedule_decision::GroupItem> &scheduleItem)
{
    schedule_framework::ScheduleResults results;
    bool batched = CheckGroupCanBatch(scheduleItem);
    if (batched) {
        // check zero size
        ASSERT_IF_NULL(framework_);
        auto instanceSpec = scheduleItem->groupReqs[0]->scheduleReq->instance();
        context->pluginCtx = scheduleItem->groupReqs[0]->scheduleReq->mutable_contexts();
        results = framework_->SelectFeasible(context, instanceSpec, resourceInfo.resourceUnit,
                                             scheduleItem->groupReqs.size());
    }
    std::vector<PreemptResult> preemptResults;
    bool isPreempt = true;
    uint32_t successCount = 0;
    uint32_t min = scheduleItem->GetRangeOpt().isRange ? static_cast<uint32_t>(scheduleItem->GetRangeOpt().min)
                                                       : static_cast<uint32_t>(scheduleItem->groupReqs.size());
    std::shared_ptr<resource_view::ResourceViewInfo> cachedForPreemption = nullptr;
    std::list<ScheduleResult> scheduleResults;
    std::unordered_map<std::string, int32_t> preAllocated;
    for (auto instanceItem : scheduleItem->groupReqs) {
        auto traceID = instanceItem->scheduleReq->traceid();
        auto reqID = instanceItem->scheduleReq->requestid();
        auto result =
            batched ? SelectFromResults(context, resourceInfo, instanceItem, results.sortedFeasibleNodes, preAllocated)
                    : DoSelectOne(context, cachedForPreemption != nullptr ? *cachedForPreemption : resourceInfo,
                                  instanceItem);
        if (result.code == static_cast<int32_t>(StatusCode::SUCCESS)
            || result.code == static_cast<int32_t>(StatusCode::INSTANCE_ALLOCATED)) {
            successCount++;
            scheduleResults.emplace_back(result);
            continue;
        }
        // If the value of successCount is min, a success message is returned. minimum requirement is satisfied
        if (successCount >= min) {
            break;
        }
        // copy is triggered only when preemption is enabled and resources are insufficient for the first time.
        if (preemptInstanceCallback_ != nullptr && cachedForPreemption == nullptr) {
            auto tmp = resource_view::ResourceViewInfo{ resourceInfo.resourceUnit, resourceInfo.schedulerLevel,
                                                        resourceInfo.alreadyScheduled, resourceInfo.allLocalLabels };
            cachedForPreemption = std::make_shared<resource_view::ResourceViewInfo>(std::move(tmp));
        }
        // if unPreemptable failure happened break to return failed.
        if (!IsScheduleResultNeedPreempt(result)) {
            scheduleResults.emplace_back(result);
            break;
        }
        scheduleResults.emplace_back(result);
        YRLOG_INFO("{}|{}|start to check preempt result", traceID, reqID);
        ASSERT_IF_NULL(preemptController_);
        auto preemptRes = preemptController_->PreemptDecision(context, instanceItem->scheduleReq->instance(),
                                                              cachedForPreemption->resourceUnit);
        if (!preemptRes.status.IsOk()) {
            YRLOG_ERROR("{}|{}|preempt status is err, {}", traceID, reqID, preemptRes.status.ToString());
            isPreempt = false;
            break;
        }
        preemptResults.emplace_back(preemptRes);
        for (auto &ins : preemptRes.preemptedInstances) {
            PrePreemptFromResourceView(ins, cachedForPreemption->resourceUnit);
        }
        DoPreAllocated(instanceItem->scheduleReq->instance(), context, preemptRes.unitID, result);
        // preempt success, continue to schedule
        successCount++;
    }
    if (isPreempt && !preemptResults.empty()) {
        preemptInstanceCallback_(preemptResults);
    }
    return DoCollectGroupResult(context, scheduleItem, scheduleResults, successCount);
}

Status GroupSchedulePerformer::RollBack(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                        const std::shared_ptr<schedule_decision::GroupItem> &scheduleItem,
                                        const GroupScheduleResult &groupResult)
{
    std::list<ScheduleResult> results;
    std::vector<std::shared_ptr<messages::ScheduleRequest>> requests;
    for (auto instanceItem : scheduleItem->groupReqs) {
        requests.emplace_back(instanceItem->scheduleReq);
    }
    for (auto &scheRes : groupResult.results) {
        results.emplace_back(scheRes);
    }
    RollBackGroupAllocated(context, results, requests, resourceView_, type_);
    return Status::OK();
}

void GroupSchedulePerformer::PrePreemptFromResourceView(const resources::InstanceInfo &instance,
                                                        resources::ResourceUnit &unit)
{
    auto nodeLabels = unit.mutable_nodelabels();
    DeleteLabel(instance, *nodeLabels);
    auto agentId = instance.unitid();
    auto agentFragmentIter = unit.mutable_fragment()->find(agentId);
    if (agentFragmentIter == unit.mutable_fragment()->end()) {
        YRLOG_WARN("resource view does not have a resource unit with ID {}.", agentId);
        return;
    }
    auto &agentResourceUnit = agentFragmentIter->second;
    auto addend = DeleteInstanceFromAgentView(instance, agentResourceUnit);
    (*unit.mutable_allocatable()) = unit.allocatable() + addend;
    UpdateBucketInfoDelInstance(instance, agentResourceUnit.capacity(), agentResourceUnit.instances_size(), unit);
    UpdateBucketInfoDelInstance(instance, agentResourceUnit.capacity(), agentResourceUnit.instances_size(),
                                agentResourceUnit);
    (void)unit.mutable_instances()->erase(instance.instanceid());
}

GroupScheduleResult GroupSchedulePerformer::DoCollectGroupResult(
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
    const std::shared_ptr<schedule_decision::GroupItem> &scheduleItem, const std::list<ScheduleResult> &results,
    const uint32_t &successCount)
{
    auto rangeOpt = scheduleItem->GetRangeOpt();
    if (!rangeOpt.isRange || (rangeOpt.min >= 0 && successCount <= static_cast<uint32_t>(rangeOpt.min))) {
        return SchedulePerformer::DoCollectGroupResult(results);
    }
    ASSERT_FS(successCount == results.size());
    ASSERT_FS(rangeOpt.max >= 0 && static_cast<uint32_t>(rangeOpt.max) >= successCount);
    ASSERT_FS(rangeOpt.step > 0);
    auto stepCount =
        std::ceil((rangeOpt.max - static_cast<int32_t>(successCount)) / static_cast<double>(rangeOpt.step)) *
        rangeOpt.step;
    auto reserved = rangeOpt.max - static_cast<int32_t>(stepCount);
    if (static_cast<int32_t>(reserved) < rangeOpt.min) {
        reserved = rangeOpt.min;
    }
    YRLOG_WARN("DoCollectGroupResult stepCount {} reserved {}.", stepCount, reserved);
    GroupScheduleResult groupResult;
    groupResult.code = static_cast<int32_t>(StatusCode::SUCCESS);
    int32_t index = 0;
    for (auto result : results) {
        index++;
        // unnecessary scheduling results need to be rolled back.
        if (index > reserved) {
            RollBackAllocated(context, result.unitID, scheduleItem->groupReqs[index]->scheduleReq->instance(), nullptr);
            continue;
        }
        groupResult.results.emplace_back(result);
    }
    return groupResult;
}
}