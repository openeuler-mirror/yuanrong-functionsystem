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

#include "common/utils/numa_binding.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <numa.h>

#include "common/utils/numa_utils.h"

namespace functionsystem::test {
using namespace ::testing;
using namespace functionsystem::utils;

class NUMABindingTest : public Test {};

// BindCPUToNUMANodes: 空节点列表返回 PARAMETER_ERROR
TEST_F(NUMABindingTest, BindCPUToNUMANodesEmptyList) {
    auto status = NUMABinding::BindCPUToNUMANodes({});
    EXPECT_FALSE(status.IsOk());
    EXPECT_EQ(status.StatusCode(), StatusCode::PARAMETER_ERROR);
}

// BindMemoryToNUMANodes: 空节点列表返回 PARAMETER_ERROR
TEST_F(NUMABindingTest, BindMemoryToNUMANodesEmptyList) {
    auto status = NUMABinding::BindMemoryToNUMANodes({});
    EXPECT_FALSE(status.IsOk());
    EXPECT_EQ(status.StatusCode(), StatusCode::PARAMETER_ERROR);
}

// BindToNUMANodes: 空节点列表返回 PARAMETER_ERROR
TEST_F(NUMABindingTest, BindToNUMANodesEmptyList) {
    auto status = NUMABinding::BindToNUMANodes({});
    EXPECT_FALSE(status.IsOk());
    EXPECT_EQ(status.StatusCode(), StatusCode::PARAMETER_ERROR);
}

// BindCPUToNUMANode: 无效 nodeId (-1) 返回 PARAMETER_ERROR
TEST_F(NUMABindingTest, BindCPUToNUMANodeInvalidNodeId) {
    auto status = NUMABinding::BindCPUToNUMANode(-1);
    EXPECT_FALSE(status.IsOk());
    EXPECT_EQ(status.StatusCode(), StatusCode::PARAMETER_ERROR);
}

// BindCPUToNUMANode: 无效 nodeId (超出范围) 返回 PARAMETER_ERROR
TEST_F(NUMABindingTest, BindCPUToNUMANodeNodeIdOutOfRange) {
    int invalidNodeId = 99999;
    if (NUMAUtils::IsNUMAAvailable() && numa_max_node() >= 0) {
        invalidNodeId = numa_max_node() + 10;
    }
    auto status = NUMABinding::BindCPUToNUMANode(invalidNodeId);
    EXPECT_FALSE(status.IsOk());
    EXPECT_EQ(status.StatusCode(), StatusCode::PARAMETER_ERROR);
}

// BindMemoryToNUMANode: 无效 nodeId 返回错误（通过 CreateNodeMask 失败）
TEST_F(NUMABindingTest, BindMemoryToNUMANodeInvalidNodeId) {
    auto status = NUMABinding::BindMemoryToNUMANode(-1);
    EXPECT_FALSE(status.IsOk());
}

// BindMemoryToNUMANodes: 空列表返回 PARAMETER_ERROR
TEST_F(NUMABindingTest, BindMemoryToNUMANodesEmptyReturnsError) {
    auto status = NUMABinding::BindMemoryToNUMANodes({});
    EXPECT_FALSE(status.IsOk());
}

// VerifyBinding: 非 NUMA 系统返回 OK
TEST_F(NUMABindingTest, VerifyBindingNonNUMASystem) {
    if (!NUMAUtils::IsNUMAAvailable()) {
        auto status = NUMABinding::VerifyBinding(0);
        EXPECT_TRUE(status.IsOk());
    }
}

// GetCurrentCPUBinding: 不崩溃
TEST_F(NUMABindingTest, GetCurrentCPUBindingDoesNotCrash) {
    int node = NUMABinding::GetCurrentCPUBinding();
    (void)node;
}

// GetCurrentMemoryBinding: 不崩溃
TEST_F(NUMABindingTest, GetCurrentMemoryBindingDoesNotCrash) {
    int node = NUMABinding::GetCurrentMemoryBinding();
    (void)node;
}

// NUMA 不可用时 BindCPUToNUMANode 返回 PARAMETER_ERROR
TEST_F(NUMABindingTest, BindCPUToNUMANodeWhenNUMANotAvailable) {
    if (!NUMAUtils::IsNUMAAvailable()) {
        auto status = NUMABinding::BindCPUToNUMANode(0);
        EXPECT_FALSE(status.IsOk());
        EXPECT_EQ(status.StatusCode(), StatusCode::PARAMETER_ERROR);
    }
}

// NUMA 不可用时 BindCPUToNUMANodes 返回 PARAMETER_ERROR
TEST_F(NUMABindingTest, BindCPUToNUMANodesWhenNUMANotAvailable) {
    if (!NUMAUtils::IsNUMAAvailable()) {
        auto status = NUMABinding::BindCPUToNUMANodes({ 0, 1 });
        EXPECT_FALSE(status.IsOk());
        EXPECT_EQ(status.StatusCode(), StatusCode::PARAMETER_ERROR);
    }
}

// NUMA 不可用时 BindMemoryToNUMANode 返回 PARAMETER_ERROR
TEST_F(NUMABindingTest, BindMemoryToNUMANodeWhenNUMANotAvailable) {
    if (!NUMAUtils::IsNUMAAvailable()) {
        auto status = NUMABinding::BindMemoryToNUMANode(0);
        EXPECT_FALSE(status.IsOk());
        EXPECT_EQ(status.StatusCode(), StatusCode::PARAMETER_ERROR);
    }
}

// NUMA 不可用时 BindMemoryToNUMANodes 返回 PARAMETER_ERROR
TEST_F(NUMABindingTest, BindMemoryToNUMANodesWhenNUMANotAvailable) {
    if (!NUMAUtils::IsNUMAAvailable()) {
        auto status = NUMABinding::BindMemoryToNUMANodes({ 0, 1 });
        EXPECT_FALSE(status.IsOk());
        EXPECT_EQ(status.StatusCode(), StatusCode::PARAMETER_ERROR);
    }
}

// NUMA 不可用时 BindToNUMANode 返回 PARAMETER_ERROR
TEST_F(NUMABindingTest, BindToNUMANodeWhenNUMANotAvailable) {
    if (!NUMAUtils::IsNUMAAvailable()) {
        auto status = NUMABinding::BindToNUMANode(0);
        EXPECT_FALSE(status.IsOk());
        EXPECT_EQ(status.StatusCode(), StatusCode::PARAMETER_ERROR);
    }
}

// NUMA 不可用时 BindToNUMANodes 空列表优先返回 PARAMETER_ERROR（在检查 NUMA 之前）
TEST_F(NUMABindingTest, BindToNUMANodesEmptyBeforeNUMACheck) {
    auto status = NUMABinding::BindToNUMANodes({});
    EXPECT_FALSE(status.IsOk());
    EXPECT_EQ(status.StatusCode(), StatusCode::PARAMETER_ERROR);
}

}  // namespace functionsystem::test
