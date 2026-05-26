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

#include "common/utils/numa_utils.h"

#include <algorithm>
#include "common/logs/logging.h"
#include "common/resource_view/resource_type.h"

namespace functionsystem::utils {

std::atomic<bool> NUMAUtils::numaAvailable_(false);
std::atomic<int> NUMAUtils::maxNode_(-1);
std::once_flag NUMAUtils::initFlag_;

void NUMAUtils::Initialize()
{
#ifdef ENABLE_NUMA
    if (FsNumaAvailable() < 0) {
        YRLOG_WARN("NUMA is not available on this system");
        numaAvailable_ = false;
        maxNode_ = -1;
        return;
    }
    numaAvailable_ = true;
    maxNode_ = FsNumaMaxNode();
    if (maxNode_ < 0) {
        YRLOG_WARN("Failed to get NUMA max node, treating as non-NUMA system");
        numaAvailable_ = false;
        maxNode_ = -1;
    } else {
        YRLOG_INFO("NUMA is available, max node: {}", maxNode_);
    }
#else
    YRLOG_INFO("NUMA support is disabled at compile time");
    numaAvailable_ = false;
    maxNode_ = -1;
#endif
}

bool NUMAUtils::IsNUMAAvailable()
{
#ifdef ENABLE_NUMA
    std::call_once(initFlag_, Initialize);
    return numaAvailable_.load();
#else
    return false;
#endif
}

int NUMAUtils::GetNUMANodeCount()
{
#ifdef ENABLE_NUMA
    if (!IsNUMAAvailable()) {
        return 1; // 非 NUMA 系统返回单节点
    }
    return maxNode_.load() + 1;
#else
    return 1;
#endif
}

int NUMAUtils::GetNUMANodeCPUCount(int nodeId)
{
#ifdef ENABLE_NUMA
    if (!IsNUMAAvailable()) {
        return 0;
    }
    
    Bitmask* cpus = FsNumaAllocateCpumask();
    if (FsNumaNodeToCpus(nodeId, cpus) != 0) {
        YRLOG_ERROR("Failed to get CPU mask for NUMA node {}", nodeId);
        FsNumaFreeCpumask(cpus);
        return 0;
    }
    
    int count = 0;
    int maxCpu = FsNumaNumConfiguredCpus();
    for (int i = 0; i < maxCpu; ++i) {
        if (FsNumaBitmaskIsbitset(cpus, i)) {
            count++;
        }
    }
    
    FsNumaFreeCpumask(cpus);
    return count;
#else
    (void)nodeId;
    return 0;
#endif
}

std::vector<int> NUMAUtils::GetNUMANodeCPUCounts()
{
#ifdef ENABLE_NUMA
    std::vector<int> cpuCounts;
    
    if (!IsNUMAAvailable()) {
        // 非 NUMA 系统，返回空 vector
        return cpuCounts;
    }
    
    int nodeCount = GetNUMANodeCount();
    cpuCounts.reserve(nodeCount);
    
    for (int i = 0; i < nodeCount; ++i) {
        int cpuCount = GetNUMANodeCPUCount(i);
        cpuCounts.push_back(cpuCount);
    }
    
    return cpuCounts;
#else
    return {};
#endif
}

static std::vector<double> GetNUMANodeCPUsFromAllocatableImpl(const resource_view::Resources& allocatable,
                                                              const std::string& nodeID)
{
    std::vector<double> cpuCounts;
    const auto& resources = allocatable.resources();
    auto numaIter = resources.find(resource_view::NUMA_RESOURCE_NAME);
    if (numaIter == resources.end()) {
        return cpuCounts;
    }

    const auto& numaResource = numaIter->second;
    if (numaResource.type() != resource_view::ValueType::Value_Type_VECTORS || !numaResource.has_vectors()) {
        return cpuCounts;
    }

    const auto& categories = numaResource.vectors().values();
    const std::string numaNodeCpuKey = "CPU";
    auto cpuCategoryIter = categories.find(numaNodeCpuKey);
    if (cpuCategoryIter == categories.end()) {
        return cpuCounts;
    }

    const auto& vectors = cpuCategoryIter->second.vectors();
    if (vectors.empty()) {
        return cpuCounts;
    }

    auto vectorIter = vectors.find(nodeID);
    if (vectorIter == vectors.end()) {
        vectorIter = vectors.begin();
    }

    const auto& vector = vectorIter->second;
    cpuCounts.reserve(vector.values_size());
    for (int i = 0; i < vector.values_size(); ++i) {
        cpuCounts.push_back(vector.values(i));
    }
    return cpuCounts;
}

std::vector<double> NUMAUtils::GetNUMANodeCPUsFromResourceUnit(const resource_view::ResourceUnit& resourceUnit)
{
    return GetNUMANodeCPUsFromAllocatableImpl(resourceUnit.allocatable(), resourceUnit.id());
}

std::vector<double> NUMAUtils::GetNUMANodeCPUsFromAllocatable(const resource_view::Resources& allocatable,
                                                              const std::string& nodeID)
{
    return GetNUMANodeCPUsFromAllocatableImpl(allocatable, nodeID);
}

} // namespace functionsystem::utils
