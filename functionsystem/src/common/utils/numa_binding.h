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

#ifndef FUNCTIONSYSTEM_NUMA_BINDING_H
#define FUNCTIONSYSTEM_NUMA_BINDING_H

#include "common/status/status.h"

#include <numa.h>

namespace functionsystem::utils {

class NUMABinding {
public:
    ~NUMABinding() = default;
    // 绑定进程到指定的 NUMA 节点（CPU + 内存）
    static Status BindToNUMANode(int nodeId);
    
    // 绑定进程到多个指定的 NUMA 节点（CPU + 内存）
    static Status BindToNUMANodes(const std::vector<int>& nodeIds);
    
    // 绑定进程的 CPU 到指定 NUMA 节点
    static Status BindCPUToNUMANode(int nodeId);
    
    // 绑定进程的 CPU 到多个指定的 NUMA 节点
    static Status BindCPUToNUMANodes(const std::vector<int>& nodeIds);
    
    // 绑定进程的内存分配到指定 NUMA 节点
    static Status BindMemoryToNUMANode(int nodeId);
    
    // 绑定进程的内存分配到多个指定的 NUMA 节点
    static Status BindMemoryToNUMANodes(const std::vector<int>& nodeIds);
    
    // 验证当前进程的 NUMA 绑定
    static Status VerifyBinding(int expectedNodeId);
    
    // 获取当前进程的 CPU 绑定节点。返回值 >=0 为节点 ID，-1 表示错误或非 NUMA 系统。
    static int GetCurrentCPUBinding();

    // 获取当前进程的内存绑定节点。返回值 >=0 为节点 ID，-1 表示错误或非 NUMA 系统。
    static int GetCurrentMemoryBinding();

private:
    static struct bitmask* CreateNodeMask(int nodeId);
    static struct bitmask* CreateNodeMask(const std::vector<int>& nodeIds);
};

} // namespace functionsystem::utils

#endif // FUNCTIONSYSTEM_NUMA_BINDING_H
