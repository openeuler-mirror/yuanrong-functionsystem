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

#ifndef FUNCTIONSYSTEM_NUMA_AFFINITY_SCORER_H
#define FUNCTIONSYSTEM_NUMA_AFFINITY_SCORER_H

#include <google/protobuf/map.h>
#include "common/proto/pb/posix/common.pb.h"
#include "common/resource_view/resource_type.h"
#include "common/schedule_plugin/common/preallocated_context.h"
#include "common/scheduler_framework/framework/policy.h"
#include "common/scheduler_framework/utils/score.h"
#include "common/status/status.h"

namespace functionsystem::schedule_plugin::score {

class NUMAAffinityScorer : public schedule_framework::ScorePlugin {
public:
    NUMAAffinityScorer() = default;
    ~NUMAAffinityScorer() override = default;
    
    std::string GetPluginName() override;
    
    schedule_framework::NodeScore Score(
        const std::shared_ptr<schedule_framework::ScheduleContext>& ctx,
        const resource_view::InstanceInfo& instance,
        const resource_view::ResourceUnit& resourceUnit) override;
    
private:
    // 根据 BindStrategy 选择 NUMA 节点（支持多节点选择）
    // allocatable: 可用资源（预分配场景下需为 effectiveAllocatable = allocatable - preContext->allocated）
    // nodeID: ResourceUnit 的 id，用于在 NUMA 向量中查找
    std::vector<int> SelectNUMANodes(common::BindStrategy strategy,
                                     double requiredCPUMillicores,
                                     const resource_view::Resources& allocatable,
                                     const std::string& nodeID) const;

    // 更新 NUMA 资源分配到 vectorAllocations
    // allocatable: 可用资源（需与 SelectNUMANodes 一致，预分配场景下为 effectiveAllocatable）
    void UpdateNUMAAllocation(double requiredCPUMillicores,
                              const std::vector<int>& selectedNodes,
                              const resource_view::Resources& allocatable,
                              const std::string& nodeID,
                              schedule_framework::NodeScore& score) const;

    common::BindStrategy ParseBindStrategy(
        const google::protobuf::Map<std::string, std::string>& extension) const;

    double GetRequiredCPUMillicores(const resource_view::InstanceInfo& instance) const;

    struct ApplyNUMAAllocationContext {
        const resource_view::InstanceInfo& instance;
        const resource_view::ResourceUnit& resourceUnit;
        common::BindStrategy strategy;
        double requiredCPUMillicores;
        const resource_view::Resources& effectiveAllocatable;
        const std::vector<int>& selectedNodes;
        const google::protobuf::Map<std::string, std::string>& extension;
        schedule_framework::NodeScore& nodeScore;
    };
    void ApplyNUMAAllocation(const ApplyNUMAAllocationContext& ctx) const;
};

} // namespace functionsystem::schedule_plugin::score

#endif // FUNCTIONSYSTEM_NUMA_AFFINITY_SCORER_H
