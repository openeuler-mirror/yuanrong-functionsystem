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

#include "numa_affinity_scorer.h"

#include <algorithm>
#include <functional>

#include "common/logs/logging.h"
#include "common/resource_view/resource_tool.h"
#include "common/schedule_plugin/common/constants.h"
#include "common/schedule_plugin/common/plugin_register.h"
#include "common/utils/numa_utils.h"
#include "common/proto/pb/posix/common.pb.h"

namespace functionsystem::schedule_plugin::score {

namespace {

bool IsNodeSelected(size_t nodeIdx, const std::vector<int>& selectedNodes)
{
    int nodeId = static_cast<int>(nodeIdx);
    if (nodeId < 0 || static_cast<size_t>(nodeId) != nodeIdx) {
        return false;
    }
    return std::find(selectedNodes.begin(), selectedNodes.end(), nodeId) != selectedNodes.end();
}

bool SelectNextNodePack(const std::vector<double>& numaCpuCounts, std::vector<int>& selectedNodes,
                        double& remainingRequired)
{
    int bestNode = -1;
    double minRemainingCPU = -1.0;
    for (size_t i = 0; i < numaCpuCounts.size(); ++i) {
        if (IsNodeSelected(i, selectedNodes)) {
            continue;
        }
        if (bestNode < 0 || numaCpuCounts[i] < minRemainingCPU) {
            bestNode = static_cast<int>(i);
            minRemainingCPU = numaCpuCounts[i];
        }
    }
    if (bestNode < 0) {
        return false;
    }
    selectedNodes.push_back(bestNode);
    double allocated = std::min(remainingRequired, numaCpuCounts[bestNode]);
    remainingRequired -= allocated;
    YRLOG_DEBUG("PACK mode: selected NUMA node {} with {} remaining CPUs, allocated {} millicores",
                bestNode,
                numaCpuCounts[bestNode],
                allocated);
    return true;
}

bool SelectNextNodeSpread(const std::vector<double>& numaCpuCounts, std::vector<int>& selectedNodes,
                          double& remainingRequired)
{
    int bestNode = -1;
    double maxRemainingCPU = -1.0;
    for (size_t i = 0; i < numaCpuCounts.size(); ++i) {
        if (IsNodeSelected(i, selectedNodes)) {
            continue;
        }
        if (bestNode < 0 || numaCpuCounts[i] > maxRemainingCPU) {
            bestNode = static_cast<int>(i);
            maxRemainingCPU = numaCpuCounts[i];
        }
    }
    if (bestNode < 0) {
        return false;
    }
    selectedNodes.push_back(bestNode);
    double allocated = std::min(remainingRequired, numaCpuCounts[bestNode]);
    remainingRequired -= allocated;
    YRLOG_DEBUG("SPREAD mode: selected NUMA node {} with {} remaining CPUs, allocated {} millicores",
                bestNode,
                numaCpuCounts[bestNode],
                allocated);
    return true;
}

}  // namespace

std::string NUMAAffinityScorer::GetPluginName()
{
    return NUMA_AFFINITY_SCORER_NAME;
}

std::vector<int> NUMAAffinityScorer::SelectNUMANodes(common::BindStrategy strategy,
                                                     double requiredCPUMillicores,
                                                     const resource_view::Resources& allocatable,
                                                     const std::string& nodeID) const
{
    std::vector<int> selectedNodes;
    std::vector<double> numaCpuCounts = utils::NUMAUtils::GetNUMANodeCPUsFromAllocatable(allocatable, nodeID);
    if (numaCpuCounts.empty()) {
        return selectedNodes;
    }

    double remainingRequired = requiredCPUMillicores;
    const size_t maxNodes = numaCpuCounts.size();
    if (strategy == common::BindStrategy::BIND_Pack) {
        while (remainingRequired > 0 && selectedNodes.size() < maxNodes) {
            if (!SelectNextNodePack(numaCpuCounts, selectedNodes, remainingRequired)) {
                break;
            }
        }
    } else if (strategy == common::BindStrategy::BIND_Spread) {
        while (remainingRequired > 0 && selectedNodes.size() < maxNodes) {
            if (!SelectNextNodeSpread(numaCpuCounts, selectedNodes, remainingRequired)) {
                break;
            }
        }
    }

    if (remainingRequired > 0) {
        YRLOG_WARN("Unable to allocate all required CPU ({} millicores remaining) across NUMA nodes",
                   remainingRequired);
        selectedNodes.clear();
    }
    return selectedNodes;
}

namespace {

struct ComputeNUMAAllocationsContext {
    double requiredCPUMillicores;
    const std::vector<int>& selectedNodes;
    int valuesSize;
    const std::function<double(int)>& getValue;
    std::vector<double>& allocations;
    std::vector<double>& availablePerNode;
};

bool ComputeNUMAAllocations(const ComputeNUMAAllocationsContext& ctx)
{
    const int size = ctx.valuesSize;
    ctx.availablePerNode.resize(ctx.selectedNodes.size());
    for (size_t i = 0; i < ctx.selectedNodes.size(); ++i) {
        int nodeId = ctx.selectedNodes[i];
        ctx.availablePerNode[i] = (nodeId >= 0 && nodeId < size) ? ctx.getValue(nodeId) : 0.0;
    }

    double remainingRequired = ctx.requiredCPUMillicores;
    ctx.allocations.assign(ctx.selectedNodes.size(), 0.0);
    for (size_t i = 0; i < ctx.selectedNodes.size(); ++i) {
        int nodeId = ctx.selectedNodes[i];
        if (nodeId < 0 || nodeId >= size) {
            continue;
        }
        double availableCPU = ctx.availablePerNode[i];
        if (i == ctx.selectedNodes.size() - 1) {
            ctx.allocations[i] = std::min(remainingRequired, availableCPU);
        } else {
            double avgAllocation = remainingRequired / (ctx.selectedNodes.size() - i);
            ctx.allocations[i] = std::min(avgAllocation, availableCPU);
        }
        remainingRequired -= ctx.allocations[i];
    }

    const double kEpsilon = 1e-9;
    if (remainingRequired <= kEpsilon) {
        return true;
    }
    for (int j = static_cast<int>(ctx.selectedNodes.size()) - 2; j >= 0 && remainingRequired > kEpsilon; --j) {
        double slack = ctx.availablePerNode[j] - ctx.allocations[j];
        if (slack > 0) {
            double add = std::min(remainingRequired, slack);
            ctx.allocations[j] += add;
            remainingRequired -= add;
        }
    }
    if (remainingRequired > kEpsilon) {
        YRLOG_WARN("NUMA allocation shortfall {} millicores after even split and backfill, skip allocation",
                   remainingRequired);
        return false;
    }
    return true;
}

void BuildAllocationValues(const std::vector<int>& selectedNodes, const std::vector<double>& allocations,
                           int size, const std::string& nodeID,
                           schedule_framework::VectorResourceAllocation& vectorAllocation)
{
    const std::string numaNodeCpuKey = "CPU";
    auto& allocCategory = (*vectorAllocation.allocationValues.mutable_values())[numaNodeCpuKey];
    for (int i = 0; i < size; ++i) {
        double allocationValue = 0.0;
        for (size_t j = 0; j < selectedNodes.size(); ++j) {
            if (selectedNodes[j] == i) {
                allocationValue = allocations[j];
                break;
            }
        }
        (*allocCategory.mutable_vectors())[nodeID].add_values(allocationValue);
    }
}

}  // namespace

void NUMAAffinityScorer::UpdateNUMAAllocation(double requiredCPUMillicores,
                                              const std::vector<int>& selectedNodes,
                                              const resource_view::Resources& allocatable,
                                              const std::string& nodeID,
                                              schedule_framework::NodeScore& score) const
{
    if (selectedNodes.empty()) {
        return;
    }

    const auto& resources = allocatable.resources();
    auto numaIter = resources.find(resource_view::NUMA_RESOURCE_NAME);
    if (numaIter == resources.end()) {
        return;
    }

    const auto& numaResource = numaIter->second;
    if (numaResource.type() != resource_view::ValueType::Value_Type_VECTORS || !numaResource.has_vectors()) {
        return;
    }

    const std::string numaNodeCpuKey = "CPU";
    const auto& categories = numaResource.vectors().values();
    auto cpuCategoryIter = categories.find(numaNodeCpuKey);
    if (cpuCategoryIter == categories.end()) {
        return;
    }

    const auto& vectors = cpuCategoryIter->second.vectors();
    auto vectorIter = vectors.find(nodeID);
    if (vectorIter == vectors.end() && !vectors.empty()) {
        vectorIter = vectors.begin();
    }
    if (vectorIter == vectors.end()) {
        return;
    }

    const auto& vector = vectorIter->second;
    std::vector<double> allocations;
    std::vector<double> availablePerNode;
    auto getValue = [&vector](int i) { return vector.values(i); };
    ComputeNUMAAllocationsContext allocCtx{requiredCPUMillicores, selectedNodes, vector.values_size(),
                                           getValue, allocations, availablePerNode};
    if (!ComputeNUMAAllocations(allocCtx)) {
        return;
    }

    schedule_framework::VectorResourceAllocation vectorAllocation;
    vectorAllocation.type = resource_view::NUMA_RESOURCE_NAME;
    vectorAllocation.selectedIndices = selectedNodes;
    BuildAllocationValues(selectedNodes, allocations, vector.values_size(), nodeID, vectorAllocation);
    score.vectorAllocations.emplace_back(std::move(vectorAllocation));
}

common::BindStrategy NUMAAffinityScorer::ParseBindStrategy(
    const google::protobuf::Map<std::string, std::string>& extension) const
{
    auto it = extension.find("bind_strategy");
    if (it == extension.end() || it->second == "BIND_None") {
        return common::BindStrategy::BIND_Spread;
    }
    if (it->second == "BIND_Pack") {
        return common::BindStrategy::BIND_Pack;
    }
    if (it->second == "BIND_Spread") {
        return common::BindStrategy::BIND_Spread;
    }
    return common::BindStrategy::BIND_Spread;
}

double NUMAAffinityScorer::GetRequiredCPUMillicores(const resource_view::InstanceInfo& instance) const
{
    auto it = instance.resources().resources().find(resource_view::CPU_RESOURCE_NAME);
    return (it != instance.resources().resources().end()) ? it->second.scalar().value() : 0.0;
}

void NUMAAffinityScorer::ApplyNUMAAllocation(const ApplyNUMAAllocationContext& ctx) const
{
    UpdateNUMAAllocation(ctx.requiredCPUMillicores, ctx.selectedNodes, ctx.effectiveAllocatable,
                         ctx.resourceUnit.id(), ctx.nodeScore);
    auto bindStrategyIter = ctx.extension.find("bind_strategy");
    std::string strategyStr = (ctx.strategy == common::BindStrategy::BIND_Pack) ? "BIND_Pack" : "BIND_Spread";
    if (bindStrategyIter == ctx.extension.end() || bindStrategyIter->second == "BIND_None") {
        strategyStr += " (default)";
    }
    std::string nodesStr;
    for (size_t i = 0; i < ctx.selectedNodes.size(); ++i) {
        if (i > 0) {
            nodesStr += ", ";
        }
        nodesStr += std::to_string(ctx.selectedNodes[i]);
    }
    YRLOG_DEBUG("{}|ResourceUnit({}) selected NUMA nodes [{}] (strategy: {})",
                ctx.instance.requestid(),
                ctx.resourceUnit.id(),
                nodesStr,
                strategyStr);
}

schedule_framework::NodeScore NUMAAffinityScorer::Score(
    const std::shared_ptr<schedule_framework::ScheduleContext>& ctx,
    const resource_view::InstanceInfo& instance,
    const resource_view::ResourceUnit& resourceUnit)
{
    schedule_framework::NodeScore nodeScore(0);
    nodeScore.name = resourceUnit.id();
    nodeScore.score = 0;

    const auto preContext = std::dynamic_pointer_cast<schedule_framework::PreAllocatedContext>(ctx);
    if (preContext == nullptr) {
        YRLOG_WARN("Invalid context for NUMAAffinityScorer, skip NUMA allocation");
        return nodeScore;
    }

    resource_view::Resources effectiveAllocatable = resourceUnit.allocatable();
    if (auto iter = preContext->allocated.find(resourceUnit.id()); iter != preContext->allocated.end()) {
        effectiveAllocatable = resourceUnit.allocatable() - iter->second.resource;
    }

    const auto& extension = instance.scheduleoption().extension();
    auto bindResourceIter = extension.find("bind_resource");
    if (bindResourceIter == extension.end() || bindResourceIter->second != "NUMA") {
        return nodeScore;
    }

    common::BindStrategy strategy = ParseBindStrategy(extension);
    double requiredCPUMillicores = GetRequiredCPUMillicores(instance);
    if (requiredCPUMillicores <= 0) {
        return nodeScore;
    }

    std::vector<int> selectedNodes = SelectNUMANodes(strategy, requiredCPUMillicores, effectiveAllocatable,
                                                     resourceUnit.id());
    if (!selectedNodes.empty()) {
        ApplyNUMAAllocation(ApplyNUMAAllocationContext{
            instance, resourceUnit, strategy, requiredCPUMillicores,
            effectiveAllocatable, selectedNodes, extension, nodeScore});
    }
    return nodeScore;
}

std::shared_ptr<schedule_framework::ScorePlugin> NUMAAffinityScorerPolicyCreator()
{
    return std::make_shared<NUMAAffinityScorer>();
}

REGISTER_SCHEDULER_PLUGIN(NUMA_AFFINITY_SCORER_NAME, NUMAAffinityScorerPolicyCreator);

} // namespace functionsystem::schedule_plugin::score
