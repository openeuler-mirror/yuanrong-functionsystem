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

#include <cerrno>
#include <string>

#include "common/logs/logging.h"
#include "common/status/status.h"
#include "common/utils/numa_utils.h"
#include "utils/os_utils.hpp"

namespace functionsystem::utils {

struct bitmask* NUMABinding::CreateNodeMask(int nodeId)
{
    if (!NUMAUtils::IsNUMAAvailable()) {
        return nullptr;
    }
    if (nodeId < 0 || nodeId > numa_max_node()) {
        YRLOG_ERROR("Invalid NUMA node ID {} (valid range: 0-{})", nodeId, numa_max_node());
        return nullptr;
    }

    struct bitmask* mask = numa_allocate_nodemask();
    if (mask == nullptr) {
        YRLOG_ERROR("Failed to allocate NUMA node mask");
        return nullptr;
    }

    numa_bitmask_setbit(mask, nodeId);
    return mask;
}

struct bitmask* NUMABinding::CreateNodeMask(const std::vector<int>& nodeIds)
{
    if (!NUMAUtils::IsNUMAAvailable()) {
        return nullptr;
    }

    struct bitmask* mask = numa_allocate_nodemask();
    if (mask == nullptr) {
        YRLOG_ERROR("Failed to allocate NUMA node mask");
        return nullptr;
    }

    int maxNode = numa_max_node();
    for (int nodeId : nodeIds) {
        if (nodeId >= 0 && nodeId <= maxNode) {
            numa_bitmask_setbit(mask, nodeId);
        } else {
            YRLOG_WARN("Skip invalid NUMA node ID {} (valid range: 0-{})", nodeId, maxNode);
        }
    }
    return mask;
}

Status NUMABinding::BindCPUToNUMANode(int nodeId)
{
    if (!NUMAUtils::IsNUMAAvailable()) {
        return Status(StatusCode::PARAMETER_ERROR, "NUMA is not available on this system");
    }
    if (nodeId < 0 || nodeId > numa_max_node()) {
        return Status(StatusCode::PARAMETER_ERROR,
                      "Invalid NUMA node ID " + std::to_string(nodeId) + " (valid range: 0-" +
                          std::to_string(numa_max_node()) + ")");
    }

    int ret = numa_run_on_node(nodeId);
    if (ret < 0) {
        YRLOG_ERROR("Failed to bind CPU to NUMA node {}: {}", nodeId, litebus::os::Strerror(errno));
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "Failed to bind CPU to NUMA node");
    }
    
    YRLOG_INFO("Successfully bound CPU to NUMA node {}", nodeId);
    return Status::OK();
}

Status NUMABinding::BindCPUToNUMANodes(const std::vector<int>& nodeIds)
{
    if (!NUMAUtils::IsNUMAAvailable()) {
        return Status(StatusCode::PARAMETER_ERROR, "NUMA is not available on this system");
    }
    
    if (nodeIds.empty()) {
        return Status(StatusCode::PARAMETER_ERROR, "Empty NUMA node IDs list");
    }
    
    struct bitmask* mask = CreateNodeMask(nodeIds);
    if (mask == nullptr) {
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "Failed to create NUMA node mask");
    }
    
    int ret = numa_run_on_node_mask(mask);
    numa_bitmask_free(mask);
    
    if (ret < 0) {
        YRLOG_ERROR("Failed to bind CPU to NUMA nodes: {}", litebus::os::Strerror(errno));
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "Failed to bind CPU to NUMA nodes");
    }
    
    std::string nodesStr;
    for (size_t i = 0; i < nodeIds.size(); ++i) {
        if (i > 0) nodesStr += ", ";
        nodesStr += std::to_string(nodeIds[i]);
    }
    YRLOG_INFO("Successfully bound CPU to NUMA nodes [{}]", nodesStr);
    return Status::OK();
}

Status NUMABinding::BindMemoryToNUMANode(int nodeId)
{
    if (!NUMAUtils::IsNUMAAvailable()) {
        return Status(StatusCode::PARAMETER_ERROR, "NUMA is not available on this system");
    }
    
    struct bitmask* mask = CreateNodeMask(nodeId);
    if (mask == nullptr) {
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "Failed to create NUMA node mask");
    }

    errno = 0;
    numa_set_membind(mask);
    if (errno != 0) {
        YRLOG_ERROR("Failed to set memory binding to NUMA node {}: {}", nodeId, litebus::os::Strerror(errno));
        numa_bitmask_free(mask);
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "Failed to set memory binding");
    }
    numa_bitmask_free(mask);
    YRLOG_INFO("Successfully bound memory to NUMA node {}", nodeId);
    return Status::OK();
}

Status NUMABinding::BindMemoryToNUMANodes(const std::vector<int>& nodeIds)
{
    if (!NUMAUtils::IsNUMAAvailable()) {
        return Status(StatusCode::PARAMETER_ERROR, "NUMA is not available on this system");
    }
    
    if (nodeIds.empty()) {
        return Status(StatusCode::PARAMETER_ERROR, "Empty NUMA node IDs list");
    }
    
    struct bitmask* mask = CreateNodeMask(nodeIds);
    if (mask == nullptr) {
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "Failed to create NUMA node mask");
    }

    errno = 0;
    numa_set_membind(mask);
    if (errno != 0) {
        YRLOG_ERROR("Failed to set memory binding to NUMA nodes: {}", litebus::os::Strerror(errno));
        numa_bitmask_free(mask);
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "Failed to set memory binding");
    }
    numa_bitmask_free(mask);

    std::string nodesStr;
    for (size_t i = 0; i < nodeIds.size(); ++i) {
        if (i > 0) nodesStr += ", ";
        nodesStr += std::to_string(nodeIds[i]);
    }
    YRLOG_INFO("Successfully bound memory to NUMA nodes [{}]", nodesStr);
    return Status::OK();
}

Status NUMABinding::BindToNUMANode(int nodeId)
{
    // 先绑定 CPU
    auto cpuStatus = BindCPUToNUMANode(nodeId);
    if (!cpuStatus.IsOk()) {
        return cpuStatus;
    }
    
    // 再绑定内存
    auto memStatus = BindMemoryToNUMANode(nodeId);
    if (!memStatus.IsOk()) {
        YRLOG_WARN("CPU bound to node {} but memory binding failed", nodeId);
        return memStatus;
    }
    
    // 验证绑定
    return VerifyBinding(nodeId);
}

Status NUMABinding::BindToNUMANodes(const std::vector<int>& nodeIds)
{
    if (nodeIds.empty()) {
        return Status(StatusCode::PARAMETER_ERROR, "Empty NUMA node IDs list");
    }
    
    // 先绑定 CPU
    auto cpuStatus = BindCPUToNUMANodes(nodeIds);
    if (!cpuStatus.IsOk()) {
        return cpuStatus;
    }
    
    // 再绑定内存
    auto memStatus = BindMemoryToNUMANodes(nodeIds);
    if (!memStatus.IsOk()) {
        YRLOG_WARN("CPU bound to nodes but memory binding failed");
        return memStatus;
    }
    
    return Status::OK();
}

Status NUMABinding::VerifyBinding(int expectedNodeId)
{
    if (!NUMAUtils::IsNUMAAvailable()) {
        return Status::OK(); // 非 NUMA 系统，无需验证
    }
    
    // 验证 CPU 绑定
    struct bitmask* cpuMask = numa_get_run_node_mask();
    if (cpuMask != nullptr) {
        if (!numa_bitmask_isbitset(cpuMask, expectedNodeId)) {
            YRLOG_WARN("CPU binding verification failed: expected node {} but got different binding",
                       expectedNodeId);
            // 不返回错误，因为可能有多个节点
        }
        numa_bitmask_free(cpuMask);
    }
    
    // 验证内存绑定
    struct bitmask* memMask = numa_get_membind();
    if (memMask != nullptr) {
        if (!numa_bitmask_isbitset(memMask, expectedNodeId)) {
            YRLOG_WARN("Memory binding verification failed: expected node {} but got different binding",
                       expectedNodeId);
            // 不返回错误，因为可能有多个节点
        }
        numa_bitmask_free(memMask);
    }
    
    return Status::OK();
}

int NUMABinding::GetCurrentCPUBinding()
{
    if (!NUMAUtils::IsNUMAAvailable()) {
        return -1;
    }
    
    struct bitmask* mask = numa_get_run_node_mask();
    if (mask == nullptr) {
        return -1;
    }
    
    // 返回第一个设置的节点
    const int maxNode = numa_max_node();
    for (int i = 0; i <= maxNode; ++i) {
        if (numa_bitmask_isbitset(mask, i)) {
            numa_bitmask_free(mask);
            return i;
        }
    }

    numa_bitmask_free(mask);
    return -1;
}

int NUMABinding::GetCurrentMemoryBinding()
{
    if (!NUMAUtils::IsNUMAAvailable()) {
        return -1;
    }
    
    struct bitmask* mask = numa_get_membind();
    if (mask == nullptr) {
        return -1;
    }
    
    // 返回第一个设置的节点
    const int maxNode = numa_max_node();
    for (int i = 0; i <= maxNode; ++i) {
        if (numa_bitmask_isbitset(mask, i)) {
            numa_bitmask_free(mask);
            return i;
        }
    }

    numa_bitmask_free(mask);
    return -1;
}

} // namespace functionsystem::utils
