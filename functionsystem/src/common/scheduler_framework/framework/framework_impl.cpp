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

#include "framework_impl.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <unordered_set>

#include "async/try.hpp"
#include "common/schedule_plugin/common/constants.h"
#include "common/schedule_plugin/common/preallocated_context.h"
#include "common/logs/logging.h"
#include "common/resource_view/resource_tool.h"
#include "common/scheduler_framework/framework/policy.h"
#include "common/scheduler_framework/framework/unit_evaluation_scope.h"
#include "common/status/status.h"

using namespace functionsystem::resource_view;

namespace functionsystem::schedule_framework {

const std::unordered_map<uint32_t, std::string> UNIT_STATUS = {
    { static_cast<uint32_t>(UnitStatus::EVICTING), "EVICTING" },
    { static_cast<uint32_t>(UnitStatus::RECOVERING), "RECOVERING" },
    { static_cast<uint32_t>(UnitStatus::TO_BE_DELETED), "TO_BE_DELETED" }
};

struct AggregatedStatus {
    std::unordered_map<std::string, uint32_t> results;
    std::unordered_map<std::string, std::string> requests;
    void Insert(const Status &status, const std::string &request)
    {
        auto iter = results.find(status.RawMessage());
        if (iter == results.end()) {
            results.emplace(status.RawMessage(), 1);
            requests.emplace(status.RawMessage(), request);
            return;
        }
        iter->second++;
    }

    std::string Dump(const std::string &desc)
    {
        std::ostringstream oss;
        oss << desc << (results.empty() ? ", " : ", The reasons are as follows:\n");
        for (auto iter = results.begin(); iter != results.end(); iter++) {
            oss << "\t" << iter->second << " unit with [" << iter->first << "]";
            if (auto it = requests.find(iter->first); it != requests.end() && !it->second.empty()) {
                oss << " requirements: [" << it->second + "]";
            }
            oss << "." << std::endl;
        }
        return oss.str();
    }
};

struct FrameworkImpl::SnapshotCandidateRange {
    int32_t code{ static_cast<int32_t>(StatusCode::SUCCESS) };
    std::string reason;
    const std::vector<std::string> *indexedCandidates{ nullptr };
    size_t candidateCount{ 0 };
    size_t start{ 0 };
    uint32_t expectedFeasible{ 0 };
    bool useMonopolyIndex{ false };
};

struct FrameworkImpl::SnapshotSelectionState {
    std::priority_queue<NodeScore> sortedFeasibleNodes;
    AggregatedStatus aggregate;
    int64_t aggregateCapacity{ 0 };
    bool hasUnboundedCandidate{ false };
};

static std::unordered_map<std::string, double> g_scoreWeights = {
    {schedule_plugin::DEFAULT_SCORER_NAME, 1.0},
    {schedule_plugin::DEFAULT_HETEROGENEOUS_SCORER_NAME, 1.0},
    {schedule_plugin::DISK_SCORER_NAME, 1.0},
    {schedule_plugin::LABEL_AFFINITY_SCORER_NAME, 100.0},
    {schedule_plugin::RELAXED_LABEL_AFFINITY_SCORER_NAME, 100.0},
    {schedule_plugin::STRICT_LABEL_AFFINITY_SCORER_NAME, 100.0},
};

bool FrameworkImpl::RegisterPolicy(const std::shared_ptr<SchedulePolicyPlugin> &plugin)
{
    auto ret = plugins_[plugin->GetPluginType()].emplace(plugin->GetPluginName(), plugin);
    if (!ret.second) {
        YRLOG_ERROR("duplicate plugin {} type({})", plugin->GetPluginName(), fmt::underlying(plugin->GetPluginType()));
    }
    // The default weight of each scoring plug-in is 1
    if (plugin->GetPluginType() == PolicyType::SCORE_POLICY) {
        if (g_scoreWeights.find(plugin->GetPluginName()) != g_scoreWeights.end()) {
            scorePluginWeight[plugin->GetPluginName()] = g_scoreWeights[plugin->GetPluginName()];
            return ret.second;
        }
        scorePluginWeight[plugin->GetPluginName()] = 1.0;
    }
    return ret.second;
}

bool FrameworkImpl::SupportsScheduleSnapshot() const
{
    const auto prefilters = plugins_.find(PolicyType::PRE_FILTER_POLICY);
    if (prefilters == plugins_.end() || prefilters->second.size() != 1) {
        return false;
    }
    const auto defaultPreFilter = prefilters->second.find(schedule_plugin::DEFAULT_PREFILTER_NAME);
    return defaultPreFilter != prefilters->second.end() && defaultPreFilter->second != nullptr;
}

bool FrameworkImpl::SupportsSemanticAggregation() const
{
    static const std::unordered_set<std::string> BUILT_IN_PLUGINS{
        schedule_plugin::DEFAULT_PREFILTER_NAME,
        schedule_plugin::DEFAULT_FILTER_NAME,
        schedule_plugin::RESOURCE_SELECTOR_FILTER_NAME,
        schedule_plugin::DEFAULT_HETEROGENEOUS_FILTER_NAME,
        schedule_plugin::LABEL_AFFINITY_FILTER_NAME,
        schedule_plugin::RELAXED_ROOT_LABEL_AFFINITY_FILTER_NAME,
        schedule_plugin::STRICT_ROOT_LABEL_AFFINITY_FILTER_NAME,
        schedule_plugin::RELAXED_NON_ROOT_LABEL_AFFINITY_FILTER_NAME,
        schedule_plugin::STRICT_NON_ROOT_LABEL_AFFINITY_FILTER_NAME,
        schedule_plugin::DISK_FILTER_NAME,
        schedule_plugin::NUMA_AFFINITY_FILTER_NAME,
        schedule_plugin::DEFAULT_SCORER_NAME,
        schedule_plugin::DEFAULT_HETEROGENEOUS_SCORER_NAME,
        schedule_plugin::LABEL_AFFINITY_SCORER_NAME,
        schedule_plugin::RELAXED_LABEL_AFFINITY_SCORER_NAME,
        schedule_plugin::STRICT_LABEL_AFFINITY_SCORER_NAME,
        schedule_plugin::DISK_SCORER_NAME,
        schedule_plugin::NUMA_AFFINITY_SCORER_NAME,
    };
    for (const auto &[type, policies] : plugins_) {
        (void)type;
        for (const auto &[name, plugin] : policies) {
            if (plugin == nullptr || BUILT_IN_PLUGINS.find(name) == BUILT_IN_PLUGINS.end()) {
                return false;
            }
        }
    }
    return true;
}

bool FrameworkImpl::UnRegisterPolicy(const std::string &name)
{
    for (auto &pair : plugins_) {
        if (pair.second.find(name) != pair.second.end()) {
            (void)pair.second.erase(name);
            return true;
        }
    }
    YRLOG_WARN("Plugin {} not exist", name);
    return false;
}

bool FrameworkImpl::IsReachAggregateCapacity(const std::priority_queue<NodeScore> &feasible,
                                             int64_t aggregateCapacity, bool hasUnboundedCandidate,
                                             uint32_t expectedFeasible) const
{
    if (relaxed_ <= 0 || expectedFeasible <= 1) {
        return IsReachRelaxed(feasible, expectedFeasible);
    }
    return feasible.size() >= static_cast<size_t>(relaxed_) &&
           (hasUnboundedCandidate || aggregateCapacity >= static_cast<int64_t>(expectedFeasible));
}

ScheduleResults FrameworkImpl::SelectFeasible(const std::shared_ptr<ScheduleContext> &ctx, const InstanceInfo &instance,
                                              const ResourceUnit &resourceUnit, uint32_t expectedFeasible)
{
    YRLOG_INFO(
        "{}|going to schedule instance {}. resource({}) resource-affinity ({}), inst-affinity({}), inner-affinity({})",
        instance.requestid(), instance.instanceid(), resource_view::ToString(instance.resources()),
        instance.scheduleoption().affinity().resource().ShortDebugString(),
        instance.scheduleoption().affinity().instance().ShortDebugString(),
        instance.scheduleoption().affinity().inner().ShortDebugString());
    // prefilter
    ctx->ClearUnfeasible();
    auto prefiltered = PreFilter(ctx, instance, resourceUnit);
    if (prefiltered == nullptr) {
        return ScheduleResults{ static_cast<int32_t>(StatusCode::ERR_SCHEDULE_PLUGIN_CONFIG),
                                "invalid prefilter plugin, please check --schedule_plugins configure.",
                                {} };
    }
    const auto &status = prefiltered->status();
    if (status.IsError()) {
        YRLOG_ERROR("{}|failed to schedule instance({}), {} ", instance.requestid(), instance.instanceid(),
                    status.ToString());
        return ScheduleResults{ static_cast<int32_t>(status.StatusCode()),
                                status.MultipleErr() ? status.GetMessage() : status.RawMessage(),
                                {} };
    }
    std::priority_queue<NodeScore> sortedFeasibleNodes;
    AggregatedStatus aggregate;
    int64_t aggregateCapacity = 0;
    bool hasUnboundedCandidate = false;
    prefiltered->reset(latelySelected);
    for (; !prefiltered->end() &&
           !IsReachAggregateCapacity(sortedFeasibleNodes, aggregateCapacity, hasUnboundedCandidate,
                                     expectedFeasible);
         prefiltered->next()) {
        auto &cur = prefiltered->current();
        auto iter = resourceUnit.fragment().find(cur);
        if (iter == resourceUnit.fragment().end()) {
            continue;
        }
        const auto &unit = iter->second;
        if (unit.status() != static_cast<uint32_t>(resource_view::UnitStatus::NORMAL)) {
            std::string statusDesc =
                UNIT_STATUS.find(unit.status()) != UNIT_STATUS.end() ? UNIT_STATUS.at(unit.status()) : "Unknown";
            YRLOG_WARN("the status of resource unit {} is {}, unavailable to schedule", unit.id(), statusDesc);
            aggregate.Insert(Status(StatusCode::RESOURCE_NOT_ENOUGH,
                                    "unavailable to schedule, the status of resource unit is " + statusDesc), "");
            continue;
        }
        UnitEvaluationScope evaluation(ctx, unit);
        auto filterStatus = Filter(ctx, instance, unit);
        if (filterStatus.status.IsError()) {
            if (filterStatus.isFatalErr) {
                return ScheduleResults{ static_cast<int32_t>(filterStatus.status.StatusCode()),
                                        filterStatus.status.RawMessage(),
                                        {} };
            }
            aggregate.Insert(filterStatus.status, std::move(filterStatus.required));
            continue;
        }
        auto score = Score(ctx, instance, unit);
        YRLOG_DEBUG("{}|resourceUnit({}) passed filtering, score: {}, availableForRequest: {}",
                    instance.requestid(), unit.id(), score.score, filterStatus.availableForRequest);
        score.availableForRequest = filterStatus.availableForRequest;
        sortedFeasibleNodes.push(score);
        if (filterStatus.availableForRequest == -1) {
            hasUnboundedCandidate = true;
        } else {
            aggregateCapacity += filterStatus.availableForRequest;
        }
        latelySelected = unit.id();
    }
    if (sortedFeasibleNodes.empty()) {
        auto reason = aggregate.Dump("no available resource that meets the request requirements");
        YRLOG_ERROR("{}|failed to schedule instance({}), {}", instance.requestid(), instance.instanceid(), reason);
        return ScheduleResults{ static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH), reason, {} };
    }
    return ScheduleResults{ static_cast<int32_t>(StatusCode::SUCCESS), "", std::move(sortedFeasibleNodes) };
}

ScheduleResults FrameworkImpl::SelectFeasible(const std::shared_ptr<ScheduleContext> &ctx,
                                              const InstanceInfo &instance,
                                              const resource_view::ScheduleResourceView &resourceView,
                                              uint32_t expectedFeasible)
{
    if (!resourceView.IsSnapshot()) {
        const auto *legacy = resourceView.GetLegacyResourceUnit();
        if (legacy == nullptr) {
            return ScheduleResults{ static_cast<int32_t>(StatusCode::ERR_INNER_SYSTEM_ERROR),
                                    "Invalid Schedule Resource View", {} };
        }
        return SelectFeasible(ctx, instance, *legacy, expectedFeasible);
    }
    if (!SupportsScheduleSnapshot()) {
        return ScheduleResults{ static_cast<int32_t>(StatusCode::ERR_SCHEDULE_PLUGIN_CONFIG),
                                "snapshot scheduling requires a compatible prefilter plugin", {} };
    }
    return SelectFeasibleFromSnapshot(ctx, instance, resourceView.GetSnapshot(), expectedFeasible);
}

ScheduleResults FrameworkImpl::SelectFeasibleFromSnapshot(
    const std::shared_ptr<ScheduleContext> &ctx, const InstanceInfo &instance,
    const resource_view::ScheduleSnapshotPtr &snapshot, uint32_t expectedFeasible)
{
    if (ctx == nullptr) {
        return ScheduleResults{ static_cast<int32_t>(StatusCode::ERR_INNER_SYSTEM_ERROR),
                                "Invalid Schedule Context", {} };
    }
    if (!resource_view::IsValid(instance.resources())) {
        return ScheduleResults{ static_cast<int32_t>(StatusCode::INVALID_RESOURCE_PARAMETER),
                                "Invalid Instance Resource Value", {} };
    }

    if (snapshot == nullptr) {
        return ScheduleResults{ static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH),
                                "No Resource In Cluster", {} };
    }
    auto range = PrepareSnapshotCandidateRange(instance, snapshot);
    if (range.code != static_cast<int32_t>(StatusCode::SUCCESS)) {
        return ScheduleResults{ range.code, std::move(range.reason), {} };
    }
    if (range.candidateCount == 0) {
        return ScheduleResults{ static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH),
                                "No Resource In Cluster", {} };
    }
    range.start = FindSnapshotStart(snapshot, range);
    range.expectedFeasible = expectedFeasible;
    ctx->ClearUnfeasible();
    return ScanSnapshotCandidates(ctx, instance, snapshot, range);
}

FrameworkImpl::SnapshotCandidateRange FrameworkImpl::PrepareSnapshotCandidateRange(
    const InstanceInfo &instance, const resource_view::ScheduleSnapshotPtr &snapshot) const
{
    SnapshotCandidateRange range;
    range.candidateCount = snapshot->units.size();
    if (instance.scheduleoption().schedpolicyname() != "monopoly") {
        return range;
    }
    const auto &resources = instance.resources().resources();
    const auto cpu = resources.find(resource_view::CPU_RESOURCE_NAME);
    const auto memory = resources.find(resource_view::MEMORY_RESOURCE_NAME);
    if (cpu == resources.end() || memory == resources.end()) {
        range.code = static_cast<int32_t>(StatusCode::INVALID_RESOURCE_PARAMETER);
        range.reason = "Invalid CPU: " +
                       std::to_string(cpu == resources.end() ? 0 : cpu->second.scalar().value());
        return range;
    }
    const auto cpuValue = cpu->second.scalar().value();
    if (cpuValue == 0.0 || std::abs(cpuValue) < EPSINON) {
        range.code = static_cast<int32_t>(StatusCode::INVALID_RESOURCE_PARAMETER);
        range.reason = "Invalid CPU: " + std::to_string(cpuValue);
        return range;
    }
    const auto memoryValue = memory->second.scalar().value();
    const auto requirement = "(" + std::to_string(static_cast<int64_t>(cpuValue)) + ", " +
                             std::to_string(static_cast<int64_t>(memoryValue)) + ")";
    const auto *indexed = snapshot->FindMonopolyCandidates(std::to_string(memoryValue / cpuValue),
                                                           std::to_string(memoryValue));
    if (indexed == nullptr || indexed->total == 0) {
        range.code = static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH);
        range.reason = requirement + (indexed == nullptr ? " Not Found" : " Not Enough");
        return range;
    }
    range.indexedCandidates = &indexed->unitIDs;
    range.candidateCount = indexed->unitIDs.size();
    range.useMonopolyIndex = true;
    return range;
}

size_t FrameworkImpl::FindSnapshotStart(const resource_view::ScheduleSnapshotPtr &snapshot,
                                        const SnapshotCandidateRange &range) const
{
    if (latelySelected.empty()) {
        return 0;
    }
    if (range.useMonopolyIndex) {
        const auto selected = std::find(range.indexedCandidates->begin(), range.indexedCandidates->end(),
                                        latelySelected);
        if (selected != range.indexedCandidates->end()) {
            return (static_cast<size_t>(std::distance(range.indexedCandidates->begin(), selected)) + 1) %
                   range.candidateCount;
        }
        return 0;
    }
    if (snapshot->unitIndex != nullptr) {
        const auto selected = snapshot->unitIndex->find(latelySelected);
        if (selected != snapshot->unitIndex->end() && selected->second < range.candidateCount) {
            return (selected->second + 1) % range.candidateCount;
        }
    }
    return 0;
}

std::optional<ScheduleResults> FrameworkImpl::EvaluateSnapshotCandidate(
    const std::shared_ptr<ScheduleContext> &ctx, const InstanceInfo &instance,
    const ResourceUnit &unit, SnapshotSelectionState &state)
{
    if (unit.status() != static_cast<uint32_t>(resource_view::UnitStatus::NORMAL)) {
        const auto status = UNIT_STATUS.find(unit.status());
        const auto statusDesc = status == UNIT_STATUS.end() ? "Unknown" : status->second;
        state.aggregate.Insert(
            Status(StatusCode::RESOURCE_NOT_ENOUGH,
                   "unavailable to schedule, the status of resource unit is " + statusDesc),
            "");
        return std::nullopt;
    }
    UnitEvaluationScope evaluation(ctx, unit);
    auto filterStatus = Filter(ctx, instance, unit);
    if (filterStatus.status.IsError()) {
        if (filterStatus.isFatalErr) {
            return ScheduleResults{ static_cast<int32_t>(filterStatus.status.StatusCode()),
                                    filterStatus.status.RawMessage(), {} };
        }
        state.aggregate.Insert(filterStatus.status, std::move(filterStatus.required));
        return std::nullopt;
    }
    auto score = Score(ctx, instance, unit);
    score.availableForRequest = filterStatus.availableForRequest;
    state.sortedFeasibleNodes.emplace(std::move(score));
    if (filterStatus.availableForRequest == -1) {
        state.hasUnboundedCandidate = true;
    } else {
        state.aggregateCapacity += filterStatus.availableForRequest;
    }
    latelySelected = unit.id();
    return std::nullopt;
}

ScheduleResults FrameworkImpl::ScanSnapshotCandidates(
    const std::shared_ptr<ScheduleContext> &ctx, const InstanceInfo &instance,
    const resource_view::ScheduleSnapshotPtr &snapshot, const SnapshotCandidateRange &range)
{
    SnapshotSelectionState state;
    for (size_t offset = 0; offset < range.candidateCount; ++offset) {
        if (IsReachAggregateCapacity(state.sortedFeasibleNodes, state.aggregateCapacity,
                                     state.hasUnboundedCandidate, range.expectedFeasible)) {
            break;
        }
        const auto candidate = (range.start + offset) % range.candidateCount;
        const auto *unit = range.useMonopolyIndex
                               ? snapshot->FindUnit(range.indexedCandidates->at(candidate))
                               : snapshot->units[candidate].get();
        if (unit == nullptr) {
            continue;
        }
        if (auto fatal = EvaluateSnapshotCandidate(ctx, instance, *unit, state); fatal.has_value()) {
            return std::move(fatal.value());
        }
    }
    if (state.sortedFeasibleNodes.empty()) {
        return ScheduleResults{ static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH),
                                state.aggregate.Dump("no available resource that meets the request requirements"),
                                {} };
    }
    return ScheduleResults{ static_cast<int32_t>(StatusCode::SUCCESS), "",
                            std::move(state.sortedFeasibleNodes) };
}

CandidateEvaluation FrameworkImpl::EvaluateUnit(
    const std::shared_ptr<ScheduleContext> &ctx, const InstanceInfo &instance,
    const resource_view::ScheduleResourceView &resourceView, const std::string &unitID)
{
    if (ctx == nullptr) {
        return CandidateEvaluation{ Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "Invalid Schedule Context"),
                                    false, true, NodeScore{ 0 } };
    }
    const auto *unit = resourceView.FindUnit(unitID);
    if (unit == nullptr || unit->status() != static_cast<uint32_t>(resource_view::UnitStatus::NORMAL)) {
        return CandidateEvaluation{ Status(StatusCode::RESOURCE_NOT_ENOUGH,
                                           "selected resource unit is unavailable"),
                                    false, false, NodeScore{ 0 } };
    }
    UnitEvaluationScope evaluation(ctx, *unit);
    auto filterStatus = Filter(ctx, instance, *unit);
    if (filterStatus.status.IsError()) {
        return CandidateEvaluation{ filterStatus.status, false, filterStatus.isFatalErr, NodeScore{ 0 } };
    }
    auto score = Score(ctx, instance, *unit);
    score.availableForRequest = filterStatus.availableForRequest;
    return CandidateEvaluation{ Status::OK(), true, false, std::move(score) };
}

std::shared_ptr<PreFilterResult> FrameworkImpl::PreFilter(const std::shared_ptr<ScheduleContext> &ctx,
                                                          const InstanceInfo &instance,
                                                          const ResourceUnit &resourceUnit)
{
    if (plugins_.find(PolicyType::PRE_FILTER_POLICY) == plugins_.end()) {
        YRLOG_WARN("no element of key PolicyType::PRE_FILTER_POLICY in map");
        return nullptr;
    }
    // only one prefilter plugin was performed
    for (auto it = plugins_[PolicyType::PRE_FILTER_POLICY].begin(); it != plugins_[PolicyType::PRE_FILTER_POLICY].end();
         ++it) {
        auto pre = std::dynamic_pointer_cast<PreFilterPlugin>(it->second);
        if (!pre->PrefilterMatched(instance)) {
            continue;
        }
        return pre->PreFilter(ctx, instance, resourceUnit);
    }
    return nullptr;
}

FrameworkImpl::FilterStatus FrameworkImpl::Filter(const std::shared_ptr<ScheduleContext> &ctx,
                                                  const InstanceInfo &instance, const ResourceUnit &resourceUnit)
{
    auto policy = plugins_.find(PolicyType::FILTER_POLICY);
    if (policy == plugins_.end() || policy->second.empty()) {
        YRLOG_WARN("no plugin of key PolicyType::FILTER_POLICY in map");
        return FilterStatus{ Status(StatusCode::ERR_SCHEDULE_PLUGIN_CONFIG,
                                    "empty filter plugin, please check --schedule_plugins configure."),
                             true };
    }
    int32_t availableForRequest = -1;
    for (auto it = policy->second.begin(); it != policy->second.end(); ++it) {
        auto filter = std::dynamic_pointer_cast<FilterPlugin>(it->second);
        auto filtered = filter->Filter(ctx, instance, resourceUnit);
        if (filtered.status.IsOk()) {
            if (filtered.availableForRequest > 0) {
                availableForRequest = availableForRequest == -1
                                          ? filtered.availableForRequest
                                          : std::min(availableForRequest, filtered.availableForRequest);
            }
            continue;
        }
        if (filtered.isFatalErr) {
            YRLOG_ERROR("{}|failed to schedule instance({}), plugin({}) raise err: {}", instance.requestid(),
                        instance.instanceid(), it->first, filtered.status.ToString());
            return FilterStatus{ filtered.status, true, 0, std::move(filtered.required) };
        }
        // the unit was not feasible, reason was returned by status
        return FilterStatus{ filtered.status, false, 0, std::move(filtered.required) };
    }
    // the unit was filtered successfully by all filter plugin
    return { Status::OK(), false, availableForRequest };
}

NodeScore FrameworkImpl::Score(const std::shared_ptr<ScheduleContext> &ctx, const InstanceInfo &instance,
                               const ResourceUnit &resourceUnit)
{
    auto id = resourceUnit.id();
    auto result = NodeScore{ id, 0 };
    auto policy = plugins_.find(PolicyType::SCORE_POLICY);
    if (policy == plugins_.end() || policy->second.empty()) {
        YRLOG_WARN("no plugin of key PolicyType::SCORE_POLICY in map");
        return result;
    }
    for (auto it = policy->second.begin(); it != policy->second.end(); ++it) {
        auto plugin = std::dynamic_pointer_cast<ScorePlugin>(it->second);
        auto pluginScore = plugin->Score(ctx, instance, resourceUnit);
        pluginScore.score = pluginScore.score * scorePluginWeight[plugin->GetPluginName()];
        // Performs move-semantics accumulation; after this operation, 'pluginScore' becomes invalid (moved-from state)
        result += pluginScore;
        if (!pluginScore.heteroProductName.empty()) {
            result.heteroProductName = pluginScore.heteroProductName;
        }
    }
    return result;
}

bool FrameworkImpl::IsReachRelaxed(const std::priority_queue<NodeScore> &feasible, uint32_t expectedFeasible) const
{
    if (relaxed_ <= 0) {
        return false;
    }
    return feasible.size() >= static_cast<size_t>(std::max(static_cast<uint32_t>(relaxed_), expectedFeasible));
};
}  // namespace functionsystem::schedule_framework
