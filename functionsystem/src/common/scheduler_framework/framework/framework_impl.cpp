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

#include <string>

#include "async/try.hpp"
#include "common/schedule_plugin/common/constants.h"
#include "common/logs/logging.h"
#include "common/resource_view/resource_tool.h"
#include "common/scheduler_framework/framework/policy.h"
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
    prefiltered->reset(latelySelected);
    for (; !prefiltered->end() && !IsReachRelaxed(sortedFeasibleNodes, expectedFeasible); prefiltered->next()) {
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
        score.availableForRequest = filterStatus.availableForRequest;
        sortedFeasibleNodes.push(score);
        latelySelected = unit.id();
    }
    if (sortedFeasibleNodes.empty()) {
        auto reason = aggregate.Dump("no available resource that meets the request requirements");
        YRLOG_ERROR("{}|failed to schedule instance({}), {}", instance.requestid(), instance.instanceid(), reason);
        return ScheduleResults{ static_cast<int32_t>(StatusCode::RESOURCE_NOT_ENOUGH), reason, {} };
    }
    return ScheduleResults{ static_cast<int32_t>(StatusCode::SUCCESS), "", std::move(sortedFeasibleNodes) };
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