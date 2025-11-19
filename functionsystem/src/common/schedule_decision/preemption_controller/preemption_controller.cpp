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
#include "preemption_controller.h"

#include <sstream>

#include "common/logs/logging.h"
#include "common/scheduler_framework/utils/label_affinity_utils.h"

namespace functionsystem::schedule_decision {
using PreemptableUnitComparator = std::function<bool(const PreemptableUnit &, const PreemptableUnit &)>;
using InstanceInfoComparator =
    std::function<bool(const resource_view::InstanceInfo &, const resource_view::InstanceInfo &)>;

bool ComparePreemptableUnit(const PreemptableUnit &l, const PreemptableUnit &r)
{
    if (l.score != r.score) {
        return l.score > r.score;
    }
    // fewer preempted instances are ranked first.
    if (l.preemptedInstances.size() != r.preemptedInstances.size()) {
        return l.preemptedInstances.size() < r.preemptedInstances.size();
    }
    if (l.preemptedResources != r.preemptedResources) {
        // Small resources are ranked first.
        return l.preemptedResources <= r.preemptedResources;
    }
    return l.unitID < r.unitID;
}

int32_t GetPreemptionPriority(const resource_view::InstanceInfo &instance, const resource_view::ResourceUnit &)
{
    return instance.scheduleoption().priority();
}

resources::Resources GetAllocatedResource(const std::string &unitID, const resource_view::Resources &resources,
                                          const std::shared_ptr<schedule_framework::PreAllocatedContext> &ctx)
{
    // calculate new available
    if (auto iter(ctx->allocated.find(unitID)); iter != ctx->allocated.end()) {
        return resources - iter->second.resource;
    }
    return resources;
}

void LogPreemptResult(const std::set<PreemptableUnit, PreemptableUnitComparator> &results,
                      const resource_view::InstanceInfo &instance)
{
    std::stringstream ss;
    auto out =
        fmt::format("{}|preempt decision for instance({}): candidate [", instance.requestid(), instance.instanceid());
    ss << out;
    auto count = 0;
    const int32_t maxCount = 5;
    for (auto &result : results) {
        if (count > maxCount) {
            break;
        }
        ss << fmt::format("\nunitID({}) score({}) preemptedInstances({}), instance(", result.unitID, result.score,
                          result.preemptedInstances.size());
        for (auto &preemptedInstance : result.preemptedInstances) {
            ss << fmt::format("[{}, {}]", preemptedInstance.instanceid(),
                              resource_view::ToString(preemptedInstance.resources()));
        }
        ss << ") ";
        count++;
    }
    ss << "]";
    YRLOG_INFO("{}", ss.str());
}

PreemptResult PreemptionController::PreemptDecision(const std::shared_ptr<schedule_framework::ScheduleContext> &ctx,
                                                    const resource_view::InstanceInfo &instance,
                                                    const resource_view::ResourceUnit &resourceUnit)
{
    PreemptResult result{Status::OK(), "", "", {}};
    std::set<PreemptableUnit, PreemptableUnitComparator> candidatePreemptableUnits(ComparePreemptableUnit);
    const auto preContext = std::dynamic_pointer_cast<schedule_framework::PreAllocatedContext>(ctx);
    if (preContext == nullptr) {
        result.status = Status(StatusCode::PARAMETER_ERROR, "invalid context for PreemptionController");
        return result;
    }
    auto infeasibleCtx = InFeasibleContext();
    for (const auto &[unitID, frag] : resourceUnit.fragment()) {
        if (!IsUnitMeetRequired(preContext, instance, frag)) {
            infeasibleCtx.InsertInfeasibleUnit(unitID);
            continue;
        }
        int64_t score = 0;
        if (!IsResourceAffinityMeetRequired(preContext, instance, frag, score)) {
            infeasibleCtx.InsertInfeasibleUnit(unitID);
            continue;
        }
        if (IsCrossedTenant(instance, frag)) {
            infeasibleCtx.InsertCrossTenantUnit(unitID);
            continue;
        }
        auto preemptedUnit = ChoseInstanceToPreempted(preContext, instance, frag, score);
        if (preemptedUnit.preemptedInstances.empty()) {
            infeasibleCtx.InsertNoPreemptableInstanceUnits(unitID);
            continue;
        }
        candidatePreemptableUnits.insert(std::move(preemptedUnit));
    }
    infeasibleCtx.Print(instance);
    if (candidatePreemptableUnits.empty()) {
        YRLOG_WARN("{}|no available instance to be preempted for ({})", instance.requestid(), instance.instanceid());
        result.status = Status(StatusCode::DOMAIN_SCHEDULER_NO_PREEMPTABLE_INSTANCE);
        return result;
    }
    LogPreemptResult(candidatePreemptableUnits, instance);
    auto preemptableUnit = *candidatePreemptableUnits.begin();
    result.unitID = preemptableUnit.unitID;
    result.ownerID = preemptableUnit.ownerID;
    result.preemptedInstances = std::move(preemptableUnit.preemptedInstances);
    candidatePreemptableUnits.clear();
    return result;
}

bool PreemptionController::IsUnitMeetRequired(const std::shared_ptr<schedule_framework::PreAllocatedContext> &ctx,
                                              const resource_view::InstanceInfo &instance,
                                              const resource_view::ResourceUnit &frag)
{
    auto cap = GetAllocatedResource(frag.id(), frag.capacity(), ctx);
    if (instance.resources() <= cap) {
        return true;
    }
    return false;
}

bool PreemptionController::IsCrossedTenant(const resource_view::InstanceInfo &instance,
                                           const resource_view::ResourceUnit &frag)
{
    if (frag.nodelabels().count(TENANT_ID) != 0 &&
        frag.nodelabels().at(TENANT_ID).items().count(instance.tenantid()) == 0) {
        return true;
    }
    return false;
}

bool PreemptionController::IsResourceAffinityMeetRequired(
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &ctx, const resource_view::InstanceInfo &instance,
    const resource_view::ResourceUnit &frag, int64_t &score)
{
    auto unitLabels = frag.nodelabels() + ctx->allocatedLabels[frag.id()];
    // do not meet resource required affinity
    if (!IsResourceRequiredAffinityPassed(frag.id(), instance, frag.nodelabels())) {
        return false;
    }
    // do not priority resource affinity
    score = CalculateResourceAffinityScore(frag.id(), instance, frag.nodelabels());
    if (score == -1) {
        return false;
    }
    // anti affinity instance is scheduled on the frag
    const auto &affinity = instance.scheduleoption().affinity();
    if (affinity.has_instance() && affinity.instance().has_requiredantiaffinity()) {
        return RequiredAntiFilter(frag.id(), affinity.instance().requiredantiaffinity(), unitLabels);
    }
    return true;
}

bool PreemptionController::IsInstancePreemptable(const resource_view::InstanceInfo &srcInstance,
                                                 const resource_view::InstanceInfo &dstInstance,
                                                 const resource_view::ResourceUnit &frag)
{
    if (!dstInstance.scheduleoption().preemptedallowed()) {
        return false;
    }
    if (GetPreemptionPriority(srcInstance, frag) <= GetPreemptionPriority(dstInstance, frag)) {
        return false;
    }
    // Non-preemption of instance tags with strong affinity
    // anti affinity was filtered by IsResourceAffinityMeetRequired
    const auto &affinity = srcInstance.scheduleoption().affinity();
    if (affinity.has_instance() &&affinity.instance().has_requiredaffinity()) {
        return !RequiredFilter(dstInstance.instanceid(), affinity.instance().requiredaffinity(),
                              ToLabelKVs(dstInstance.labels()) + ToLabelKVs(dstInstance.kvlabels()));
    }
    return true;
}

bool InstanceAffinityComparator(const resource_view::InstanceInfo &instance, const resource_view::ResourceUnit &frag,
                                const resource_view::InstanceInfo &l, const resource_view::InstanceInfo &r)
{
    if (GetPreemptionPriority(l, frag) != GetPreemptionPriority(r, frag)) {
        // The resource with a lower priority is ranked first. Otherwise, the resource with the same weight is ranked
        // first in descending order of weak affinity and scoring weight.
        return GetPreemptionPriority(l, frag) < GetPreemptionPriority(r, frag);
    }
    // if affinity is required on topology scope, the topology index is required
    auto lAffinity =
        CalculateInstanceAffinityScore(l.instanceid(), instance, ToLabelKVs(l.labels()) + ToLabelKVs(l.kvlabels()));
    auto rAffinity =
        CalculateInstanceAffinityScore(r.instanceid(), instance, ToLabelKVs(r.labels()) + ToLabelKVs(r.kvlabels()));
    if (lAffinity != rAffinity) {
        return lAffinity < rAffinity;
    }
    // The resource with a higher occupied value is ranked first.
    if (l.resources() != r.resources()) {
        return l.resources() > r.resources();
    }
    return l.instanceid() > r.instanceid();
}

PreemptableUnit PreemptionController::ChoseInstanceToPreempted(
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &ctx, const resource_view::InstanceInfo &instance,
    const resource_view::ResourceUnit &frag, int64_t &score)
{
    PreemptableUnit preemptableUnit;
    std::vector<resource_view::InstanceInfo> result;
    auto instanceComparator = [&instance, &frag](const resource_view::InstanceInfo &l,
                                                 const resource_view::InstanceInfo &r) -> bool {
        return InstanceAffinityComparator(instance, frag, l, r);
    };
    std::set<resource_view::InstanceInfo, InstanceInfoComparator> candidatePreemptedInstances(instanceComparator);
    for (const auto &[_, instanceInfo] : frag.instances()) {
        [[maybe_unused]] const auto& unused = _;
        if (IsInstancePreemptable(instance, instanceInfo, frag)) {
            candidatePreemptedInstances.insert(instanceInfo);
        }
    }
    if (candidatePreemptedInstances.empty()) {
        return preemptableUnit;
    }
    auto avail = GetAllocatedResource(frag.id(), frag.allocatable(), ctx);
    auto unitLabels = frag.nodelabels() + ctx->allocatedLabels[frag.id()];
    resource_view::Resources preemptedResources = BuildResources(0, 0);
    for (const auto &preemptedInstance : candidatePreemptedInstances) {
        avail = avail + preemptedInstance.resources();
        unitLabels = unitLabels - ToLabelKVs(preemptedInstance.labels()) - ToLabelKVs(preemptedInstance.kvlabels());
        result.push_back(preemptedInstance);
        preemptedResources = preemptedResources + preemptedInstance.resources();
        if (instance.resources() <= avail) {
            break;
        }
    }
    if (instance.resources() > avail) {
        YRLOG_WARN("{}|all preemptable instance can not meet resource requirement ({})", instance.requestid(),
                   instance.instanceid());
        return preemptableUnit;
    }

    score += CalculateInstanceAffinityScore(frag.id(), instance, unitLabels);
    preemptableUnit.unitID = frag.id();
    preemptableUnit.ownerID = frag.ownerid();
    preemptableUnit.score = score;
    preemptableUnit.preemptedInstances = std::move(result);
    preemptableUnit.preemptedResources = std::move(preemptedResources);
    return preemptableUnit;
}

}  // namespace functionsystem::domain_scheduler