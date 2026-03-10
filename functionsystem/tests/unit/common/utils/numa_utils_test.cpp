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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "common/resource_view/view_utils.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace functionsystem::utils;

const std::string DEFAULT_NUMA_NODE_ID = "test_numa_node";

class NUMAUtilsTest : public Test {};

// GetNUMANodeCPUsFromAllocatable: 从 allocatable Resources 解析 NUMA CPU
TEST_F(NUMAUtilsTest, GetNUMANodeCPUsFromAllocatable) {
    resource_view::Resources allocatable = view_utils::GetNUMAResources({ 1000.0, 2000.0, 500.0 }, DEFAULT_NUMA_NODE_ID);

    auto result = NUMAUtils::GetNUMANodeCPUsFromAllocatable(allocatable, DEFAULT_NUMA_NODE_ID);

    EXPECT_EQ(result.size(), size_t{3});
    EXPECT_DOUBLE_EQ(result[0], 1000.0);
    EXPECT_DOUBLE_EQ(result[1], 2000.0);
    EXPECT_DOUBLE_EQ(result[2], 500.0);
}

// GetNUMANodeCPUsFromAllocatable: nodeID 不匹配时使用第一个 vector
TEST_F(NUMAUtilsTest, GetNUMANodeCPUsFromAllocatableFallbackToFirst) {
    resource_view::Resources allocatable = view_utils::GetNUMAResources({ 800.0, 1200.0 }, DEFAULT_NUMA_NODE_ID);

    auto result = NUMAUtils::GetNUMANodeCPUsFromAllocatable(allocatable, "non_existent_node_id");

    EXPECT_EQ(result.size(), size_t{2});
    EXPECT_DOUBLE_EQ(result[0], 800.0);
    EXPECT_DOUBLE_EQ(result[1], 1200.0);
}

// GetNUMANodeCPUsFromAllocatable: 无 NUMA 资源时返回空
TEST_F(NUMAUtilsTest, GetNUMANodeCPUsFromAllocatableEmptyWhenNoNUMA) {
    resource_view::Resources allocatable = view_utils::GetCpuMemResources();

    auto result = NUMAUtils::GetNUMANodeCPUsFromAllocatable(allocatable, DEFAULT_NUMA_NODE_ID);

    EXPECT_TRUE(result.empty());
}

// GetNUMANodeCPUsFromResourceUnit: 从 ResourceUnit 解析 NUMA CPU
TEST_F(NUMAUtilsTest, GetNUMANodeCPUsFromResourceUnit) {
    auto unit = view_utils::Get1DResourceUnitWithNUMA({ 500.0, 1500.0, 300.0 }, 2300.0, 4096.0, DEFAULT_NUMA_NODE_ID);
    unit.set_id(DEFAULT_NUMA_NODE_ID);

    auto result = NUMAUtils::GetNUMANodeCPUsFromResourceUnit(unit);

    EXPECT_EQ(result.size(), size_t{3});
    EXPECT_DOUBLE_EQ(result[0], 500.0);
    EXPECT_DOUBLE_EQ(result[1], 1500.0);
    EXPECT_DOUBLE_EQ(result[2], 300.0);
}

// GetNUMANodeCPUsFromResourceUnit: ResourceUnit 无 NUMA 时返回空
TEST_F(NUMAUtilsTest, GetNUMANodeCPUsFromResourceUnitEmptyWhenNoNUMA) {
    auto unit = view_utils::Get1DResourceUnit("unit_without_numa");

    auto result = NUMAUtils::GetNUMANodeCPUsFromResourceUnit(unit);

    EXPECT_TRUE(result.empty());
}

// IsNUMAAvailable: 不崩溃（依赖运行环境）
TEST_F(NUMAUtilsTest, IsNUMAAvailableDoesNotCrash) {
    bool available = NUMAUtils::IsNUMAAvailable();
    (void)available;
}

// GetNUMANodeCount: 非 NUMA 系统返回 1，NUMA 系统返回节点数
TEST_F(NUMAUtilsTest, GetNUMANodeCount) {
    int count = NUMAUtils::GetNUMANodeCount();
    EXPECT_GE(count, 1);
}

}  // namespace functionsystem::test
