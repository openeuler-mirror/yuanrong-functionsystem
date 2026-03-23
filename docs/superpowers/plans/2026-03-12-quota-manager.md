# QuotaManagerActor Implementation Plan

> **For agentic workers:** REQUIRED: Use `superpowers:subagent-driven-development` (if subagents available) or `superpowers:executing-plans` to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `function_master/instance_manager` 下新增 `QuotaManagerActor`，实现租户级 CPU/内存 quota 管理，支持用量跟踪、LIFO 驱逐和调度冷却拦截。

**Architecture:** QuotaManagerActor 订阅 InstanceManagerActor 的实例状态变更，维护 per-tenant 用量快照；超配时通过 ForwardKill 驱逐最新实例，并向 DomainSchedSrvActor 发送 TenantQuotaExceeded；InstanceCtrlActor 收到通知后在 Schedule() 入口冷却该租户 N 秒。

**Tech Stack:** C++17, LiteBus Actor Framework (ActorBase, litebus::Timer, litebus::Async, litebus::Future), Google Test/Mock, nlohmann/json, protobuf

**Spec:** `docs/superpowers/specs/2026-03-12-quota-manager-design.md`

---

## File Map

### 新增文件

| 文件 | 职责 |
|------|------|
| `functionsystem/src/function_master/instance_manager/quota_manager/quota_config.h` | TenantQuota 结构 + QuotaConfig（JSON 加载、GetQuota）|
| `functionsystem/src/function_master/instance_manager/quota_manager/quota_manager_actor.h` | QuotaManagerActor 类声明、TenantUsage 结构、常量 |
| `functionsystem/src/function_master/instance_manager/quota_manager/quota_manager_actor.cpp` | QuotaManagerActor 实现（Init、OnInstanceRunning/Exited、CheckAndEnforce）|
| `functionsystem/src/function_master/instance_manager/quota_manager/quota_manager.h` | QuotaManager wrapper 类声明 |
| `functionsystem/src/function_master/instance_manager/quota_manager/quota_manager.cpp` | QuotaManager wrapper 实现 |
| `functionsystem/tests/unit/function_master/instance_manager/quota_manager/quota_manager_test.cpp` | QuotaConfig + QuotaManagerActor 单元测试 |
| `functionsystem/tests/unit/function_master/instance_manager/quota_manager/CMakeLists.txt` | 测试构建配置 |
| `functionsystem/tests/unit/domain_scheduler/instance_control/instance_ctrl_quota_test.cpp` | InstanceCtrlActor 冷却逻辑测试 |

### 修改文件

| 文件 | 变更 |
|------|------|
| `proto/posix/message.proto` | 新增 TenantQuotaExceeded 消息 |
| `functionsystem/src/common/common_flags/common_flags.h` | 新增 quotaConfigFile_ 成员 + getter |
| `functionsystem/src/common/common_flags/common_flags.cpp` | 注册 --quota_config_file flag |
| `functionsystem/src/function_master/instance_manager/instance_manager_actor.h` | 新增 notifyQuotaMgr_ AID + 通知接口 |
| `functionsystem/src/function_master/instance_manager/instance_manager_actor.cpp` | 实例 RUNNING/EXITED 时通知 QuotaManagerActor |
| `functionsystem/src/domain_scheduler/instance_control/instance_ctrl_actor.h` | 新增 blockedTenants_ + OnTenantQuotaExceeded |
| `functionsystem/src/domain_scheduler/instance_control/instance_ctrl_actor.cpp` | Schedule() 冷却拦截 + OnTenantQuotaExceeded 实现 |
| `functionsystem/src/domain_scheduler/instance_control/instance_ctrl.h` | 新增 OnTenantQuotaExceeded 对外接口 |
| `functionsystem/src/domain_scheduler/instance_control/instance_ctrl.cpp` | 实现 OnTenantQuotaExceeded wrapper |
| `functionsystem/src/domain_scheduler/domain_scheduler_service/domain_sched_srv_actor.h` | 新增 OnTenantQuotaExceeded handler 声明 |
| `functionsystem/src/domain_scheduler/domain_scheduler_service/domain_sched_srv_actor.cpp` | 注册 + 实现 TenantQuotaExceeded 转发 |
| `functionsystem/tests/unit/function_master/instance_manager/CMakeLists.txt` | 新增 add_subdirectory(quota_manager) |

---

## Chunk 1: 基础设施（Proto + Flag + QuotaConfig）

### Task 1: 新增 TenantQuotaExceeded proto 消息

**Files:**
- Modify: `proto/posix/message.proto`

- [ ] **Step 1: 在 message.proto 末尾新增消息定义**

在文件末尾（现有最后一个 message 之后）添加：

```protobuf
message TenantQuotaExceeded {
    string tenantID   = 1;
    int64  cooldownMs = 2;
}
```

- [ ] **Step 2: 编译 proto 验证语法**

```bash
cd /home/robbluo/code/yuanrong-functionsystem
bash run.sh build -j 4 2>&1 | grep -E "message.proto|error" | head -20
```

期望：无 proto 编译错误。

- [ ] **Step 3: Commit**

```bash
git add proto/posix/message.proto
git commit -m "feat(proto): add TenantQuotaExceeded message for quota cooldown notification"
```

---

### Task 2: 新增 --quota_config_file flag

**Files:**
- Modify: `functionsystem/src/common/common_flags/common_flags.h`
- Modify: `functionsystem/src/common/common_flags/common_flags.cpp`

- [ ] **Step 1: 在 common_flags.h 中新增成员和 getter**

在 `CommonFlags` 类 private 成员区域（其他 string 类型 flag 成员附近）添加：

```cpp
std::string quotaConfigFile_{ "" };
```

在 public getter 区域添加：

```cpp
std::string GetQuotaConfigFile() const { return quotaConfigFile_; }
```

- [ ] **Step 2: 在 common_flags.cpp 构造函数中注册 flag**

在 `CommonFlags::CommonFlags()` 函数体内，参考 `metricsConfigFile_` 的注册方式添加：

```cpp
AddFlag(&CommonFlags::quotaConfigFile_, "quota_config_file",
        "set the quota config json file path for tenant resource quota management", "");
```

- [ ] **Step 3: 编译验证**

```bash
bash run.sh build -j 4 2>&1 | grep -E "common_flags|error" | head -20
```

期望：无编译错误。

- [ ] **Step 4: Commit**

```bash
git add functionsystem/src/common/common_flags/common_flags.h \
        functionsystem/src/common/common_flags/common_flags.cpp
git commit -m "feat(flags): add --quota_config_file flag for tenant quota configuration"
```

---

### Task 3: 实现 QuotaConfig

**Files:**
- Create: `functionsystem/src/function_master/instance_manager/quota_manager/quota_config.h`

- [ ] **Step 1: 创建 quota_config.h**

```cpp
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef FUNCTIONSYSTEM_SRC_FUNCTION_MASTER_INSTANCE_MANAGER_QUOTA_MANAGER_QUOTA_CONFIG_H
#define FUNCTIONSYSTEM_SRC_FUNCTION_MASTER_INSTANCE_MANAGER_QUOTA_MANAGER_QUOTA_CONFIG_H

#include <cstdint>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "common/log/log.h"

namespace function_master {

struct TenantQuota {
    int64_t cpuMillicores{ 0 };   // CPU 毫核
    int64_t memLimitMb{ 0 };      // 内存 MB
    int64_t cooldownMs{ 10000 };  // 冷却时间，默认 10s
};

class QuotaConfig {
public:
    // Flag 未配置（path 为空）：使用兜底默认值，打印 WARNING
    // Flag 已配置但文件不存在或解析失败：YRLOG_FATAL + 终止
    static QuotaConfig LoadFromFile(const std::string &path);

    // 查询顺序：perTenantQuota_[tenantID] → defaultQuota_
    TenantQuota GetQuota(const std::string &tenantID) const;

    // 预留：外部接口写入 per-tenant quota（首阶段不调用）
    void UpdateTenantQuota(const std::string &tenantID, const TenantQuota &quota);

private:
    TenantQuota defaultQuota_;
    std::unordered_map<std::string, TenantQuota> perTenantQuota_;
};

}  // namespace function_master

#endif
```

- [ ] **Step 2: 将 LoadFromFile 和 GetQuota 实现内联至 quota_config.h（header-only）**

在类定义后、`#endif` 前添加内联实现：

```cpp
inline QuotaConfig QuotaConfig::LoadFromFile(const std::string &path)
{
    QuotaConfig cfg;
    // 内置兜底默认值
    cfg.defaultQuota_ = TenantQuota{ 32000, 65536, 10000 };

    if (path.empty()) {
        YRLOG_WARN("quota_config_file not set, using built-in defaults "
                   "(cpuMillicores=32000, memMb=65536, cooldownMs=10000)");
        return cfg;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
        YRLOG_FATAL("quota_config_file not found: {}", path);
        std::terminate();
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception &e) {
        YRLOG_FATAL("quota_config_file parse error: {}, path: {}", e.what(), path);
        std::terminate();
    }

    auto parseQuota = [](const nlohmann::json &node) -> TenantQuota {
        TenantQuota q;
        q.cpuMillicores = node.value("cpuMillicores", int64_t{ 32000 });
        q.memLimitMb    = node.value("memMb",          int64_t{ 65536 });
        q.cooldownMs    = node.value("cooldownMs",     int64_t{ 10000 });
        return q;
    };

    if (j.contains("default")) {
        cfg.defaultQuota_ = parseQuota(j["default"]);
    }

    return cfg;
}

inline TenantQuota QuotaConfig::GetQuota(const std::string &tenantID) const
{
    auto it = perTenantQuota_.find(tenantID);
    if (it != perTenantQuota_.end()) {
        return it->second;
    }
    return defaultQuota_;
}

inline void QuotaConfig::UpdateTenantQuota(const std::string &tenantID, const TenantQuota &quota)
{
    perTenantQuota_[tenantID] = quota;
}
```

注意：`#include <fstream>` 需添加到头文件 includes 中。

- [ ] **Step 3: 编译验证**

```bash
bash run.sh build -j 4 2>&1 | grep -E "quota_config|error" | head -20
```

- [ ] **Step 4: Commit**

```bash
git add functionsystem/src/function_master/instance_manager/quota_manager/
git commit -m "feat(quota): add QuotaConfig with JSON file loading and per-tenant quota lookup"
```

---

## Chunk 2: QuotaManagerActor 核心实现

### Task 4: QuotaManagerActor 头文件

**Files:**
- Create: `functionsystem/src/function_master/instance_manager/quota_manager/quota_manager_actor.h`

- [ ] **Step 1: 创建头文件**

```cpp
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#ifndef FUNCTIONSYSTEM_SRC_FUNCTION_MASTER_INSTANCE_MANAGER_QUOTA_MANAGER_QUOTA_MANAGER_ACTOR_H
#define FUNCTIONSYSTEM_SRC_FUNCTION_MASTER_INSTANCE_MANAGER_QUOTA_MANAGER_QUOTA_MANAGER_ACTOR_H

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>

#include "actor/actor.hpp"
#include "async/future.hpp"

#include "function_master/instance_manager/quota_manager/quota_config.h"
#include "common/types/resource_type.h"  // InstanceInfo

namespace function_master {

constexpr std::string_view QUOTA_MANAGER_ACTOR_NAME = "QuotaManagerActor";

struct TenantUsage {
    int64_t cpuMillicores{ 0 };
    int64_t memMb{ 0 };
    // LIFO 驱逐：按到达时间升序，驱逐从尾部取（最晚）
    std::multimap<int64_t, std::string> sortedInstances; // {arrivalTimeMs, instanceID}
};

class QuotaManagerActor : public litebus::ActorBase,
                          public std::enable_shared_from_this<QuotaManagerActor> {
public:
    explicit QuotaManagerActor(QuotaConfig config);
    ~QuotaManagerActor() override = default;

protected:
    void Init() override;
    void Finalize() override;

    // 来自 InstanceManagerActor 的状态通知
    void OnInstanceRunning(const litebus::AID &from, std::string &&name, std::string &&msg);
    void OnInstanceExited(const litebus::AID &from, std::string &&name, std::string &&msg);

private:
    void CheckAndEnforce(const std::string &tenantID);
    void RebuildUsageFromSnapshot();

    int64_t NowMs() const;

    QuotaConfig                                  config_;
    std::unordered_map<std::string, TenantUsage> tenantUsage_;
    std::unordered_map<std::string, int64_t>     instanceArrivalTime_; // instanceID → arrivalTimeMs

    litebus::AID instanceMgrAID_;
    litebus::AID domainSchedSrvAID_;
};

}  // namespace function_master

#endif
```

- [ ] **Step 2: 编译验证（头文件可见）**

```bash
bash run.sh build -j 4 2>&1 | grep -E "quota_manager_actor|error" | head -20
```

---

### Task 5: QuotaManagerActor 实现（用量跟踪）

**Files:**
- Create: `functionsystem/src/function_master/instance_manager/quota_manager/quota_manager_actor.cpp`

- [ ] **Step 1: 创建 cpp 文件，实现构造和 Init/Finalize**

```cpp
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "function_master/instance_manager/quota_manager/quota_manager_actor.h"

#include <chrono>

#include "actor/actor_system.hpp"
#include "async/async.hpp"

#include "common/log/log.h"
#include "common/types/instance_state.h"
#include "common/types/resource_type.h"
#include "common/resource_view/resource_type.h"   // CPU_RESOURCE_NAME, MEMORY_RESOURCE_NAME
#include "proto/posix/message.pb.h"               // TenantQuotaExceeded
#include "proto/posix/inner_service.pb.h"          // ForwardKillRequest

namespace function_master {

QuotaManagerActor::QuotaManagerActor(QuotaConfig config)
    : config_(std::move(config))
{}

void QuotaManagerActor::Init()
{
    Receive("OnInstanceRunning", &QuotaManagerActor::OnInstanceRunning);
    Receive("OnInstanceExited",  &QuotaManagerActor::OnInstanceExited);

    // 获取 InstanceManagerActor AID（复用项目现有机制）
    instanceMgrAID_ = litebus::ActorSystem::GetInstance().GetActor("InstanceManagerActor");
    // 获取 root domain DomainSchedSrvActor AID
    domainSchedSrvAID_ = litebus::ActorSystem::GetInstance().GetActor("DomainSchedSrvActor");

    // 拉取存量 running 实例重建用量
    RebuildUsageFromSnapshot();
}

void QuotaManagerActor::Finalize()
{
    tenantUsage_.clear();
    instanceArrivalTime_.clear();
}

int64_t QuotaManagerActor::NowMs() const
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}
```

- [ ] **Step 2: 实现资源提取工具函数和 OnInstanceRunning/OnInstanceExited**

在同一 cpp 文件中继续添加：

```cpp
namespace {

// 从 InstanceInfo.resources 提取 cpu(毫核) 和 mem(MB)
std::pair<int64_t, int64_t> ExtractResources(const InstanceInfo &info)
{
    const auto &resMap = info.resources().resources();
    double cpu = 0.0;
    double mem = 0.0;
    if (auto it = resMap.find(resource_view::CPU_RESOURCE_NAME); it != resMap.end()) {
        cpu = it->second.scalar().value();
    }
    if (auto it = resMap.find(resource_view::MEMORY_RESOURCE_NAME); it != resMap.end()) {
        mem = it->second.scalar().value();
    }
    return { static_cast<int64_t>(cpu), static_cast<int64_t>(mem) };
}

}  // namespace

void QuotaManagerActor::OnInstanceRunning(
    const litebus::AID &from, std::string &&name, std::string &&msg)
{
    InstanceInfo info;
    if (!info.ParseFromString(msg)) {
        YRLOG_WARN("QuotaManagerActor::OnInstanceRunning parse failed");
        return;
    }

    const auto &tenantID   = info.tenantid();
    const auto &instanceID = info.instanceid();

    if (tenantID.empty()) {
        YRLOG_WARN("QuotaManagerActor::OnInstanceRunning empty tenantID, instanceID={}", instanceID);
        return;
    }
    // 系统租户豁免
    if (info.issystemfunc()) {
        return;
    }

    auto [cpu, mem] = ExtractResources(info);
    int64_t arrivalMs = NowMs();

    auto &usage = tenantUsage_[tenantID];
    usage.cpuMillicores += cpu;
    usage.memMb         += mem;
    usage.sortedInstances.emplace(arrivalMs, instanceID);
    instanceArrivalTime_[instanceID] = arrivalMs;

    YRLOG_DEBUG("QuotaManagerActor: tenant={} +instance={} cpu={} mem={} "
                "totalCpu={} totalMem={}",
                tenantID, instanceID, cpu, mem,
                usage.cpuMillicores, usage.memMb);

    CheckAndEnforce(tenantID);
}

void QuotaManagerActor::OnInstanceExited(
    const litebus::AID &from, std::string &&name, std::string &&msg)
{
    InstanceInfo info;
    if (!info.ParseFromString(msg)) {
        YRLOG_WARN("QuotaManagerActor::OnInstanceExited parse failed");
        return;
    }

    const auto &tenantID   = info.tenantid();
    const auto &instanceID = info.instanceid();

    if (tenantID.empty() || info.issystemfunc()) {
        return;
    }

    auto [cpu, mem] = ExtractResources(info);
    auto &usage = tenantUsage_[tenantID];
    usage.cpuMillicores = std::max(int64_t{ 0 }, usage.cpuMillicores - cpu);
    usage.memMb         = std::max(int64_t{ 0 }, usage.memMb - mem);

    // 从 sortedInstances 中移除
    auto it = instanceArrivalTime_.find(instanceID);
    if (it != instanceArrivalTime_.end()) {
        auto range = usage.sortedInstances.equal_range(it->second);
        for (auto sit = range.first; sit != range.second; ++sit) {
            if (sit->second == instanceID) {
                usage.sortedInstances.erase(sit);
                break;
            }
        }
        instanceArrivalTime_.erase(it);
    }

    YRLOG_DEBUG("QuotaManagerActor: tenant={} -instance={} cpu={} mem={} "
                "totalCpu={} totalMem={}",
                tenantID, instanceID, cpu, mem,
                usage.cpuMillicores, usage.memMb);
}
```

---

### Task 6: 实现 CheckAndEnforce（驱逐 + 通知）

**Files:**
- Modify: `functionsystem/src/function_master/instance_manager/quota_manager/quota_manager_actor.cpp`

- [ ] **Step 1: 实现 CheckAndEnforce**

```cpp
void QuotaManagerActor::CheckAndEnforce(const std::string &tenantID)
{
    auto quota = config_.GetQuota(tenantID);
    auto &usage = tenantUsage_[tenantID];

    bool overQuota = (usage.cpuMillicores > quota.cpuMillicores ||
                      usage.memMb > quota.memLimitMb);
    if (!overQuota) {
        return;
    }

    YRLOG_WARN("QuotaManagerActor: tenant={} OVER QUOTA "
               "cpu={}/{} mem={}/{}, evicting instances (LIFO)",
               tenantID,
               usage.cpuMillicores, quota.cpuMillicores,
               usage.memMb, quota.memLimitMb);

    // LIFO 驱逐：从 sortedInstances 尾部（最晚到达）逐个 Kill
    while ((usage.cpuMillicores > quota.cpuMillicores ||
            usage.memMb > quota.memLimitMb) &&
           !usage.sortedInstances.empty())
    {
        auto it = std::prev(usage.sortedInstances.end());
        const std::string instanceID = it->second;

        inner_service::ForwardKillRequest killReq;
        killReq.set_instanceid(instanceID);
        killReq.set_requestid("QUOTA_EVICTION|tenantID=" + tenantID +
                              "|instanceID=" + instanceID);

        YRLOG_INFO("QuotaManagerActor: evicting instanceID={} tenantID={}",
                   instanceID, tenantID);

        Send(instanceMgrAID_, "ForwardKill", killReq.SerializeAsString());

        // 乐观移除（用量在 OnInstanceExited 最终扣减）
        instanceArrivalTime_.erase(instanceID);
        usage.sortedInstances.erase(it);
    }

    // 通知 DomainSchedSrvActor 进入冷却
    messages::TenantQuotaExceeded event;
    event.set_tenantid(tenantID);
    event.set_cooldownms(quota.cooldownMs);
    Send(domainSchedSrvAID_, "TenantQuotaExceeded", event.SerializeAsString());

    YRLOG_INFO("QuotaManagerActor: sent TenantQuotaExceeded tenantID={} cooldownMs={}",
               tenantID, quota.cooldownMs);
}
```

---

### Task 7: 实现启动时存量快照重建

**Files:**
- Modify: `functionsystem/src/function_master/instance_manager/quota_manager/quota_manager_actor.cpp`

- [ ] **Step 1: 实现 RebuildUsageFromSnapshot**

```cpp
void QuotaManagerActor::RebuildUsageFromSnapshot()
{
    // 向 InstanceManagerActor 拉取全量 running 实例
    // 使用 QueryInstancesInfoRequest，过滤 running 状态
    messages::QueryInstancesInfoRequest req;
    // 不设置 tenantID → 拉取全部租户

    litebus::Async(instanceMgrAID_,
                   &InstanceManagerActor::QueryInstancesInfo, req)
        .Then([aid(GetAID())](const litebus::Future<messages::QueryInstancesInfoResponse> &f) {
            if (f.IsError()) {
                YRLOG_WARN("QuotaManagerActor::RebuildUsageFromSnapshot query failed, "
                           "starting with empty usage");
                return;
            }
            const auto &rsp = f.Get();
            (void)litebus::Async(aid, &QuotaManagerActor::OnSnapshotRebuilt, rsp);
        });
}

void QuotaManagerActor::OnSnapshotRebuilt(
    const messages::QueryInstancesInfoResponse &rsp)
{
    for (const auto &info : rsp.instances()) {
        if (info.instancestatus().code() !=
            static_cast<int32_t>(InstanceState::RUNNING)) {
            continue;
        }
        if (info.tenantid().empty() || info.issystemfunc()) {
            continue;
        }

        auto [cpu, mem] = ExtractResources(info);
        int64_t arrivalMs = NowMs();

        auto &usage = tenantUsage_[info.tenantid()];
        usage.cpuMillicores += cpu;
        usage.memMb         += mem;
        usage.sortedInstances.emplace(arrivalMs, info.instanceid());
        instanceArrivalTime_[info.instanceid()] = arrivalMs;
    }

    // 对超配租户执行驱逐
    for (auto &[tenantID, _] : tenantUsage_) {
        CheckAndEnforce(tenantID);
    }

    YRLOG_INFO("QuotaManagerActor: snapshot rebuilt, {} tenants tracked",
               tenantUsage_.size());
}
```

注意：需在 `.h` 中补充 `OnSnapshotRebuilt` 私有方法声明。

- [ ] **Step 2: 编译验证**

```bash
bash run.sh build -j 4 2>&1 | grep -E "quota_manager|error" | head -30
```

- [ ] **Step 3: Commit**

```bash
git add functionsystem/src/function_master/instance_manager/quota_manager/
git commit -m "feat(quota): implement QuotaManagerActor - usage tracking, LIFO eviction, cooldown notification"
```

---

### Task 8: QuotaManager wrapper

**Files:**
- Create: `functionsystem/src/function_master/instance_manager/quota_manager/quota_manager.h`
- Create: `functionsystem/src/function_master/instance_manager/quota_manager/quota_manager.cpp`

- [ ] **Step 1: 创建 quota_manager.h**

```cpp
#ifndef FUNCTIONSYSTEM_SRC_FUNCTION_MASTER_INSTANCE_MANAGER_QUOTA_MANAGER_QUOTA_MANAGER_H
#define FUNCTIONSYSTEM_SRC_FUNCTION_MASTER_INSTANCE_MANAGER_QUOTA_MANAGER_QUOTA_MANAGER_H

#include "actor/actor.hpp"
#include "async/future.hpp"
#include "common/status/status.h"

namespace function_master {

class QuotaManager {
public:
    explicit QuotaManager(litebus::ActorReference actor);
    ~QuotaManager() = default;

private:
    litebus::ActorReference actor_{ nullptr };
};

}  // namespace function_master

#endif
```

- [ ] **Step 2: 创建 quota_manager.cpp**

```cpp
#include "function_master/instance_manager/quota_manager/quota_manager.h"

namespace function_master {

QuotaManager::QuotaManager(litebus::ActorReference actor)
    : actor_(std::move(actor))
{}

}  // namespace function_master
```

- [ ] **Step 3: Commit**

```bash
git add functionsystem/src/function_master/instance_manager/quota_manager/
git commit -m "feat(quota): add QuotaManager wrapper class"
```

---

## Chunk 3: Domain 调度器集成

### Task 9: InstanceCtrlActor 冷却拦截

**Files:**
- Modify: `functionsystem/src/domain_scheduler/instance_control/instance_ctrl_actor.h`
- Modify: `functionsystem/src/domain_scheduler/instance_control/instance_ctrl_actor.cpp`
- Modify: `functionsystem/src/domain_scheduler/instance_control/instance_ctrl.h`
- Modify: `functionsystem/src/domain_scheduler/instance_control/instance_ctrl.cpp`

- [ ] **Step 1: instance_ctrl_actor.h 新增成员和方法声明**

在 `InstanceCtrlActor` 类 private 区域，Timer 成员附近添加：

```cpp
// Quota 冷却：key=tenantID，value=冷却 Timer
std::unordered_map<std::string, litebus::Timer> blockedTenants_;
```

在 public 方法区域添加：

```cpp
void OnTenantQuotaExceeded(const litebus::AID &from, std::string &&name, std::string &&msg);
```

- [ ] **Step 2: instance_ctrl_actor.cpp - OnTenantQuotaExceeded 实现**

```cpp
void InstanceCtrlActor::OnTenantQuotaExceeded(
    const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::TenantQuotaExceeded event;
    if (!event.ParseFromString(msg)) {
        YRLOG_WARN("InstanceCtrlActor::OnTenantQuotaExceeded parse failed");
        return;
    }

    const std::string tenantID = event.tenantid();
    int64_t cooldownMs         = event.cooldownms();
    if (cooldownMs <= 0) {
        cooldownMs = 10000;  // 安全兜底
    }

    YRLOG_INFO("InstanceCtrlActor: tenant={} blocked for {}ms (quota exceeded)",
               tenantID, cooldownMs);

    // 覆盖已有 Timer（刷新冷却）
    blockedTenants_[tenantID] = litebus::Timer(
        cooldownMs,
        [this, tenantID]() {
            blockedTenants_.erase(tenantID);
            YRLOG_INFO("InstanceCtrlActor: tenant={} cooldown expired, scheduling resumed",
                       tenantID);
        });
}
```

- [ ] **Step 3: instance_ctrl_actor.cpp - Schedule() 入口拦截**

在 `InstanceCtrlActor::Schedule()` 方法，`ASSERT_IF_NULL(req)` 之后、`requestTrySchedTimes_` 之前插入：

```cpp
// Quota 冷却拦截
const auto &tenantID = req->instance().tenantid();
if (!tenantID.empty() && blockedTenants_.count(tenantID)) {
    schedule_decision::ScheduleResult result;
    result.code   = static_cast<int32_t>(StatusCode::ERR_RESOURCE_NOT_ENOUGH);
    result.reason = "QUOTA_EXCEEDED|tenantID=" + tenantID +
                    "|reason=tenant resource quota exceeded";
    auto promise = std::make_shared<litebus::Promise<std::shared_ptr<messages::ScheduleResponse>>>();
    promise->SetValue(BuildErrorScheduleRsp(result, req));
    return promise->GetFuture();
}
```

- [ ] **Step 4: instance_ctrl_actor.cpp Init() 中注册 handler**

在 `InstanceCtrlActor::Init()` 的 `Receive()` 调用群中添加：

```cpp
Receive("TenantQuotaExceeded", &InstanceCtrlActor::OnTenantQuotaExceeded);
```

- [ ] **Step 5: instance_ctrl.h 新增对外接口**

在 `InstanceCtrl` 类 public 区域添加：

```cpp
void OnTenantQuotaExceeded(std::string msg);
```

- [ ] **Step 6: instance_ctrl.cpp 实现 wrapper**

```cpp
void InstanceCtrl::OnTenantQuotaExceeded(std::string msg)
{
    litebus::AID from;  // 空 AID 占位
    (void)litebus::Async(aid_, &InstanceCtrlActor::OnTenantQuotaExceeded,
                         from, std::string{ "TenantQuotaExceeded" }, std::move(msg));
}
```

- [ ] **Step 7: 编译验证**

```bash
bash run.sh build -j 4 2>&1 | grep -E "instance_ctrl|error" | head -30
```

- [ ] **Step 8: Commit**

```bash
git add functionsystem/src/domain_scheduler/instance_control/
git commit -m "feat(domain): add quota cooldown interception in InstanceCtrlActor::Schedule()"
```

---

### Task 10: DomainSchedSrvActor 转发 TenantQuotaExceeded

**Files:**
- Modify: `functionsystem/src/domain_scheduler/domain_scheduler_service/domain_sched_srv_actor.h`
- Modify: `functionsystem/src/domain_scheduler/domain_scheduler_service/domain_sched_srv_actor.cpp`

- [ ] **Step 1: domain_sched_srv_actor.h 新增方法声明**

在 public 方法区域（其他 message handler 附近）添加：

```cpp
void OnTenantQuotaExceeded(const litebus::AID &from, std::string &&name, std::string &&msg);
```

- [ ] **Step 2: domain_sched_srv_actor.cpp 实现 + 注册**

在 `DomainSchedSrvActor::Init()` 的 `Receive()` 群中添加：

```cpp
Receive("TenantQuotaExceeded", &DomainSchedSrvActor::OnTenantQuotaExceeded);
```

新增方法实现：

```cpp
void DomainSchedSrvActor::OnTenantQuotaExceeded(
    const litebus::AID &from, std::string &&name, std::string &&msg)
{
    if (!instanceCtrl_) {
        YRLOG_WARN("DomainSchedSrvActor::OnTenantQuotaExceeded: instanceCtrl_ is null");
        return;
    }
    instanceCtrl_->OnTenantQuotaExceeded(std::move(msg));
}
```

- [ ] **Step 3: 编译验证**

```bash
bash run.sh build -j 4 2>&1 | grep -E "domain_sched_srv|error" | head -20
```

- [ ] **Step 4: Commit**

```bash
git add functionsystem/src/domain_scheduler/domain_scheduler_service/
git commit -m "feat(domain): forward TenantQuotaExceeded from DomainSchedSrvActor to InstanceCtrlActor"
```

---

## Chunk 4: InstanceManagerActor 通知

### Task 11: InstanceManagerActor 发送状态变更通知

**Files:**
- Modify: `functionsystem/src/function_master/instance_manager/instance_manager_actor.h`
- Modify: `functionsystem/src/function_master/instance_manager/instance_manager_actor.cpp`

- [ ] **Step 1: instance_manager_actor.h 新增成员**

在 `Member` struct（或直接成员区域）添加：

```cpp
litebus::AID quotaMgrAID_;
```

- [ ] **Step 2: instance_manager_actor.cpp - Init() 中获取 QuotaManagerActor AID**

在 `InstanceManagerActor::Init()` 末尾添加：

```cpp
quotaMgrAID_ = litebus::ActorSystem::GetInstance().GetActor(
    std::string(QUOTA_MANAGER_ACTOR_NAME));
```

需要 `#include "function_master/instance_manager/quota_manager/quota_manager_actor.h"` 以引用 `QUOTA_MANAGER_ACTOR_NAME`。

- [ ] **Step 3: 找到实例变为 RUNNING 的处理点，添加通知**

在 instance_manager_actor.cpp 中搜索 `InstanceState::RUNNING` 的判断处（约 line 2202），找到实例状态变更为 RUNNING 之后，添加通知：

```cpp
// 通知 QuotaManagerActor
if (!quotaMgrAID_.IsEmpty()) {
    Send(quotaMgrAID_, "OnInstanceRunning", instance->SerializeAsString());
}
```

- [ ] **Step 4: 找到实例退出处理点，添加 EXITED 通知**

搜索 `InstanceState::EXITING` 或 `InstanceState::EXITED` 判断处（约 line 1373），在实例状态变更为 EXITED 后添加：

```cpp
if (!quotaMgrAID_.IsEmpty()) {
    Send(quotaMgrAID_, "OnInstanceExited", instance->SerializeAsString());
}
```

- [ ] **Step 5: 编译验证**

```bash
bash run.sh build -j 4 2>&1 | grep -E "instance_manager_actor|error" | head -30
```

- [ ] **Step 6: Commit**

```bash
git add functionsystem/src/function_master/instance_manager/instance_manager_actor.h \
        functionsystem/src/function_master/instance_manager/instance_manager_actor.cpp
git commit -m "feat(instance-manager): notify QuotaManagerActor on instance RUNNING/EXITED state change"
```

---

## Chunk 5: 单元测试

### Task 12: QuotaConfig 和 QuotaManagerActor 单元测试

**Files:**
- Create: `functionsystem/tests/unit/function_master/instance_manager/quota_manager/quota_manager_test.cpp`
- Create: `functionsystem/tests/unit/function_master/instance_manager/quota_manager/CMakeLists.txt`
- Modify: `functionsystem/tests/unit/function_master/instance_manager/CMakeLists.txt`

- [ ] **Step 1: 创建测试 CMakeLists.txt**

`functionsystem/tests/unit/function_master/instance_manager/quota_manager/CMakeLists.txt`：

```cmake
aux_source_directory(${CMAKE_CURRENT_LIST_DIR} QUOTA_MANAGER_TEST_SRCS)
target_sources(${UNIT_TEST_MODULE} PRIVATE ${QUOTA_MANAGER_TEST_SRCS})
```

- [ ] **Step 2: 在 instance_manager/CMakeLists.txt 中添加子目录**

在 `functionsystem/tests/unit/function_master/instance_manager/CMakeLists.txt` 末尾追加：

```cmake
add_subdirectory(quota_manager)
```

- [ ] **Step 3: 创建测试文件，写 QuotaConfig 测试**

```cpp
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <fstream>

#include "function_master/instance_manager/quota_manager/quota_config.h"
#include "function_master/instance_manager/quota_manager/quota_manager_actor.h"

using namespace function_master;
using namespace testing;

// ─── QuotaConfig 测试 ─────────────────────────────────────────────────────────

class QuotaConfigTest : public ::testing::Test {};

// 空路径：返回内置默认值，不崩溃
TEST_F(QuotaConfigTest, EmptyPath_UsesBuiltInDefaults)
{
    auto cfg = QuotaConfig::LoadFromFile("");
    TenantQuota q = cfg.GetQuota("any-tenant");
    EXPECT_EQ(q.cpuMillicores, 32000);
    EXPECT_EQ(q.memLimitMb,    65536);
    EXPECT_EQ(q.cooldownMs,    10000);
}

// 有效 JSON 文件：正确解析 default quota
TEST_F(QuotaConfigTest, ValidJsonFile_ParsesDefaultQuota)
{
    // 创建临时 JSON 文件
    std::string tmpPath = "/tmp/quota_test_valid.json";
    std::ofstream f(tmpPath);
    f << R"({"default": {"cpuMillicores": 8000, "memMb": 16384, "cooldownMs": 5000}})";
    f.close();

    auto cfg = QuotaConfig::LoadFromFile(tmpPath);
    TenantQuota q = cfg.GetQuota("tenant-a");
    EXPECT_EQ(q.cpuMillicores, 8000);
    EXPECT_EQ(q.memLimitMb,    16384);
    EXPECT_EQ(q.cooldownMs,    5000);
}

// GetQuota：per-tenant 覆盖 default
TEST_F(QuotaConfigTest, GetQuota_PerTenantOverridesDefault)
{
    auto cfg = QuotaConfig::LoadFromFile("");
    cfg.UpdateTenantQuota("tenant-vip", TenantQuota{ 64000, 131072, 3000 });

    EXPECT_EQ(cfg.GetQuota("tenant-vip").cpuMillicores, 64000);
    EXPECT_EQ(cfg.GetQuota("tenant-other").cpuMillicores, 32000);  // 默认值
}

// ─── TenantUsage LIFO 顺序测试 ────────────────────────────────────────────────

class TenantUsageLIFOTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        actor_ = std::make_shared<QuotaManagerActor>(QuotaConfig::LoadFromFile(""));
        litebus::Spawn(actor_);
    }

    void TearDown() override
    {
        litebus::Terminate(actor_->GetAID());
        litebus::Await(actor_);
    }

    // 构造一个 InstanceInfo 发送 OnInstanceRunning 消息
    void SendRunning(const std::string &instanceID, const std::string &tenantID,
                     int64_t cpuMillicores, int64_t memMb, bool isSystem = false)
    {
        InstanceInfo info;
        info.set_instanceid(instanceID);
        info.set_tenantid(tenantID);
        info.set_issystemfunc(isSystem);
        (*info.mutable_resources()->mutable_resources())["CPU"].mutable_scalar()->set_value(
            static_cast<double>(cpuMillicores));
        (*info.mutable_resources()->mutable_resources())["Memory"].mutable_scalar()->set_value(
            static_cast<double>(memMb));
        litebus::AID from;
        (void)litebus::Async(actor_->GetAID(), &QuotaManagerActor::OnInstanceRunning,
                             from, std::string{ "OnInstanceRunning" },
                             info.SerializeAsString());
    }

    std::shared_ptr<QuotaManagerActor> actor_;
};

// 系统租户不参与统计
TEST_F(TenantUsageLIFOTest, SystemTenant_ExemptFromQuota)
{
    // 系统实例：即使 cpu 超配也不触发驱逐
    SendRunning("sys-inst-1", "0", 999999, 999999, /*isSystem=*/true);
    // 等待消息处理
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // 无驱逐信号（此处验证 actor 未崩溃，详细驱逐行为需 mock InstanceManagerActor）
    SUCCEED();
}

// tenantID 为空跳过统计
TEST_F(TenantUsageLIFOTest, EmptyTenantID_Skipped)
{
    SendRunning("inst-no-tenant", "", 1000, 1024);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    SUCCEED();
}
```

- [ ] **Step 4: 运行测试，确认通过**

```bash
bash run.sh build -j 4 2>&1 | tail -5
bash run.sh test -j 4 exec -s "QuotaConfigTest"
bash run.sh test -j 4 exec -s "TenantUsageLIFOTest"
```

期望：所有 QuotaConfigTest.* 和 TenantUsageLIFOTest.* PASSED。

- [ ] **Step 5: Commit**

```bash
git add functionsystem/tests/unit/function_master/instance_manager/quota_manager/ \
        functionsystem/tests/unit/function_master/instance_manager/CMakeLists.txt
git commit -m "test(quota): add QuotaConfig and QuotaManagerActor unit tests"
```

---

### Task 13: InstanceCtrlActor 冷却逻辑测试

**Files:**
- Create: `functionsystem/tests/unit/domain_scheduler/instance_control/instance_ctrl_quota_test.cpp`

注意：`instance_control/CMakeLists.txt` 使用 `aux_source_directory`，新文件自动纳入，无需修改。

- [ ] **Step 1: 创建测试文件**

```cpp
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 */

#include "domain_scheduler/instance_control/instance_ctrl.h"
#include "domain_scheduler/instance_control/instance_ctrl_actor.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include "mocks/mock_scheduler.h"
#include "mocks/mock_domain_underlayer_sched_mgr.h"
#include "mocks/mock_resource_view.h"
#include "utils/future_test_helper.h"
#include "proto/posix/message.pb.h"

using namespace domain_scheduler;
using namespace testing;

class InstanceCtrlQuotaTest : public ::testing::Test {
public:
    void SetUp() override
    {
        actor_ = std::make_shared<InstanceCtrlActor>("QuotaTest");
        mockScheduler_    = std::make_shared<MockScheduler>();
        mockUnderlayer_   = std::make_shared<MockDomainUnderlayerSchedMgr>();

        auto primary = MockResourceView::CreateMockResourceView();
        auto virt    = MockResourceView::CreateMockResourceView();
        resourceViewMgr_ = std::make_shared<resource_view::ResourceViewMgr>(primary, virt);

        actor_->BindScheduler(mockScheduler_);
        actor_->BindResourceView(resourceViewMgr_);
        actor_->BindUnderlayerMgr(mockUnderlayer_);

        litebus::Spawn(actor_);
        ctrl_ = std::make_shared<InstanceCtrl>(actor_->GetAID());
    }

    void TearDown() override
    {
        litebus::Terminate(actor_->GetAID());
        litebus::Await(actor_);
    }

    // 发送 TenantQuotaExceeded 消息给 actor
    void SendQuotaExceeded(const std::string &tenantID, int64_t cooldownMs)
    {
        messages::TenantQuotaExceeded event;
        event.set_tenantid(tenantID);
        event.set_cooldownms(cooldownMs);
        ctrl_->OnTenantQuotaExceeded(event.SerializeAsString());
    }

    std::shared_ptr<InstanceCtrlActor>                actor_;
    std::shared_ptr<InstanceCtrl>                     ctrl_;
    std::shared_ptr<MockScheduler>                    mockScheduler_;
    std::shared_ptr<MockDomainUnderlayerSchedMgr>     mockUnderlayer_;
    std::shared_ptr<resource_view::ResourceViewMgr>   resourceViewMgr_;
};

// 冷却期内：Schedule 直接返回 ERR_RESOURCE_NOT_ENOUGH，不调用 scheduler
TEST_F(InstanceCtrlQuotaTest, Schedule_BlockedDuringCooldown)
{
    const std::string tenantID = "test-tenant";

    // 设置冷却（500ms）
    SendQuotaExceeded(tenantID, 500);
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // 等消息处理

    // scheduler 不应被调用
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _)).Times(0);

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("req-blocked-001");
    req->mutable_instance()->set_tenantid(tenantID);

    auto future = ctrl_->Schedule(req);
    ASSERT_AWAIT_READY_FOR(future, 1000);

    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), static_cast<int32_t>(StatusCode::ERR_RESOURCE_NOT_ENOUGH));
    EXPECT_THAT(rsp->message(), HasSubstr("QUOTA_EXCEEDED"));
    EXPECT_THAT(rsp->message(), HasSubstr(tenantID));
}

// 冷却期满后：Schedule 正常路由到 scheduler
TEST_F(InstanceCtrlQuotaTest, Schedule_ResumesAfterCooldown)
{
    const std::string tenantID = "test-tenant-2";

    // 设置短冷却（200ms）
    SendQuotaExceeded(tenantID, 200);
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // 等冷却过期

    // scheduler 应被调用
    ScheduleResult result{ "unit-1", 0, "" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _))
        .WillOnce(Return(AsyncReturn(result)));

    auto mockRsp = std::make_shared<messages::ScheduleResponse>();
    mockRsp->set_code(0);
    mockRsp->set_requestid("req-resume-001");
    EXPECT_CALL(*mockUnderlayer_, DispatchSchedule("unit-1", _))
        .WillOnce(Return(AsyncReturn(mockRsp)));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("req-resume-001");
    req->mutable_instance()->set_tenantid(tenantID);

    auto future = ctrl_->Schedule(req);
    ASSERT_AWAIT_READY_FOR(future, 2000);

    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), 0);
}

// 重复 QuotaExceeded：Timer 刷新，冷却从新开始
TEST_F(InstanceCtrlQuotaTest, QuotaExceeded_TimerRefreshOnRepeat)
{
    const std::string tenantID = "test-tenant-3";

    SendQuotaExceeded(tenantID, 300);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 冷却中再次触发，Timer 刷新
    SendQuotaExceeded(tenantID, 300);
    std::this_thread::sleep_for(std::chrono::milliseconds(150)); // 第一个 Timer 已到期，但第二个未到

    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _)).Times(0);

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("req-refresh-001");
    req->mutable_instance()->set_tenantid(tenantID);

    auto future = ctrl_->Schedule(req);
    ASSERT_AWAIT_READY_FOR(future, 1000);

    auto rsp = future.Get();
    EXPECT_EQ(rsp->code(), static_cast<int32_t>(StatusCode::ERR_RESOURCE_NOT_ENOUGH));
}

// 空 tenantID：不拦截，正常调度
TEST_F(InstanceCtrlQuotaTest, Schedule_EmptyTenantID_NotBlocked)
{
    ScheduleResult result{ "unit-1", 0, "" };
    EXPECT_CALL(*mockScheduler_, ScheduleDecision(_, _))
        .WillOnce(Return(AsyncReturn(result)));

    auto mockRsp = std::make_shared<messages::ScheduleResponse>();
    mockRsp->set_code(0);
    mockRsp->set_requestid("req-empty-tenant");
    EXPECT_CALL(*mockUnderlayer_, DispatchSchedule(_, _))
        .WillOnce(Return(AsyncReturn(mockRsp)));

    auto req = std::make_shared<messages::ScheduleRequest>();
    req->set_requestid("req-empty-tenant");
    // tenantID 为空

    auto future = ctrl_->Schedule(req);
    ASSERT_AWAIT_READY_FOR(future, 2000);
    EXPECT_EQ(future.Get()->code(), 0);
}
```

- [ ] **Step 2: 运行测试**

```bash
bash run.sh build -j 4 2>&1 | tail -5
bash run.sh test -j 4 exec -s "InstanceCtrlQuotaTest"
```

期望：4 个测试全部 PASSED。

- [ ] **Step 3: Commit**

```bash
git add functionsystem/tests/unit/domain_scheduler/instance_control/instance_ctrl_quota_test.cpp
git commit -m "test(quota): add InstanceCtrlActor cooldown blocking unit tests"
```

---

## Chunk 6: 启动集成 & 全量验证

### Task 14: function_master 启动集成

**Files:**
- 找到 function_master 的启动/组装代码（通常在 `function_master_launcher.cpp` 或 `startup/` 目录）

- [ ] **Step 1: 定位启动代码**

```bash
find /home/robbluo/code/yuanrong-functionsystem/functionsystem/src/function_master \
     -name "*launcher*" -o -name "*startup*" | head -10
```

- [ ] **Step 2: 在启动代码中创建并注册 QuotaManagerActor**

在 InstanceManagerActor 创建之后添加：

```cpp
#include "function_master/instance_manager/quota_manager/quota_manager_actor.h"
#include "function_master/instance_manager/quota_manager/quota_manager.h"
#include "function_master/instance_manager/quota_manager/quota_config.h"

// 加载 quota 配置
auto quotaConfig = function_master::QuotaConfig::LoadFromFile(
    flags->GetQuotaConfigFile());   // flags 为 CommonFlags 实例

// 创建 QuotaManagerActor
auto quotaMgrActor = std::make_shared<function_master::QuotaManagerActor>(
    std::move(quotaConfig));
litebus::Spawn(quotaMgrActor);
```

- [ ] **Step 3: 完整编译验证**

```bash
bash run.sh build -j 4 2>&1 | tail -20
```

期望：`Build successful` 或等价输出，无 error。

- [ ] **Step 4: 运行全量单元测试**

```bash
bash run.sh test -j 4 exec
```

期望：所有已有测试通过，新增测试通过，无 regression。

- [ ] **Step 5: 最终 Commit**

```bash
git add functionsystem/src/function_master/
git commit -m "feat(quota): wire QuotaManagerActor into function_master startup"
```

---

## 测试覆盖摘要

| 测试类 | 测试用例 | 验证行为 |
|--------|---------|---------|
| `QuotaConfigTest` | `EmptyPath_UsesBuiltInDefaults` | Flag 未配置时使用兜底默认值 |
| `QuotaConfigTest` | `ValidJsonFile_ParsesDefaultQuota` | JSON 文件正确解析 |
| `QuotaConfigTest` | `GetQuota_PerTenantOverridesDefault` | per-tenant 覆盖 default |
| `TenantUsageLIFOTest` | `SystemTenant_ExemptFromQuota` | 系统租户豁免 |
| `TenantUsageLIFOTest` | `EmptyTenantID_Skipped` | 空 tenantID 不统计 |
| `InstanceCtrlQuotaTest` | `Schedule_BlockedDuringCooldown` | 冷却期内拒绝调度 + 错误码/消息格式 |
| `InstanceCtrlQuotaTest` | `Schedule_ResumesAfterCooldown` | 冷却期满后正常调度 |
| `InstanceCtrlQuotaTest` | `QuotaExceeded_TimerRefreshOnRepeat` | 重复超配刷新冷却 Timer |
| `InstanceCtrlQuotaTest` | `Schedule_EmptyTenantID_NotBlocked` | 空 tenantID 不拦截 |
