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

#ifndef FUNCTIONSYSTEM_NUMA_UTILS_H
#define FUNCTIONSYSTEM_NUMA_UTILS_H

#include <vector>
#include <mutex>
#include <atomic>

#include <numa.h>

#include "common/resource_view/resource_type.h"

namespace functionsystem::utils {

class NUMAUtils {
public:
    ~NUMAUtils() = default;
    // 检测系统是否支持 NUMA
    static bool IsNUMAAvailable();
    
    // 获取 NUMA 节点数量（最大节点号 + 1）
    static int GetNUMANodeCount();

    // 获取指定 NUMA 节点的 CPU 核心数量
    static int GetNUMANodeCPUCount(int nodeId);
    
    // 获取所有 NUMA 节点的 CPU 核心数量（vector，下标为节点号）
    static std::vector<int> GetNUMANodeCPUCounts();

    // 从 ResourceUnit 的 NUMA Resource 中获取各 NUMA 节点的剩余 CPU 数量（单位：毫核）
    // 返回 vector<double>，与 proto 一致，避免精度丢失
    static std::vector<double> GetNUMANodeCPUsFromResourceUnit(const resource_view::ResourceUnit& resourceUnit);

    // 从 allocatable Resources 中获取各 NUMA 节点的剩余 CPU 数量（单位：毫核）
    // nodeID 用于在 vectors 中查找对应节点的 vector
    static std::vector<double> GetNUMANodeCPUsFromAllocatable(const resource_view::Resources& allocatable,
                                                             const std::string& nodeID);

private:
    static std::atomic<bool> numaAvailable_;
    static std::atomic<int> maxNode_;
    static std::once_flag initFlag_;
    
    static void Initialize();
};

} // namespace functionsystem::utils

#endif // FUNCTIONSYSTEM_NUMA_UTILS_H
