# QuotaManagerActor 设计文档

**日期**：2026-03-12
**模块**：function_master / instance_manager
**状态**：待实现

---

## 1. 背景与目标

在 `function_master` 的 `instance_manager` 模块下新增 `QuotaManagerActor`，实现租户级别的资源 quota 管理。

**核心能力：**

1. 支持获取租户的 CPU（毫核）、内存（MB）quota 配置（首阶段为静态 JSON 配置，支持默认值）
2. 根据租户下所有 running 实例统计当前资源用量
3. 当租户资源超过 quota 时：
   - 主动驱逐最新创建的实例（LIFO），直至用量回到 quota 以内
   - 通知 root domain 的 `InstanceCtrlActor` 进入冷却，冷却期内拒绝该租户的调度请求
   - 冷却时间可配，默认 10s

---

## 2. 目录组织

```
functionsystem/src/function_master/instance_manager/
└── quota_manager/
    ├── quota_manager_actor.h
    ├── quota_manager_actor.cpp
    ├── quota_manager.h          # 对外接口封装（类比 instance_manager.h）
    ├── quota_manager.cpp
    └── quota_config.h           # QuotaConfig 数据结构 + JSON 解析
```

`domain_scheduler/instance_control/instance_ctrl_actor.h/.cpp` 新增冷却处理逻辑（小范围修改）。

---

## 3. 架构设计

### 3.1 组件职责

| 组件 | 所属模块 | 职责 |
|------|---------|------|
| `QuotaManagerActor` | function_master/instance_manager | 用量跟踪、超配检测、LIFO 驱逐、发送超配通知 |
| `QuotaManager` | function_master/instance_manager | 对外接口封装，供外部 Bind/Init 使用 |
| `QuotaConfig` | function_master/instance_manager | 加载 JSON 配置，提供 quota 查询接口（预留 per-tenant 差异化）|
| `InstanceCtrlActor`（修改）| domain_scheduler | 接收超配通知，维护 blockedTenants_ + cooldown Timer |

### 3.2 数据流

```
① 实例状态变更（InstanceManagerActor → QuotaManagerActor）
InstanceManagerActor 收到实例 RUNNING/EXITED 事件
  └─ Send(quotaMgrAID, "OnInstanceRunning" / "OnInstanceExited", payload)

② 超配检测（QuotaManagerActor 内部）
OnInstanceRunning 触发 CheckAndEnforce(tenantID)
  └─ 用量 ≤ quota：无操作
  └─ 用量 > quota：触发驱逐 + 通知冷却

③ 驱逐（QuotaManagerActor → InstanceManagerActor）
LIFO：从 sortedInstances 尾部（最新实例）逐个 Kill
  └─ Send(instanceMgrAID, "KillInstance", {instanceID, reason="quota_eviction"})

④ 冷却通知（QuotaManagerActor → InstanceCtrlActor）
通过 Global 获取 root domain AID（复用 InstanceManagerActor 现有机制）
  └─ Send(domainAID, "TenantQuotaExceeded", {tenantID, cooldownMs})

⑤ 调度拦截（InstanceCtrlActor 自主决策）
InstanceCtrlActor::ScheduleDecision() 入口：
  └─ blockedTenants_.count(tenantID) → 直接返回 ERR_RESOURCE_NOT_ENOUGH
  └─ 未被 block → 正常调度流程
```

### 3.3 完整时序

```
InstanceMgrActor   QuotaMgrActor      root DomainCtrlActor  InstanceCtrlActor
      │                  │                     │                    │
      │──OnInstanceRun──▶│                     │                    │
      │                  │─CheckAndEnforce     │                    │
      │                  │ (超配)              │                    │
      │◀─KillInstance────│                     │                    │
      │                  │──TenantQuotaExceeded▶│                   │
      │                  │                     │──forward──────────▶│
      │                  │                     │                    │ blockedTenants_+Timer
      │──OnInstanceExit─▶│                     │                    │
      │                  │ 用量更新             │    10s 后          │
      │                  │                     │                    │ Timer 到期
      │                  │                     │                    │ erase(tenantID)
```

---

## 4. 数据结构

### 4.1 QuotaConfig（quota_config.h）

```cpp
struct TenantQuota {
    int64_t cpuMillicores{ 0 };   // CPU 毫核，如 32000 = 32 核
    int64_t memLimitMb{ 0 };      // 内存 MB
    int64_t cooldownMs{ 10000 };  // 冷却时间，默认 10s
};

class QuotaConfig {
public:
    // 从 JSON 文件加载，Flag: --quota_config_file
    static QuotaConfig LoadFromFile(const std::string &path);

    // 获取租户 quota
    // 查询顺序：perTenantQuota_[tenantID] → defaultQuota_
    // 首阶段 perTenantQuota_ 为空，始终返回 defaultQuota_
    TenantQuota GetQuota(const std::string &tenantID) const;

private:
    TenantQuota defaultQuota_;
    // 预留 per-tenant 差异化：由外部接口写入
    std::unordered_map<std::string, TenantQuota> perTenantQuota_;
};
```

**JSON 配置示例：**
```json
{
  "default": {
    "cpuMillicores": 32000,
    "memMb": 65536,
    "cooldownMs": 10000
  }
}
```

**启动 Flag：**
```
--quota_config_file=<path>   配置文件路径；未配置时使用内置兜底默认值
```

### 4.2 QuotaManagerActor 内部状态

```cpp
struct TenantUsage {
    int64_t cpuMillicores{ 0 };
    int64_t memMb{ 0 };
    // LIFO 驱逐：按 createTime 升序，驱逐从尾部取（最新）
    std::vector<std::pair<int64_t, std::string>> sortedInstances; // {createTime, instanceID}
};

struct Member {
    QuotaConfig config_;
    std::unordered_map<std::string, TenantUsage> tenantUsage_;
};
```

### 4.3 InstanceCtrlActor 新增状态

```cpp
// 超 quota 租户的冷却集合，Timer 到期自动移除
std::unordered_map<std::string, litebus::Timer> blockedTenants_;
```

### 4.4 新增 Proto 消息

```protobuf
// QuotaManagerActor → InstanceCtrlActor
message TenantQuotaExceeded {
    string tenantID   = 1;
    int64  cooldownMs = 2;
}
```

---

## 5. 接口设计

### 5.1 QuotaManager（quota_manager.h）

```cpp
class QuotaManager {
public:
    explicit QuotaManager(const litebus::AID &aid);

    // 启动时拉取存量实例，重建用量快照
    litebus::Future<Status> Init();

private:
    litebus::AID aid_;
};
```

### 5.2 QuotaManagerActor 消息处理

```cpp
class QuotaManagerActor : public litebus::ActorBase {
protected:
    void Init() override;     // 拉取存量 running 实例，重建 tenantUsage_
    void Finalize() override;

    // 来自 InstanceManagerActor 的状态通知
    void OnInstanceRunning(const litebus::AID &from, std::string &&name, std::string &&msg);
    void OnInstanceExited (const litebus::AID &from, std::string &&name, std::string &&msg);

private:
    void CheckAndEnforce(const std::string &tenantID);
    void RebuildUsageFromSnapshot(const std::vector<InstanceInfo> &instances);

    QuotaConfig config_;
    Member      member_;
};
```

### 5.3 InstanceCtrlActor 新增处理

```cpp
// 接收超配通知，启动冷却 Timer
void OnTenantQuotaExceeded(const litebus::AID &from,
                           std::string &&name,
                           std::string &&msg);
```

`ScheduleDecision` 入口新增拦截：
```cpp
if (blockedTenants_.count(req->instance().tenantid())) {
    return BuildErrorScheduleRsp(
        StatusCode::ERR_RESOURCE_NOT_ENOUGH,
        "tenant quota exceeded, retry after cooldown");
}
```

### 5.4 预留 per-tenant 外部接口

```cpp
// QuotaManagerActor 预留，首阶段不实现
void UpdateTenantQuota(const std::string &tenantID, const TenantQuota &quota);
```

---

## 6. 启动集成

### function_master 启动序列

```
1. 解析 Flag：--quota_config_file
2. QuotaConfig::LoadFromFile(path)
   └─ 文件不存在或解析失败 → FATAL + 退出
   └─ Flag 未配置 → 使用内置兜底默认值，打印 WARNING
3. 创建 QuotaManagerActor，注入 config_
4. QuotaManagerActor::Init()
   └─ 通过 Global 获取 InstanceManagerActor AID（复用现有机制）
   └─ 拉取全量 running 实例，重建 tenantUsage_
   └─ 对超配租户执行 CheckAndEnforce()
```

### domain_scheduler（InstanceCtrlActor）

无启动序列变更。仅新增 `OnTenantQuotaExceeded` 消息处理 handler。

---

## 7. 边界情况 & 错误处理

| 场景 | 处理策略 |
|------|---------|
| 启动时 InstanceManagerActor 未就绪 | `AsyncAfter` 延迟重试 N 次；超限则 WARNING 日志，以空用量启动（不阻塞服务）|
| KillInstance 无响应 / 实例未及时退出 | 乐观移除 sortedInstances，`OnInstanceExited` 做最终用量扣减；超时后重发 Kill（复用现有异常实例处理机制）|
| 超配通知发出后用量自然降回 quota 以内 | 冷却 Timer 不提前取消（保守策略，避免抖动），冷却期满自动恢复 |
| `--quota_config_file` 文件不存在或 JSON 非法 | FATAL 日志 + 进程退出 |
| Flag 未配置 | 使用内置兜底默认值，打印 WARNING |
| `tenantID` 为空 | 跳过 quota 检查，打印 WARNING |
| 系统租户（`systemTenantID`，默认 `"0"`）| 豁免 quota 限制，不参与统计与驱逐 |
| 同一租户多实例并发变为 Running | Actor 单线程串行处理，天然无并发竞争 |

---

## 8. 不在本期范围内

- per-tenant 差异化 quota 的外部接口实现（接口签名已预留）
- quota 变更的热更新（当前需重启生效）
- 基于 `actualUse`（实际用量测量）的动态统计（当前基于 `resources` 请求值）
- quota 超配的告警/上报

---

## 9. 关键文件影响范围

| 文件 | 变更类型 |
|------|---------|
| `instance_manager/quota_manager/quota_config.h` | 新增 |
| `instance_manager/quota_manager/quota_manager_actor.h/.cpp` | 新增 |
| `instance_manager/quota_manager/quota_manager.h/.cpp` | 新增 |
| `instance_manager/instance_manager_actor.h/.cpp` | 修改：新增实例状态变更通知 |
| `domain_scheduler/instance_control/instance_ctrl_actor.h/.cpp` | 修改：新增冷却处理 |
| `proto/posix/message.proto` | 修改：新增 `TenantQuotaExceeded` 消息 |
| `common_flags/common_flags.h/.cpp` | 修改：新增 `--quota_config_file` Flag |
