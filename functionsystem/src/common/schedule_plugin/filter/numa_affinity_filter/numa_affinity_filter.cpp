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

#include "numa_affinity_filter.h"

#include "common/logs/logging.h"
#include "common/resource_view/resource_tool.h"
#include "common/schedule_plugin/common/constants.h"
#include "common/schedule_plugin/common/plugin_register.h"
#include "common/utils/numa_utils.h"

namespace functionsystem::schedule_plugin::filter {

const std::string NUMA_AFFINITY_FILTER_NAME = "NUMAAffinityFilter";

std::string NUMAAffinityFilter::GetPluginName()
{
    return NUMA_AFFINITY_FILTER_NAME;
}

bool NUMAAffinityFilter::NeedNUMAAffinityFilter(const resource_view::InstanceInfo& instance) const
{
    // 检查是否在组调度中配置了 NUMA 绑定
    // 如果 bind_resource 是 "NUMA"，无论 bind_strategy 是什么（包括 BIND_None），都需要进行 NUMA 过滤
    const auto& extension = instance.scheduleoption().extension();
    auto bindResourceIter = extension.find("bind_resource");
    
    return bindResourceIter != extension.end() && bindResourceIter->second == "NUMA";
}

bool NUMAAffinityFilter::CheckNUMAResources(
    const resource_view::InstanceInfo& instance,
    const resource_view::ResourceUnit& resourceUnit) const
{
    std::vector<double> numaCpuCounts = utils::NUMAUtils::GetNUMANodeCPUsFromResourceUnit(resourceUnit);
    return CheckNUMAResources(instance, resourceUnit, numaCpuCounts);
}

bool NUMAAffinityFilter::CheckNUMAResources(
    const resource_view::InstanceInfo& instance,
    const resource_view::ResourceUnit& resourceUnit,
    const std::vector<double>& numaCpuCounts) const
{
    if (numaCpuCounts.empty()) {
        YRLOG_DEBUG("{}|ResourceUnit({}) has no NUMA information, filtering out",
                    instance.requestid(), resourceUnit.id());
        return false;
    }
    
    const auto& resources = instance.resources().resources();
    auto cpuIter = resources.find(resource_view::CPU_RESOURCE_NAME);
    if (cpuIter == resources.end()) {
        YRLOG_DEBUG("{}|Instance({}) has no CPU requirement", instance.requestid(), instance.instanceid());
        return true;
    }
    
    double requiredCPUMillicores = cpuIter->second.scalar().value();
    if (requiredCPUMillicores <= 0) {
        return true;
    }

    double totalCPUMillicores = 0.0;
    for (double c : numaCpuCounts) {
        totalCPUMillicores += c;
    }
    if (totalCPUMillicores >= requiredCPUMillicores) {
        YRLOG_DEBUG("{}|ResourceUnit({}) NUMA total CPU {} millicores >= required {} millicores",
                    instance.requestid(), resourceUnit.id(), totalCPUMillicores, requiredCPUMillicores);
        return true;
    }
    YRLOG_DEBUG("{}|ResourceUnit({}) NUMA total CPU {} millicores < required {} millicores",
                instance.requestid(), resourceUnit.id(), totalCPUMillicores, requiredCPUMillicores);
    return false;
}

bool NUMAAffinityFilter::PerformNUMAFilter(
    const std::shared_ptr<schedule_framework::PreAllocatedContext>& preContext,
    const resource_view::InstanceInfo& instance,
    const resource_view::ResourceUnit& resourceUnit) const
{
    // 预分配场景：扣除已预分配资源后的可用量（与 DefaultFilter、NUMAAffinityScorer 一致）
    resource_view::Resources effectiveAllocatable = resourceUnit.allocatable();
    if (preContext != nullptr) {
        auto iter = preContext->allocated.find(resourceUnit.id());
        if (iter != preContext->allocated.end()) {
            effectiveAllocatable = resourceUnit.allocatable() - iter->second.resource;
            if (!resource_view::IsValid(effectiveAllocatable)) {
                YRLOG_DEBUG("{}|ResourceUnit({}) effective allocatable invalid after deducting pre-allocated",
                            instance.requestid(), resourceUnit.id());
                return false;
            }
        }
    }
    std::vector<double> numaCpuCounts =
        utils::NUMAUtils::GetNUMANodeCPUsFromAllocatable(effectiveAllocatable, resourceUnit.id());
    if (numaCpuCounts.empty()) {
        return false;
    }
    return CheckNUMAResources(instance, resourceUnit, numaCpuCounts);
}

schedule_framework::Filtered NUMAAffinityFilter::Filter(
    const std::shared_ptr<schedule_framework::ScheduleContext>& ctx,
    const resource_view::InstanceInfo& instance,
    const resource_view::ResourceUnit& resourceUnit)
{
    schedule_framework::Filtered result{};
    result.status = Status::OK();
    result.availableForRequest = -1;
    
    const auto preContext = std::dynamic_pointer_cast<schedule_framework::PreAllocatedContext>(ctx);
    
    // 检查是否需要 NUMA 亲和性过滤
    if (!NeedNUMAAffinityFilter(instance)) {
        return result; // 不需要过滤，直接通过
    }
    
    // 执行 NUMA 过滤
    if (!PerformNUMAFilter(preContext, instance, resourceUnit)) {
        YRLOG_WARN("{}|ResourceUnit({}) failed NUMA affinity filter",
                   instance.requestid(), resourceUnit.id());
        result.status = Status(StatusCode::AFFINITY_SCHEDULE_FAILED, "NUMA affinity can't be satisfied");
        result.required = "NUMA affinity requirement";
        return result;
    }
    
    return result;
}

std::shared_ptr<schedule_framework::FilterPlugin> NUMAAffinityFilterPolicyCreator()
{
    return std::make_shared<NUMAAffinityFilter>();
}

REGISTER_SCHEDULER_PLUGIN(NUMA_AFFINITY_FILTER_NAME, NUMAAffinityFilterPolicyCreator);

} // namespace functionsystem::schedule_plugin::filter
