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

#ifdef ENABLE_NUMA
Bitmask* NUMABinding::CreateNodeMask(int nodeId)
{
    if (!NUMAUtils::IsNUMAAvailable()) {
        return nullptr;
    }
    if (nodeId < 0 || nodeId > FsNumaMaxNode()) {
        YRLOG_ERROR("Invalid NUMA node ID {} (valid range: 0-{})", nodeId, FsNumaMaxNode());
        return nullptr;
    }

    Bitmask* mask = FsNumaAllocateNodemask();
    if (mask == nullptr) {
        YRLOG_ERROR("Failed to allocate NUMA node mask");
        return nullptr;
    }

    FsNumaBitmaskSetbit(mask, nodeId);
    return mask;
}

Bitmask* NUMABinding::CreateNodeMask(const std::vector<int>& nodeIds)
{
    if (!NUMAUtils::IsNUMAAvailable()) {
        return nullptr;
    }

    Bitmask* mask = FsNumaAllocateNodemask();
    if (mask == nullptr) {
        YRLOG_ERROR("Failed to allocate NUMA node mask");
        return nullptr;
    }

    int maxNode = FsNumaMaxNode();
    for (int nodeId : nodeIds) {
        if (nodeId >= 0 && nodeId <= maxNode) {
            FsNumaBitmaskSetbit(mask, nodeId);
        } else {
            YRLOG_WARN("Skip invalid NUMA node ID {} (valid range: 0-{})", nodeId, maxNode);
        }
    }
    return mask;
}
#endif

Status NUMABinding::BindCPUToNUMANode(int nodeId)
{
#ifdef ENABLE_NUMA
    if (!NUMAUtils::IsNUMAAvailable()) {
        return Status(StatusCode::PARAMETER_ERROR, "NUMA is not available on this system");
    }
    if (nodeId < 0 || nodeId > FsNumaMaxNode()) {
        return Status(StatusCode::PARAMETER_ERROR,
                      "Invalid NUMA node ID " + std::to_string(nodeId) + " (valid range: 0-" +
                          std::to_string(FsNumaMaxNode()) + ")");
    }

    int ret = FsNumaRunOnNode(nodeId);
    if (ret < 0) {
        YRLOG_ERROR("Failed to bind CPU to NUMA node {}: {}", nodeId, litebus::os::Strerror(errno));
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "Failed to bind CPU to NUMA node");
    }
    
    YRLOG_INFO("Successfully bound CPU to NUMA node {}", nodeId);
    return Status::OK();
#else
    (void)nodeId;
    return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "NUMA support is disabled at compile time");
#endif
}

Status NUMABinding::BindCPUToNUMANodes(const std::vector<int>& nodeIds)
{
#ifdef ENABLE_NUMA
    if (!NUMAUtils::IsNUMAAvailable()) {
        return Status(StatusCode::PARAMETER_ERROR, "NUMA is not available on this system");
    }
    
    if (nodeIds.empty()) {
        return Status(StatusCode::PARAMETER_ERROR, "Empty NUMA node IDs list");
    }
    
    Bitmask* mask = CreateNodeMask(nodeIds);
    if (mask == nullptr) {
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "Failed to create NUMA node mask");
    }
    
    int ret = FsNumaRunOnNodeMask(mask);
    FsNumaBitmaskFree(mask);
    
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
#else
    (void)nodeIds;
    return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "NUMA support is disabled at compile time");
#endif
}

Status NUMABinding::BindMemoryToNUMANode(int nodeId)
{
#ifdef ENABLE_NUMA
    if (!NUMAUtils::IsNUMAAvailable()) {
        return Status(StatusCode::PARAMETER_ERROR, "NUMA is not available on this system");
    }
    
    Bitmask* mask = CreateNodeMask(nodeId);
    if (mask == nullptr) {
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "Failed to create NUMA node mask");
    }

    errno = 0;
    FsNumaSetMembind(mask);
    if (errno != 0) {
        YRLOG_ERROR("Failed to set memory binding to NUMA node {}: {}", nodeId, litebus::os::Strerror(errno));
        FsNumaBitmaskFree(mask);
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "Failed to set memory binding");
    }
    FsNumaBitmaskFree(mask);
    YRLOG_INFO("Successfully bound memory to NUMA node {}", nodeId);
    return Status::OK();
#else
    (void)nodeId;
    return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "NUMA support is disabled at compile time");
#endif
}

Status NUMABinding::BindMemoryToNUMANodes(const std::vector<int>& nodeIds)
{
#ifdef ENABLE_NUMA
    if (!NUMAUtils::IsNUMAAvailable()) {
        return Status(StatusCode::PARAMETER_ERROR, "NUMA is not available on this system");
    }
    
    if (nodeIds.empty()) {
        return Status(StatusCode::PARAMETER_ERROR, "Empty NUMA node IDs list");
    }
    
    Bitmask* mask = CreateNodeMask(nodeIds);
    if (mask == nullptr) {
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "Failed to create NUMA node mask");
    }

    errno = 0;
    FsNumaSetMembind(mask);
    if (errno != 0) {
        YRLOG_ERROR("Failed to set memory binding to NUMA nodes: {}", litebus::os::Strerror(errno));
        FsNumaBitmaskFree(mask);
        return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "Failed to set memory binding");
    }
    FsNumaBitmaskFree(mask);

    std::string nodesStr;
    for (size_t i = 0; i < nodeIds.size(); ++i) {
        if (i > 0) nodesStr += ", ";
        nodesStr += std::to_string(nodeIds[i]);
    }
    YRLOG_INFO("Successfully bound memory to NUMA nodes [{}]", nodesStr);
    return Status::OK();
#else
    (void)nodeIds;
    return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "NUMA support is disabled at compile time");
#endif
}

Status NUMABinding::BindToNUMANode(int nodeId)
{
#ifdef ENABLE_NUMA
    auto cpuStatus = BindCPUToNUMANode(nodeId);
    if (!cpuStatus.IsOk()) {
        return cpuStatus;
    }
    
    auto memStatus = BindMemoryToNUMANode(nodeId);
    if (!memStatus.IsOk()) {
        YRLOG_WARN("CPU bound to node {} but memory binding failed", nodeId);
        return memStatus;
    }
    
    return VerifyBinding(nodeId);
#else
    (void)nodeId;
    return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "NUMA support is disabled at compile time");
#endif
}

Status NUMABinding::BindToNUMANodes(const std::vector<int>& nodeIds)
{
#ifdef ENABLE_NUMA
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
#else
    (void)nodeIds;
    return Status(StatusCode::ERR_INNER_SYSTEM_ERROR, "NUMA support is disabled at compile time");
#endif
}

Status NUMABinding::VerifyBinding(int expectedNodeId)
{
#ifdef ENABLE_NUMA
    if (!NUMAUtils::IsNUMAAvailable()) {
        return Status::OK(); // 非 NUMA 系统，无需验证
    }
    
    // 验证 CPU 绑定
    Bitmask* cpuMask = FsNumaGetRunNodeMask();
    if (cpuMask != nullptr) {
        if (!FsNumaBitmaskIsbitset(cpuMask, expectedNodeId)) {
            YRLOG_WARN("CPU binding verification failed: expected node {} but got different binding",
                       expectedNodeId);
        }
        FsNumaBitmaskFree(cpuMask);
    }
    
    Bitmask* memMask = FsNumaGetMembind();
    if (memMask != nullptr) {
        if (!FsNumaBitmaskIsbitset(memMask, expectedNodeId)) {
            YRLOG_WARN("Memory binding verification failed: expected node {} but got different binding",
                       expectedNodeId);
        }
        FsNumaBitmaskFree(memMask);
    }
    
    return Status::OK();
#else
    (void)expectedNodeId;
    return Status::OK();
#endif
}

int NUMABinding::GetCurrentCPUBinding()
{
#ifdef ENABLE_NUMA
    if (!NUMAUtils::IsNUMAAvailable()) {
        return -1;
    }
    
    Bitmask* mask = FsNumaGetRunNodeMask();
    if (mask == nullptr) {
        return -1;
    }
    
    const int maxNode = FsNumaMaxNode();
    for (int i = 0; i <= maxNode; ++i) {
        if (FsNumaBitmaskIsbitset(mask, i)) {
            FsNumaBitmaskFree(mask);
            return i;
        }
    }

    FsNumaBitmaskFree(mask);
    return -1;
#else
    return -1;
#endif
}

int NUMABinding::GetCurrentMemoryBinding()
{
#ifdef ENABLE_NUMA
    if (!NUMAUtils::IsNUMAAvailable()) {
        return -1;
    }
    
    Bitmask* mask = FsNumaGetMembind();
    if (mask == nullptr) {
        return -1;
    }
    
    const int maxNode = FsNumaMaxNode();
    for (int i = 0; i <= maxNode; ++i) {
        if (FsNumaBitmaskIsbitset(mask, i)) {
            FsNumaBitmaskFree(mask);
            return i;
        }
    }

    FsNumaBitmaskFree(mask);
    return -1;
#else
    return -1;
#endif
}

} // namespace functionsystem::utils
