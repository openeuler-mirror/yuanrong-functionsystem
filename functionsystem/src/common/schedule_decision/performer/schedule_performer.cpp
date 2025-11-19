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

#include "schedule_performer.h"

namespace functionsystem::schedule_decision {

void UpdateInstanceWithVectorAllocations(const ScheduleResult &schedResult, resource_view::InstanceInfo &instance)
{
    auto *resources = instance.mutable_resources()->mutable_resources();
    // Set heterogeneous allocation info (will be merged into vectorAllocations processing below)
    for (const auto &allocated : schedResult.allocatedVectors) {
        auto *vectors = (*resources)[allocated.first].mutable_vectors();
        (*resources)[allocated.first].set_name(allocated.first);
        (*resources)[allocated.first].set_type(resource_view::ValueType::Value_Type_VECTORS);
        for (const auto &value : allocated.second.values()) {
            (*vectors->mutable_values())[value.first] = value.second;
        }
    }
    // Set allocation info for vector-type resource (generic implementation)
    for (const auto &vectorAllocation : schedResult.vectorAllocations) {
        auto *vectors = (*resources)[vectorAllocation.type].mutable_vectors();
        (*resources)[vectorAllocation.type].set_name(vectorAllocation.type);
        (*resources)[vectorAllocation.type].set_type(resource_view::ValueType::Value_Type_VECTORS);
        for (const auto &value : vectorAllocation.allocationValues.values()) {
            (*vectors->mutable_values())[value.first] = value.second;
        }
    }
}

void SchedulePerformer::Allocate(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                 const std::string &selected, const resource_view::InstanceInfo &ins,
                                 ScheduleResult &schedResult)
{
    if (type_ == AllocateType::ALLOCATION) {
        schedResult.allocatedPromise = std::make_shared<litebus::Promise<Status>>();
        ASSERT_IF_NULL(resourceView_);
        (void)resourceView_->AddInstances(
            { { ins.instanceid(), resource_view::InstanceAllocatedInfo{ ins, schedResult.allocatedPromise } } });
    }
    context->allocated[selected].resource = context->allocated[selected].resource.resources().size() == 0
                                                ? ins.resources()
                                                : context->allocated[selected].resource + ins.resources();

    context->allocatedLabels[selected] =
        context->allocatedLabels[selected] + ToLabelKVs(ins.labels()) + ToLabelKVs(ins.kvlabels());

    // local and domain need to mark agent is selected to avoid select same agent
    // while two instance scheduling in a short time
    context->preAllocatedSelectedFunctionAgentMap[ins.instanceid()] = selected;
    context->preAllocatedSelectedFunctionAgentSet.insert(selected);
}

void SchedulePerformer::PreAllocated(const resource_view::InstanceInfo &ins,
                                     const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                     const std::string &requestID, const std::string &traceID,
                                     ScheduleResult &schedResult)
{
    if (schedResult.code != static_cast<int32_t>(StatusCode::SUCCESS)) {
        return;
    }
    auto selected = schedResult.unitID;
    YRLOG_INFO("{}|{}|scheduler {} is selected.", traceID, requestID, selected);
    DoPreAllocated(ins, context, selected, schedResult);
}

void SchedulePerformer::DoPreAllocated(const resource_view::InstanceInfo &ins,
                                       const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                       const std::string &selected, ScheduleResult &schedResult)
{
    auto backupIns = ins;
    const auto &required = ins.resources().resources();
    ASSERT_IF_NULL(resourceView_);
    for (auto &req : required) {
        auto resourceName = req.first;
        if (resource_view::IsHeterogeneousResource(resourceName) || resource_view::IsDiskResource(resourceName)) {
            backupIns.mutable_resources()->mutable_resources()->erase(req.first);
        }
    }
    UpdateInstanceWithVectorAllocations(schedResult, backupIns);
    (*backupIns.mutable_schedulerchain()->Add()) = selected;
    backupIns.set_unitid(selected);
    Allocate(context, selected, backupIns, schedResult);
}

std::string SchedulePerformer::GetAlreadyScheduledResult(const std::string &requestID,
                                                         const resource_view::ResourceViewInfo &resourceInfo) const
{
    std::string alreadyScheduledResult = "";
    if (resourceInfo.alreadyScheduled.find(requestID) == resourceInfo.alreadyScheduled.end()) {
        return alreadyScheduledResult;
    }

    alreadyScheduledResult = resourceInfo.alreadyScheduled.at(requestID);
    if (type_ == AllocateType::ALLOCATION) {
        return alreadyScheduledResult;
    }
    auto &resourceUnit = resourceInfo.resourceUnit;
    if (resourceUnit.fragment().find(alreadyScheduledResult) == resourceUnit.fragment().end()) {
        YRLOG_ERROR("resource view does not have a agent unit with ID {}.", alreadyScheduledResult);
        return "";
    }

    alreadyScheduledResult = resourceUnit.fragment().at(alreadyScheduledResult).ownerid();
    return alreadyScheduledResult;
}

void SchedulePerformer::RollBackAllocated(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                          const std::string &selected, const resource_view::InstanceInfo &ins,
                                          const std::shared_ptr<resource_view::ResourceView> &resourceView)
{
    if (context->allocated.find(selected) != context->allocated.end()) {
        context->allocated[selected].resource = context->allocated[selected].resource - ins.resources();
    }
    if (context->allocatedLabels.find(selected) != context->allocatedLabels.end()) {
        context->allocatedLabels[selected] =
            context->allocatedLabels[selected] - ToLabelKVs(ins.labels()) - ToLabelKVs(ins.kvlabels());
    }
    context->preAllocatedSelectedFunctionAgentSet.erase(selected); // need to free pod while rollback
    // rollback the preAllocated instance
    if (type_ == AllocateType::ALLOCATION) {
        ASSERT_IF_NULL(resourceView);
        resourceView->DeleteInstances({ ins.instanceid() }, true);
    }
}

void SchedulePerformer::RollBackGroupAllocated(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                               const std::list<ScheduleResult> &results,
                                               const std::vector<std::shared_ptr<messages::ScheduleRequest>> &requests,
                                               const std::shared_ptr<resource_view::ResourceView> &resourceView,
                                               AllocateType type)
{
    auto index = 0;
    for (auto result : results) {
        // rollback successful schedule result
        if (result.code != static_cast<int32_t>(StatusCode::SUCCESS)) {
            index++;
            continue;
        }
        auto request = requests[index];
        auto selectedId = result.id;
        if (type == AllocateType::PRE_ALLOCATION) {
            selectedId = context->preAllocatedSelectedFunctionAgentMap[request->instance().instanceid()];
        }
        YRLOG_INFO("{}|{}|rollback instance({}) of group({}) schedule result, which selected({})", request->traceid(),
                   request->requestid(), request->instance().instanceid(), request->instance().groupid(), selectedId);
        RollBackAllocated(context, selectedId, request->instance(), resourceView);
        index++;
    }
}

bool SchedulePerformer::IsScheduled(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                    const resource_view::ResourceViewInfo &resourceInfo,
                                    const std::shared_ptr<InstanceItem> &instanceItem, ScheduleResult &result,
                                    std::unordered_map<std::string, int32_t> &preAllocatedSelected)
{
    auto scheReq = instanceItem->scheduleReq;
    auto requestID = scheReq->requestid();
    auto traceID = scheReq->traceid();
    const auto &groupContext = scheReq->contexts();
    if (type_ == AllocateType::PRE_ALLOCATION
        && groupContext.find(GROUP_SCHEDULE_CONTEXT) != groupContext.end()
        && !groupContext.at(GROUP_SCHEDULE_CONTEXT).groupschedctx().reserved().empty()) {
        auto &unitID = groupContext.at(GROUP_SCHEDULE_CONTEXT).groupschedctx().reserved();
        if (resourceInfo.resourceUnit.fragment().find(unitID) == resourceInfo.resourceUnit.fragment().end()) {
            return false;
        }
        result.code = static_cast<int32_t>(StatusCode::SUCCESS);
        result.id = unitID;
        result.unitID = unitID;
        if (preAllocatedSelected.find(unitID) == preAllocatedSelected.end()) {
            preAllocatedSelected[unitID] = 0;
        }

        YRLOG_WARN("{}|request {}. request is already reserved to {}", traceID, requestID, result.id);
        auto alreadyScheduledResult = GetAlreadyScheduledResult(requestID, resourceInfo);
        if (alreadyScheduledResult.empty()) {
            preAllocatedSelected[unitID] += 1;
            PreAllocated(scheReq->instance(), context, requestID, traceID, result);
        }
        context->preAllocatedSelectedFunctionAgentMap[scheReq->instance().instanceid()] = result.id;
        context->preAllocatedSelectedFunctionAgentSet.insert(result.id);
        result.unitID = result.id;
        result.id = resourceInfo.resourceUnit.fragment().at(result.id).ownerid();
        return true;
    }
    auto alreadyScheduledResult = GetAlreadyScheduledResult(requestID, resourceInfo);
    if (!alreadyScheduledResult.empty()) {
        YRLOG_WARN("{}|request {}. request is already scheduled to {}", traceID, requestID, alreadyScheduledResult);
        result = ScheduleResult{ alreadyScheduledResult, static_cast<int32_t>(StatusCode::INSTANCE_ALLOCATED),
                                 "request is already scheduled to " + alreadyScheduledResult, {}, "", {}};
        return true;
    }
    return false;
}

ScheduleResult SchedulePerformer::DoSelectOne(const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
                                              const resource_view::ResourceViewInfo &resourceInfo,
                                              const std::shared_ptr<InstanceItem> &instanceItem)
{
    context->pluginCtx = instanceItem->scheduleReq->mutable_contexts();
    auto result = ScheduleResult{};
    std::unordered_map<std::string, int32_t> _;
    if (IsScheduled(context, resourceInfo, instanceItem, result, _)) {
        return result;
    }
    ASSERT_IF_NULL(framework_);
    auto results = framework_->SelectFeasible(context, instanceItem->scheduleReq->instance(),
                                              resourceInfo.resourceUnit, 1);
    if (results.code != static_cast<int32_t>(StatusCode::SUCCESS)) {
        return ScheduleResult{ "", results.code, results.reason, {}, "", {}, {} };
    }
    return SelectFromResults(context, resourceInfo, instanceItem, results.sortedFeasibleNodes, _);
}

bool SchedulePerformer::IsScheduleResultNeedPreempt(const ScheduleResult &result)
{
    return preemptInstanceCallback_ != nullptr &&
           (result.code == static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH) ||
            result.code == static_cast<int32_t>(StatusCode::AFFINITY_SCHEDULE_FAILED));
}

GroupScheduleResult SchedulePerformer::DoCollectGroupResult(const std::list<ScheduleResult> &results)
{
    GroupScheduleResult groupResult;
    groupResult.code = static_cast<int32_t>(StatusCode::SUCCESS);
    auto index = 0;
    for (auto result : results) {
        groupResult.results.emplace_back(result);
        // if any instance fails to be scheduled, error codes need to be set in groups.
        if (result.code != static_cast<int32_t>(StatusCode::SUCCESS) &&
            result.code != static_cast<int32_t>(StatusCode::INSTANCE_ALLOCATED)) {
            groupResult.code = result.code;
            groupResult.reason = "\n" + result.reason + "";
        }
        index++;
    }
    return groupResult;
}

ScheduleResult SchedulePerformer::SelectFromResults(
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &context,
    const resource_view::ResourceViewInfo &resourceInfo, const std::shared_ptr<InstanceItem> &instanceItem,
    std::priority_queue<schedule_framework::NodeScore> &candidateNode,
    std::unordered_map<std::string, int32_t> &preAllocatedSelected)
{
    auto scheReq = instanceItem->scheduleReq;
    auto requestID = scheReq->requestid();
    auto traceID = scheReq->traceid();
    auto result = ScheduleResult{};
    if (IsScheduled(context, resourceInfo, instanceItem, result, preAllocatedSelected)) {
        return result;
    }
    // reuse spec context
    schedule_framework::CopyPluginContext(*scheReq->mutable_contexts(), *context->pluginCtx);
    while (!candidateNode.empty()) {
        auto nodeScore = candidateNode.top();
        // availableForRequest should never be 0
        ASSERT_FS(nodeScore.availableForRequest == -1 || nodeScore.availableForRequest > 0);
        if (nodeScore.availableForRequest == -1) {
            return ScheduleResult{ nodeScore.name, 0, "", nodeScore.realIDs, nodeScore.heteroProductName,
                                   nodeScore.allocatedVectors, nodeScore.vectorAllocations };
        }
        // preAllocate is used for range scheduling. After range scheduling fails, some requests that are successfully
        // reserved are not rolled back. Only the requests that fail and after failed request are rolled
        // back.
        if (preAllocatedSelected.find(nodeScore.name) != preAllocatedSelected.end()) {
            nodeScore.availableForRequest -= preAllocatedSelected[nodeScore.name];
            preAllocatedSelected.erase(nodeScore.name);
            if (nodeScore.availableForRequest <= 0) {
                candidateNode.pop();
                continue;
            }
        }
        // top on priority_queue is immutable, for the deducted available quantity, use the first dequeue modification
        // and then enqueue.
        candidateNode.pop();
        nodeScore.availableForRequest--;
        result = ScheduleResult{ nodeScore.name, 0, "", nodeScore.realIDs, nodeScore.heteroProductName,
                                 nodeScore.allocatedVectors, nodeScore.vectorAllocations };
        if (nodeScore.availableForRequest > 0) {
            candidateNode.emplace(nodeScore);
        }
        // while scheduled to bundle, the id from result is logical bundle id which should be transferred to owner
        // on local: ownerid() == real agent id
        // on domain: ownerid == localid
        result.unitID = result.id;
        result.id = resourceInfo.resourceUnit.fragment().at(result.id).ownerid();

        PreAllocated(scheReq->instance(), context, requestID, traceID, result);
        return result;
    }
    return ScheduleResult{ "",
                           static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH),
                           "no available resource that meets the request requirements",
                           {},
                           "",
                           {},
                           {}};
}
}