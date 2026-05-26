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

#include "common/utils/numa_dl.h"

#include <dlfcn.h>
#include <mutex>

#include "common/logs/logging.h"

namespace {

void* g_handle = nullptr;

std::once_flag g_initFlag;

void InitNuma()
{
    for (const char* name : {"libnuma.so.1", "libnuma.so"}) {
        g_handle = dlopen(name, RTLD_LAZY | RTLD_GLOBAL);
        if (g_handle != nullptr) {
            YRLOG_INFO("[NUMADL] dlopen succeeded: {}", name);
            return;
        }
        YRLOG_WARN("[NUMADL] dlopen failed: {}, error: {}", name, dlerror());
    }
    YRLOG_WARN("[NUMADL] libnuma not found, NUMA features will be unavailable");
}

template <typename Fn>
Fn LoadSym(const char* name)
{
    if (g_handle == nullptr) {
        return nullptr;
    }
    void* sym = dlsym(g_handle, name);
    if (sym == nullptr) {
        YRLOG_ERROR("[NUMADL] dlsym failed for {}: {}", name, dlerror());
        return nullptr;
    }
    return reinterpret_cast<Fn>(sym);
}

template <typename Ret, typename Fn, typename... Args>
Ret CallRet(const char* name, Ret fallback, Args... args)
{
    std::call_once(g_initFlag, InitNuma);
    auto fn = LoadSym<Fn>(name);
    if (fn == nullptr) {
        return fallback;
    }
    return fn(args...);
}

template <typename Fn, typename... Args>
void CallVoid(const char* name, Args... args)
{
    std::call_once(g_initFlag, InitNuma);
    auto fn = LoadSym<Fn>(name);
    if (fn == nullptr) {
        return;
    }
    fn(args...);
}

} // namespace

namespace functionsystem::utils {

int FsNumaAvailable(void)
{
    return CallRet<int, decltype(&FsNumaAvailable)>("numa_available", -1);
}

int FsNumaMaxNode(void)
{
    return CallRet<int, decltype(&FsNumaMaxNode)>("numa_max_node", -1);
}

int FsNumaNumConfiguredCpus(void)
{
    return CallRet<int, decltype(&FsNumaNumConfiguredCpus)>("numa_num_configured_cpus", 0);
}

Bitmask* FsNumaAllocateCpumask(void)
{
    return CallRet<Bitmask*, decltype(&FsNumaAllocateCpumask)>("numa_allocate_cpumask", nullptr);
}

void FsNumaFreeCpumask(Bitmask* mask)
{
    CallVoid<decltype(&FsNumaFreeCpumask)>("numa_free_cpumask", mask);
}

Bitmask* FsNumaAllocateNodemask(void)
{
    return CallRet<Bitmask*, decltype(&FsNumaAllocateNodemask)>("numa_allocate_nodemask", nullptr);
}

void FsNumaBitmaskFree(Bitmask* mask)
{
    CallVoid<decltype(&FsNumaBitmaskFree)>("numa_bitmask_free", mask);
}

Bitmask* FsNumaBitmaskSetbit(Bitmask* mask, unsigned int bit)
{
    return CallRet<Bitmask*, decltype(&FsNumaBitmaskSetbit)>("numa_bitmask_setbit", nullptr, mask, bit);
}

int FsNumaBitmaskIsbitset(const Bitmask* mask, unsigned int bit)
{
    return CallRet<int, decltype(&FsNumaBitmaskIsbitset)>("numa_bitmask_isbitset", 0, mask, bit);
}

int FsNumaNodeToCpus(int node, Bitmask* mask)
{
    return CallRet<int, decltype(&FsNumaNodeToCpus)>("numa_node_to_cpus", -1, node, mask);
}

int FsNumaRunOnNode(int node)
{
    return CallRet<int, decltype(&FsNumaRunOnNode)>("numa_run_on_node", -1, node);
}

int FsNumaRunOnNodeMask(Bitmask* mask)
{
    return CallRet<int, decltype(&FsNumaRunOnNodeMask)>("numa_run_on_node_mask", -1, mask);
}

void FsNumaSetMembind(Bitmask* mask)
{
    CallVoid<decltype(&FsNumaSetMembind)>("numa_set_membind", mask);
}

Bitmask* FsNumaGetMembind(void)
{
    return CallRet<Bitmask*, decltype(&FsNumaGetMembind)>("numa_get_membind", nullptr);
}

Bitmask* FsNumaGetRunNodeMask(void)
{
    return CallRet<Bitmask*, decltype(&FsNumaGetRunNodeMask)>("numa_get_run_node_mask", nullptr);
}

} // namespace functionsystem::utils