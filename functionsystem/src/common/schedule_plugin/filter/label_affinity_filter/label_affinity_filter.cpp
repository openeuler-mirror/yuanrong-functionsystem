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

#include "label_affinity_filter.h"

#include "common/logs/logging.h"
#include "common/resource_view/resource_tool.h"
#include "common/schedule_plugin/common/affinity_utils.h"
#include "common/schedule_plugin/common/constants.h"
#include "common/schedule_plugin/common/plugin_register.h"

namespace functionsystem::schedule_plugin::filter {

std::string LabelAffinityFilter::GetPluginName()
{
    if (isRelaxed_ && isRootDomainLevel_) {
        return RELAXED_ROOT_LABEL_AFFINITY_FILTER_NAME;
    }
    if (!isRelaxed_ && isRootDomainLevel_) {
        return STRICT_ROOT_LABEL_AFFINITY_FILTER_NAME;
    }
    if (isRelaxed_ && !isRootDomainLevel_) {
        return RELAXED_NON_ROOT_LABEL_AFFINITY_FILTER_NAME;
    }
    if (!isRelaxed_ && !isRootDomainLevel_) {
        return STRICT_NON_ROOT_LABEL_AFFINITY_FILTER_NAME;
    }
    return LABEL_AFFINITY_FILTER_NAME;
}

std::string GetRequiredAffinityString(const resource_view::InstanceInfo &instance)
{
    std::ostringstream out;
    if (instance.scheduleoption().affinity().has_resource()
        && (instance.scheduleoption().affinity().resource().has_requiredaffinity()
            || instance.scheduleoption().affinity().resource().has_requiredantiaffinity())) {
        out << "resource { aff { "
            << instance.scheduleoption().affinity().resource().requiredaffinity().ShortDebugString() << " } antiAff {"
            << instance.scheduleoption().affinity().resource().requiredantiaffinity().ShortDebugString() << " } }";
    }
    if (instance.scheduleoption().affinity().has_instance()
        && (instance.scheduleoption().affinity().instance().has_requiredaffinity()
            || instance.scheduleoption().affinity().instance().has_requiredantiaffinity())) {
        out << "instance { aff { "
            << instance.scheduleoption().affinity().instance().requiredaffinity().ShortDebugString() << " } antiAff {"
            << instance.scheduleoption().affinity().instance().requiredantiaffinity().ShortDebugString() << " } }";
    }
    return out.str();
}

bool CheckAgentAvailable(const resource_view::InstanceInfo &instance, messages::AffinityContext &affinityCtx,
                         const resource_view::ResourceUnit &resourceUnit,
                         const std::shared_ptr<schedule_framework::PreAllocatedContext> &preContext)
{
    auto unitId = resourceUnit.id();
    if (!preContext->CheckNodeFeasible(resourceUnit.ownerid())) {
        YRLOG_DEBUG("{}|instance({}) filtered agent({}) because the node({}) is unavailable",
                    instance.requestid(), instance.instanceid(), unitId, resourceUnit.ownerid());
        return false;
    }
    if (auto it = affinityCtx.scheduledresult().find(unitId);
            it != affinityCtx.scheduledresult().end() && it->second == StatusCode::AFFINITY_SCHEDULE_FAILED) {
        YRLOG_DEBUG("{}|instance({}) resource affinity filter agent {} already not feasible by underlayer",
                    instance.requestid(), instance.instanceid(), unitId);
        return false;
    }
    return true;
}

bool AffinityScorerMeetOptimal(const std::string &unitID, const affinity::Selector &selector,
                               const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels,
                               bool anti)
{
    if (selector.condition().subconditions().empty()) {
        return true;
    }
    int64_t score = 0;
    if (anti) {
        score = AntiAffinityScorer(unitID, selector, labels);
    } else {
        score = AffinityScorer(unitID, selector, labels);
    }

    return score == selector.condition().subconditions(0).weight();
}

bool IsInstanceAffinityScoreOptimal(const std::string &unitID, const resource_view::InstanceInfo &instance,
                                    const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    const auto &affinity = instance.scheduleoption().affinity();
    if (!affinity.has_instance()) {
        return true;
    }

    if (affinity.instance().has_preferredaffinity() &&
        !AffinityScorerMeetOptimal(unitID, affinity.instance().preferredaffinity(), labels, false)) {
        YRLOG_DEBUG("The resourceUnit({}) does not meet the instance preferredaffinity optimal score.", unitID);
        return false;
    }

    if (affinity.instance().has_preferredantiaffinity() &&
        !AffinityScorerMeetOptimal(unitID, affinity.instance().preferredantiaffinity(), labels, true)) {
        YRLOG_DEBUG("The resourceUnit({}) does not meet the instance preferredantiaffinity optimal score.", unitID);
        return false;
    }

    if (affinity.instance().has_requiredaffinity() &&
        IsAffinityPriority(affinity.instance().requiredaffinity()) &&
        !AffinityScorerMeetOptimal(unitID, affinity.instance().requiredaffinity(), labels, false)) {
        YRLOG_DEBUG("The resourceUnit({}) does not meet the instance requiredaffinity optimal score.", unitID);
        return false;
    }

    if (affinity.instance().has_requiredantiaffinity() &&
        IsAffinityPriority(affinity.instance().requiredantiaffinity()) &&
        !AffinityScorerMeetOptimal(unitID, affinity.instance().requiredantiaffinity(), labels, true)) {
        YRLOG_DEBUG("The resourceUnit({}) does not meet the instance requiredantiaffinity optimal score.", unitID);
        return false;
    }

    return true;
}

bool IsResourceAffinityScoreOptimal(const std::string &unitID, const resource_view::InstanceInfo &instance,
                                    const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    const auto &affinity = instance.scheduleoption().affinity();
    if (!affinity.has_resource()) {
        return true;
    }

    if (affinity.resource().has_preferredaffinity() &&
        !AffinityScorerMeetOptimal(unitID, affinity.resource().preferredaffinity(), labels, false)) {
        YRLOG_DEBUG("The resourceUnit({}) does not meet the resource preferredaffinity optimal score.", unitID);
        return false;
    }

    if (affinity.resource().has_preferredantiaffinity() &&
        !AffinityScorerMeetOptimal(unitID, affinity.resource().preferredantiaffinity(), labels, true)) {
        YRLOG_DEBUG("The resourceUnit({}) does not meet the resource preferredantiaffinity optimal score.", unitID);
        return false;
    }

    if (affinity.resource().has_requiredaffinity() &&
        IsAffinityPriority(affinity.resource().requiredaffinity()) &&
        !AffinityScorerMeetOptimal(unitID, affinity.resource().requiredaffinity(), labels, false)) {
        YRLOG_DEBUG("The resourceUnit({}) does not meet the resource requiredaffinity optimal score.", unitID);
        return false;
    }

    if (affinity.resource().has_requiredantiaffinity() &&
        IsAffinityPriority(affinity.resource().requiredantiaffinity()) &&
        !AffinityScorerMeetOptimal(unitID, affinity.resource().requiredantiaffinity(), labels, true)) {
        YRLOG_DEBUG("The resourceUnit({}) does not meet the resource requiredantiaffinity optimal score.", unitID);
        return false;
    }

    return true;
}

bool IsPreemptAffinityScoreOptimal(const std::string &unitID, const resource_view::InstanceInfo &instance,
                                   const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    const auto &affinity = instance.scheduleoption().affinity();
    if (!affinity.has_inner() || !affinity.inner().has_preempt()) {
        return true;
    }

    if (affinity.inner().preempt().has_preferredaffinity() &&
        !AffinityScorerMeetOptimal(unitID, affinity.inner().preempt().preferredaffinity(), labels, false)) {
        YRLOG_DEBUG("The resourceUnit({}) does not meet the preempt preferredaffinity optimal score.", unitID);
        return false;
    }

    if (affinity.inner().preempt().has_preferredantiaffinity() &&
        !AffinityScorerMeetOptimal(unitID, affinity.inner().preempt().preferredantiaffinity(), labels, true)) {
        YRLOG_DEBUG("The resourceUnit({}) does not meet the preempt preferredantiaffinity optimal score.", unitID);
        return false;
    }

    return true;
}

bool IsDataAffinityScoreOptimal(const std::string &unitID, const resource_view::InstanceInfo &instance,
                                const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    const auto &affinity = instance.scheduleoption().affinity();

    if (affinity.has_inner() && affinity.inner().has_data() && affinity.inner().data().has_preferredaffinity() &&
        !AffinityScorerMeetOptimal(unitID, affinity.inner().data().preferredaffinity(), labels, false)) {
        YRLOG_DEBUG("The resourceUnit({}) does not meet the data preferredaffinity optimal score.", unitID);
        return false;
    }
    return true;
}

bool IsTenantAffinityScoreOptimal(const std::string &unitID, const resource_view::InstanceInfo &instance,
                                  const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    const auto &affinity = instance.scheduleoption().affinity();
    if (affinity.has_inner() && affinity.inner().has_tenant() && affinity.inner().tenant().has_preferredaffinity() &&
        !AffinityScorerMeetOptimal(unitID, affinity.inner().tenant().preferredaffinity(), labels, false)) {
        YRLOG_DEBUG("The resourceUnit({}) does not meet the tenant preferredaffinity optimal score.", unitID);
        return false;
    }
    return true;
}

bool IsGroupScheduleAffinityScoreOptimal(const std::string &unitID, const resource_view::InstanceInfo &instance,
                                   const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    const auto &affinity = instance.scheduleoption().affinity();
    if (!affinity.has_inner() || !affinity.inner().has_grouplb()) {
        return true;
    }

    if (affinity.inner().grouplb().has_preferredaffinity() &&
        !AffinityScorerMeetOptimal(unitID, affinity.inner().grouplb().preferredaffinity(), labels, false)) {
        YRLOG_DEBUG("The resourceUnit({}) does not meet the grouplb preferredaffinity optimal score.", unitID);
        return false;
    }

    if (affinity.inner().grouplb().has_preferredantiaffinity() &&
        !AffinityScorerMeetOptimal(unitID, affinity.inner().grouplb().preferredantiaffinity(), labels, true)) {
        YRLOG_DEBUG("The resourceUnit({}) does not meet the grouplb preferredantiaffinity optimal score.", unitID);
        return false;
    }
    return true;
}

bool IsInnerAffinityScoreOptimal(const resource_view::ResourceUnit &resourceUnit,
                                 const resource_view::InstanceInfo &instance,
                                 const std::shared_ptr<schedule_framework::PreAllocatedContext> &preContext)
{
    auto unitId = resourceUnit.id();
    auto ownerId = resourceUnit.ownerid();
    // 1.check if the inner preempt affinity meets the optimal score.
    auto localNodeLabels = GetLocalNodeLabels(resourceUnit, preContext);
    if (!IsPreemptAffinityScoreOptimal(ownerId, instance, localNodeLabels)) {
        return false;
    }

    // 2.check if the inner data affinity meets the optimal score.
    if (!IsDataAffinityScoreOptimal(unitId, instance, resourceUnit.nodelabels())) {
        return false;
    }
	
	// 3.check if the inner Tenant affinity meets the optimal score.
    // merge scheduled instance labels
    auto unitChildAgentLabels = resourceUnit.nodelabels() + preContext->allocatedLabels[unitId];
    if (!IsTenantAffinityScoreOptimal(unitId, instance, unitChildAgentLabels)) {
        return false;
    }

    // 4.check if the inner group affinity meets the optimal score.
    if (!IsGroupScheduleAffinityScoreOptimal(unitId, instance, unitChildAgentLabels)) {
        return false;
    }
    return true;
}

bool IsInstanceRequiredAffinityPassed(const std::string &unitID, const resource_view::InstanceInfo &instance,
                                      const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    const auto &affinity = instance.scheduleoption().affinity();
    if (!affinity.has_instance()) {
        return true;
    }

    if (affinity.instance().has_requiredaffinity() &&
        !RequiredAffinityFilter(unitID, affinity.instance().requiredaffinity(), labels)) {
        return false;
    }

    if (affinity.instance().has_requiredantiaffinity() &&
        !RequiredAntiAffinityFilter(unitID, affinity.instance().requiredantiaffinity(), labels)) {
        return false;
    }

    return true;
}

bool IsResourceRequiredAffinityPassed(const std::string &unitID, const resource_view::InstanceInfo &instance,
                                      const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    const auto &affinity = instance.scheduleoption().affinity();
    if (!affinity.has_resource()) {
        return true;
    }

    if (affinity.resource().has_requiredaffinity() &&
        !RequiredAffinityFilter(unitID, affinity.resource().requiredaffinity(), labels)) {
        return false;
    }

    if (affinity.resource().has_requiredantiaffinity() &&
        !RequiredAntiAffinityFilter(unitID, affinity.resource().requiredantiaffinity(), labels)) {
        return false;
    }

    return true;
}

bool IsInnerTenantRequiredAffinityPassed(const std::string &unitID, const resource_view::InstanceInfo &instance,
    const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    const auto &affinity = instance.scheduleoption().affinity();
    if (!affinity.has_inner() || !affinity.inner().has_tenant() ||
        !affinity.inner().tenant().has_requiredantiaffinity()) {
        return true;
    }
    return RequiredAntiAffinityFilter(unitID, affinity.inner().tenant().requiredantiaffinity(), labels);
}

bool IsInnerResourceGroupRequiredAffinityPassed(
    const std::string &unitID, const resource_view::InstanceInfo &instance,
    const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    const auto &affinity = instance.scheduleoption().affinity();
    if (!affinity.has_inner() || !affinity.inner().has_rgroup()
        || !affinity.inner().rgroup().has_requiredaffinity()) {
        return true;
    }
    return RequiredAffinityFilter(unitID, affinity.inner().rgroup().requiredaffinity(), labels);
}

bool IsInnerGroupScheduleRequiredAffinityPassed(
    const std::string &unitID, const resource_view::InstanceInfo &instance,
    const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    const auto &affinity = instance.scheduleoption().affinity();
    if (!affinity.has_inner() || !affinity.inner().has_grouplb()
        || !affinity.inner().grouplb().has_requiredantiaffinity()) {
        return true;
    }
    return RequiredAntiAffinityFilter(unitID, affinity.inner().grouplb().requiredantiaffinity(), labels);
}

bool IsInnerPendingRequiredAffinityPassed(const std::string &unitID, const resource_view::InstanceInfo &instance,
    const ::google::protobuf::Map<std::string, resource_view::ValueCounter> &labels)
{
    const auto &affinity = instance.scheduleoption().affinity();
    if (!affinity.has_inner() || !affinity.inner().has_pending()) {
        return true;
    }

    const auto &pending = affinity.inner().pending();
    for (const auto& pendingResource : pending.resources()) {
        bool isPendingRequirementMet = true;
        if (pendingResource.has_requiredaffinity()) {
            isPendingRequirementMet &= RequiredAffinityFilter(unitID, pendingResource.requiredaffinity(), labels);
        }

        if (pendingResource.has_requiredantiaffinity()) {
            isPendingRequirementMet &= RequiredAntiAffinityFilter(unitID, pendingResource.requiredantiaffinity(),
                                                                  labels);
        }

        // If true, the resource matches pending instances requirements and should be excluded from scheduling
        if (isPendingRequirementMet) {
            return false;
        }
    }

    return true;
}

bool NeedLabelFilter(const resource_view::InstanceInfo &instance)
{
    const auto &affinity = instance.scheduleoption().affinity();

    // 1.check instance-related affinity
    if (affinity.has_instance()) {
        if (affinity.instance().has_requiredaffinity() || affinity.instance().has_requiredantiaffinity()) {
            return true;
        }
    }

    // 2.check resource-related affinity
    if (affinity.has_resource()) {
        if (affinity.resource().has_requiredaffinity() || affinity.resource().has_requiredantiaffinity()) {
            return true;
        }
    }

    // 3.check inner-related affinity
    if (affinity.has_inner()) {
        if (affinity.inner().has_tenant() && affinity.inner().tenant().has_requiredantiaffinity()) {
            return true;
        }
        if (affinity.inner().has_pending() &&
            (!affinity.inner().pending().resources().empty())) {
            return true;
        }
        if (affinity.inner().has_rgroup() && affinity.inner().rgroup().has_requiredaffinity()) {
            return true;
        }
        if (affinity.inner().has_grouplb() && affinity.inner().grouplb().has_requiredantiaffinity()) {
            return true;
        }
    }

    return false;
}

// Returns true if the filtering passes, false otherwise.
bool LabelAffinityFilter::PerformLabelFilter(
    const resource_view::InstanceInfo &instance,
    messages::AffinityContext &affinityCtx,
    const resource_view::ResourceUnit &resourceUnit,
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &ctx) const
{
    auto unitId = resourceUnit.id();
    auto ownerId = resourceUnit.ownerid();

    if (!CheckAgentAvailable(instance, affinityCtx, resourceUnit, ctx)) {
        return false;
    }

    // merge scheduled instance labels
    auto unitLabels = resourceUnit.nodelabels() + ctx->allocatedLabels[resourceUnit.id()];
    auto localNodeLabels = GetLocalNodeLabels(resourceUnit, ctx);
    // 1.Filter instance-related affinity
    if (IsNodeAffinityScope(instance) &&
        !IsInstanceRequiredAffinityPassed(ownerId, instance, localNodeLabels)) {
        YRLOG_DEBUG("{}|instance({}) agent({}) failed to perform instance node affinity filtering. nodelabel({})",
                    instance.requestid(), instance.instanceid(), unitId,
                    DebugProtoMapString(localNodeLabels));
        ctx->TagNodeUnfeasible(ownerId);
        return false;
    }
    if (!IsNodeAffinityScope(instance) && !IsInstanceRequiredAffinityPassed(unitId, instance, unitLabels)) {
        YRLOG_DEBUG("{}|instance({}) agent({}) failed to perform instance affinity filtering. unitLabels({})",
                    instance.requestid(), instance.instanceid(), unitId, DebugProtoMapString(unitLabels));
        return false;
    }
    // 2.Filter resource-related affinity
    if (!IsResourceRequiredAffinityPassed(resourceUnit.id(), instance, resourceUnit.nodelabels())) {
        YRLOG_DEBUG("{}|instance({}) agent({}) failed to perform resource affinity filtering. nodelabels({})",
                    instance.requestid(), instance.instanceid(), unitId,
                    DebugProtoMapString(resourceUnit.nodelabels()));
        return false;
    }
    // 3.Filter inner-related affinity
    if (!IsInnerTenantRequiredAffinityPassed(resourceUnit.id(), instance, unitLabels)) {
        YRLOG_DEBUG("{}|instance({}) agent({}) failed to perform inner(tenant) affinity filtering. unitLabels({})",
                    instance.requestid(), instance.instanceid(), unitId, DebugProtoMapString(unitLabels));
        return false;
    }
    if (!IsInnerPendingRequiredAffinityPassed(resourceUnit.id(), instance, resourceUnit.nodelabels())) {
        YRLOG_DEBUG("{}|instance({}) agent({}) failed to perform inner(pending) affinity filtering. nodelabels({})",
                    instance.requestid(), instance.instanceid(), unitId,
                    DebugProtoMapString(resourceUnit.nodelabels()));
        return false;
    }
    if (!IsInnerResourceGroupRequiredAffinityPassed(resourceUnit.id(), instance, resourceUnit.nodelabels())) {
        YRLOG_DEBUG("{}|instance({}) agent({}) failed to perform inner(rgroup) affinity filtering. nodelabels({})",
                    instance.requestid(), instance.instanceid(), unitId,
                    DebugProtoMapString(resourceUnit.nodelabels()));
        return false;
    }
    if (!IsInnerGroupScheduleRequiredAffinityPassed(resourceUnit.id(), instance, unitLabels)) {
        YRLOG_DEBUG("{}|instance({}) agent({}) failed to perform inner(grouplb) affinity filtering. nodelabels({})",
                    instance.requestid(), instance.instanceid(), unitId,
                    DebugProtoMapString(resourceUnit.nodelabels()));
        return false;
    }
    return true;
}

// Returns true if the score is optimal, false otherwise.
bool LabelAffinityFilter::PerformScoreOptimalityCheck(
    const resource_view::ResourceUnit &resourceUnit,
    const resource_view::InstanceInfo &instance,
    const std::shared_ptr<schedule_framework::PreAllocatedContext> &preContext) const
{
    auto unitId = resourceUnit.id();

    // 1.check if the instance-related affinity meets the optimal score.
    bool isInstanceAffinityScoreOptimal = true;
    if (IsNodeAffinityScope(instance)) {
        auto localNodeLabels = GetLocalNodeLabels(resourceUnit, preContext);
        isInstanceAffinityScoreOptimal =
            IsInstanceAffinityScoreOptimal(unitId, instance, localNodeLabels);
    } else {
        // merge scheduled instance labels
        auto unitChildAgentLabels = resourceUnit.nodelabels() + preContext->allocatedLabels[unitId];
        isInstanceAffinityScoreOptimal = IsInstanceAffinityScoreOptimal(unitId, instance, unitChildAgentLabels);
    }
    if (!isInstanceAffinityScoreOptimal) {
        return false;
    }

    // 2.check if the inner-related affinity meets the optimal score.
    if (!IsInnerAffinityScoreOptimal(resourceUnit, instance, preContext)) {
        return false;
    }

    // 3.check if the resource-related affinity meets the optimal score.
    if (!IsResourceAffinityScoreOptimal(unitId, instance, resourceUnit.nodelabels())) {
        return false;
    }

    return true;
}

schedule_framework::Filtered LabelAffinityFilter::Filter(
    const std::shared_ptr<schedule_framework::ScheduleContext> &ctx,
    const resource_view::InstanceInfo &instance,
    const resource_view::ResourceUnit &resourceUnit)
{
    schedule_framework::Filtered result{};
    result.status = Status::OK();
    result.availableForRequest = -1;

    const auto preContext = std::dynamic_pointer_cast<schedule_framework::PreAllocatedContext>(ctx);
    if (preContext == nullptr || preContext->pluginCtx == nullptr || preContext->allLocalLabels.empty()) {
        YRLOG_WARN("{}|invalid context for LabelAffinityFilter", instance.requestid());
        result.status = Status(StatusCode::PARAMETER_ERROR, "Invalid context");
        return result;
    }

    auto &pluginCtx = *preContext->pluginCtx;
    auto &affinityCtx = *pluginCtx[LABEL_AFFINITY_PLUGIN].mutable_affinityctx();
    if (isRootDomainLevel_) {
        affinityCtx.set_istopdownscheduling(true);
    }

    // 1. Performs the label filtering operation.
    if (NeedLabelFilter(instance) && !PerformLabelFilter(instance, affinityCtx, resourceUnit, preContext)) {
        YRLOG_WARN("{}|The resourceUnit({}) failed to required affinity filter.", instance.requestid(),
                   resourceUnit.id());
        (*affinityCtx.mutable_scheduledresult())[resourceUnit.id()] = StatusCode::AFFINITY_SCHEDULE_FAILED;
        result.status = Status(StatusCode::AFFINITY_SCHEDULE_FAILED, "Affinity can't be Satisfied");
        result.required = GetRequiredAffinityString(instance);
        return result;
    }

    // 2. Performs the score optimality check.
    if (NeedOptimalAffinityCheck(isRelaxed_, affinityCtx.istopdownscheduling()) && NeedAffinityScorer(instance)
        && !PerformScoreOptimalityCheck(resourceUnit, instance, preContext)) {
        YRLOG_WARN("{}|The resourceUnit({}) does not meet the preferredaffinity optimal score.", instance.requestid(),
                   resourceUnit.id());
        result.status = Status(StatusCode::AFFINITY_SCHEDULE_FAILED, "Affinity can't be Satisfied");
        result.required = GetRequiredAffinityString(instance);
        return result;
    }
    return result;
}

std::shared_ptr<schedule_framework::FilterPlugin> RelaxedRootLabelAffinityFilterPolicyCreator()
{
    bool isRelaxed = true;
    bool isRootDomainLevel = true;
    return std::make_shared<LabelAffinityFilter>(isRelaxed, isRootDomainLevel);
}

std::shared_ptr<schedule_framework::FilterPlugin> RelaxedNonRootLabelAffinityFilterPolicyCreator()
{
    bool isRelaxed = true;
    bool isRootDomainLevel = false;
    return std::make_shared<LabelAffinityFilter>(isRelaxed, isRootDomainLevel);
}

std::shared_ptr<schedule_framework::FilterPlugin> StrictRootLabelAffinityFilterPolicyCreator()
{
    bool isRelaxed = false;
    bool isRootDomainLevel = true;
    return std::make_shared<LabelAffinityFilter>(isRelaxed, isRootDomainLevel);
}

std::shared_ptr<schedule_framework::FilterPlugin> StricNonRootLabelAffinityFilterPolicyCreator()
{
    bool isRelaxed = false;
    bool isRootDomainLevel = false;
    return std::make_shared<LabelAffinityFilter>(isRelaxed, isRootDomainLevel);
}

REGISTER_SCHEDULER_PLUGIN(RELAXED_ROOT_LABEL_AFFINITY_FILTER_NAME, RelaxedRootLabelAffinityFilterPolicyCreator);
REGISTER_SCHEDULER_PLUGIN(RELAXED_NON_ROOT_LABEL_AFFINITY_FILTER_NAME, RelaxedNonRootLabelAffinityFilterPolicyCreator);
REGISTER_SCHEDULER_PLUGIN(STRICT_ROOT_LABEL_AFFINITY_FILTER_NAME, StrictRootLabelAffinityFilterPolicyCreator);
REGISTER_SCHEDULER_PLUGIN(STRICT_NON_ROOT_LABEL_AFFINITY_FILTER_NAME, StricNonRootLabelAffinityFilterPolicyCreator);

}  // namespace functionsystem::schedule_plugin::filter