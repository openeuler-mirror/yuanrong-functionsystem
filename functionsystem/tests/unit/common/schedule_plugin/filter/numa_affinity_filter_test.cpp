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

#include "common/schedule_plugin/filter/numa_affinity_filter/numa_affinity_filter.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/resource_view/resource_type.h"
#include "common/resource_view/view_utils.h"
#include "common/schedule_plugin/common/preallocated_context.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace functionsystem::schedule_plugin::filter;
using namespace functionsystem::schedule_framework;

const std::string DEFAULT_NUMA_NODE_ID = "test_numa_node";

class NUMAAffinityFilterTest : public Test {};

// 不需要 NUMA 过滤时直接通过
TEST_F(NUMAAffinityFilterTest, PassWhenNoBindResource) {
    NUMAAffinityFilter filter;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(500);

    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 1000.0, 1000.0 }, 2000.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto result = filter.Filter(preAllocated, instance, unit);

    EXPECT_EQ(result.status, StatusCode::SUCCESS);
}

// bind_resource != "NUMA" 时直接通过
TEST_F(NUMAAffinityFilterTest, PassWhenBindResourceNotNUMA) {
    NUMAAffinityFilter filter;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(500);
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "GPU";

    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 1000.0, 1000.0 }, 2000.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto result = filter.Filter(preAllocated, instance, unit);

    EXPECT_EQ(result.status, StatusCode::SUCCESS);
}

// bind_resource=NUMA 且 NUMA 资源充足时通过
TEST_F(NUMAAffinityFilterTest, PassWhenNUMAResourceSufficient) {
    NUMAAffinityFilter filter;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(500);
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "NUMA";

    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 1000.0, 1000.0 }, 2000.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto result = filter.Filter(preAllocated, instance, unit);

    EXPECT_EQ(result.status, StatusCode::SUCCESS);
}

// bind_resource=NUMA 但 ResourceUnit 无 NUMA 信息时过滤失败
TEST_F(NUMAAffinityFilterTest, FailWhenNoNUMAInfo) {
    NUMAAffinityFilter filter;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(500);
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "NUMA";

    auto unit = view_utils::Get1DResourceUnit();

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto result = filter.Filter(preAllocated, instance, unit);

    EXPECT_EQ(result.status, StatusCode::AFFINITY_SCHEDULE_FAILED);
    EXPECT_EQ(result.required, "NUMA affinity requirement");
}

// bind_resource=NUMA 但 NUMA 资源不足时过滤失败
TEST_F(NUMAAffinityFilterTest, FailWhenNUMAResourceInsufficient) {
    NUMAAffinityFilter filter;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(1500);
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "NUMA";

    // 每个 NUMA 节点只有 500 毫核，无法满足 1500
    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 500.0, 500.0 }, 1000.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto result = filter.Filter(preAllocated, instance, unit);

    EXPECT_EQ(result.status, StatusCode::AFFINITY_SCHEDULE_FAILED);
}

// 实例无 CPU 需求时通过（任意 NUMA 节点都满足）
TEST_F(NUMAAffinityFilterTest, PassWhenNoCPURequirement) {
    NUMAAffinityFilter filter;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources()).erase(resource_view::CPU_RESOURCE_NAME);
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "NUMA";

    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 100.0, 100.0 }, 200.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto result = filter.Filter(preAllocated, instance, unit);

    EXPECT_EQ(result.status, StatusCode::SUCCESS);
}

// CPU 需求为 0 时通过
TEST_F(NUMAAffinityFilterTest, PassWhenZeroCPURequirement) {
    NUMAAffinityFilter filter;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(0);
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "NUMA";

    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 100.0, 100.0 }, 200.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    auto result = filter.Filter(preAllocated, instance, unit);

    EXPECT_EQ(result.status, StatusCode::SUCCESS);
}

// 预分配场景：扣除已分配后仍充足则通过
TEST_F(NUMAAffinityFilterTest, PassWithPreAllocatedContext) {
    NUMAAffinityFilter filter;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(1000);
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "NUMA";

    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 1000.0, 1000.0 }, 2000.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    resource_view::Resources allocated;
    (*allocated.mutable_resources())[resource_view::CPU_RESOURCE_NAME] =
        view_utils::GetNameResourceWithValue(view_utils::RESOURCE_CPU_NAME, 500);
    (*allocated.mutable_resources())[resource_view::MEMORY_RESOURCE_NAME] =
        view_utils::GetNameResourceWithValue(view_utils::RESOURCE_MEM_NAME, 512);
    (*allocated.mutable_resources())[resource_view::NUMA_RESOURCE_NAME] =
        view_utils::GetNUMAResource({ 500.0, 500.0 }, DEFAULT_NUMA_NODE_ID);
    preAllocated->allocated[unit.id()].resource = std::move(allocated);

    auto result = filter.Filter(preAllocated, instance, unit);

    EXPECT_EQ(result.status, StatusCode::SUCCESS);
}

// 预分配场景：扣除已分配后不足则失败
TEST_F(NUMAAffinityFilterTest, FailWithPreAllocatedInsufficient) {
    NUMAAffinityFilter filter;
    auto instance = view_utils::Get1DInstance();
    (*instance.mutable_resources()->mutable_resources())[resource_view::CPU_RESOURCE_NAME]
        .mutable_scalar()->set_value(1100);
    (*instance.mutable_scheduleoption()->mutable_extension())["bind_resource"] = "NUMA";

    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 1000.0, 1000.0 }, 2000.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto preAllocated = std::make_shared<PreAllocatedContext>();
    resource_view::Resources allocated;
    (*allocated.mutable_resources())[resource_view::CPU_RESOURCE_NAME] =
        view_utils::GetNameResourceWithValue(view_utils::RESOURCE_CPU_NAME, 500);
    (*allocated.mutable_resources())[resource_view::MEMORY_RESOURCE_NAME] =
        view_utils::GetNameResourceWithValue(view_utils::RESOURCE_MEM_NAME, 512);
    (*allocated.mutable_resources())[resource_view::NUMA_RESOURCE_NAME] =
        view_utils::GetNUMAResource({ 500.0, 500.0 }, DEFAULT_NUMA_NODE_ID);
    preAllocated->allocated[unit.id()].resource = std::move(allocated);

    auto result = filter.Filter(preAllocated, instance, unit);

    EXPECT_EQ(result.status, StatusCode::AFFINITY_SCHEDULE_FAILED);
}

// GetPluginName
TEST_F(NUMAAffinityFilterTest, GetPluginName) {
    NUMAAffinityFilter filter;
    EXPECT_EQ(filter.GetPluginName(), "NUMAAffinityFilter");
}

}  // namespace functionsystem::test
