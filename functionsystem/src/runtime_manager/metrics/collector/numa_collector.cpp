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

#include "numa_collector.h"

#include "common/logs/logging.h"
#include "common/resource_view/resource_type.h"
#include "common/utils/numa_utils.h"

namespace functionsystem::runtime_manager {

NUMACollector::NUMACollector(const std::shared_ptr<ProcFSTools> procFSTools)
    : BaseMetricsCollector(metrics_type::NUMA, collector_type::SYSTEM, procFSTools)
{
    uuid_ = litebus::uuid_generator::UUID::GetRandomUUID().ToString();
}

litebus::Future<Metric> NUMACollector::GetUsage() const
{
    litebus::Promise<Metric> promise;
    Metric metric = GetNUMACPUInfo();
    metric.value = 0;
    promise.SetValue(metric);
    return promise.GetFuture();
}

Metric NUMACollector::GetLimit() const
{
    return GetNUMACPUInfo();
}

std::string NUMACollector::GenFilter() const
{
    // system-numa
    return litebus::os::Join(collectorType_, metricsType_, '-');
}

Metric NUMACollector::GetNUMACPUInfo() const
{
    Metric metric;
    
    // 检查系统是否支持 NUMA
    if (!utils::NUMAUtils::IsNUMAAvailable()) {
        YRLOG_DEBUG("NUMA is not available, returning empty metric");
        return metric;
    }
    
    // 获取所有 NUMA 节点的总 CPU 核心数量
    std::vector<int> totalCPUs = utils::NUMAUtils::GetNUMANodeCPUCounts();
    if (totalCPUs.empty()) {
        YRLOG_DEBUG("Failed to get NUMA node CPU counts, returning empty metric");
        return metric;
    }
    
    // 使用 devClusterMetrics 抽象存储 NUMA 节点信息（与 Disk/XPU 保持一致）
    DevClusterMetrics devClusterMetrics;
    devClusterMetrics.uuid = uuid_;  // 使用随机 UUID（每个节点不同，像 Disk）
    devClusterMetrics.count = totalCPUs.size();  // NUMA 节点数量
    
    // 将 NUMA 节点 ID 列表存储到 intsInfo["ids"] 中（与 XPU 保持一致）
    std::vector<int> nodeIDs;
    nodeIDs.reserve(totalCPUs.size());
    for (size_t i = 0; i < totalCPUs.size(); ++i) {
        nodeIDs.push_back(static_cast<int>(i));  // NUMA 节点 ID：0, 1, 2, ...
    }
    devClusterMetrics.intsInfo[resource_view::IDS_KEY] = nodeIDs;
    
    // 将每个 NUMA 节点的 CPU 数量（转换为毫核）存储到 intsInfo["CPU"] 中
    constexpr double millicoresPerCore = 1000.0;
    std::vector<int> cpuCountsMillicores;
    cpuCountsMillicores.reserve(totalCPUs.size());
    for (int cpuCountCores : totalCPUs) {
        cpuCountsMillicores.push_back(static_cast<int>(cpuCountCores * millicoresPerCore));
    }
    devClusterMetrics.intsInfo["CPU"] = cpuCountsMillicores;
    
    metric.devClusterMetrics = devClusterMetrics;
    metric.value = static_cast<double>(totalCPUs.size());  // 使用节点数量作为 value
    
    std::string cpuCountsStr;
    for (size_t i = 0; i < totalCPUs.size(); ++i) {
        if (i > 0) cpuCountsStr += ", ";
        cpuCountsStr += std::to_string(totalCPUs[i]);
    }
    YRLOG_DEBUG("Collected NUMA info: {} nodes, CPU counts: [{}]", totalCPUs.size(), cpuCountsStr);
    
    return metric;
}

}  // namespace functionsystem::runtime_manager
