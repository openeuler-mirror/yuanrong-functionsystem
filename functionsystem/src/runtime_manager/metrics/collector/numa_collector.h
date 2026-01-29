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

#ifndef RUNTIME_MANAGER_METRICS_COLLECTOR_NUMA_COLLECTOR_H
#define RUNTIME_MANAGER_METRICS_COLLECTOR_NUMA_COLLECTOR_H

#include "base_metrics_collector.h"

namespace functionsystem::runtime_manager {


/**
 * NUMA 资源收集器
 * 负责收集 NUMA 拓扑信息并计算每个 NUMA 节点的 CPU 资源
 * 使用 Vectors 类型存储，类似 XPU 采集方式
 */
class NUMACollector : public BaseMetricsCollector {
public:
    explicit NUMACollector(const std::shared_ptr<ProcFSTools> procFSTools = nullptr);
    ~NUMACollector() override = default;

    litebus::Future<Metric> GetUsage() const override;
    Metric GetLimit() const override;
    std::string GenFilter() const override;

private:
    /**
     * 获取 NUMA 节点的 CPU 信息
     * @return Metric 包含 devClusterMetrics 字段，存储 NUMA 节点信息（与 Disk/XPU 保持一致）
     */
    Metric GetNUMACPUInfo() const;
    
    std::string uuid_;  // 随机生成的 UUID（每个节点不同，像 Disk）
};

}  // namespace functionsystem::runtime_manager

#endif  // RUNTIME_MANAGER_METRICS_COLLECTOR_NUMA_COLLECTOR_H
