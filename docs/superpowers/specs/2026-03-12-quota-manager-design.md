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
   - 通知 `InstanceCtrlActor` 进入冷却，冷却期内拒绝该租户的调度请求
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

`domain_scheduler` 侧小范围修改：
- `instance_control/instance_ctrl_actor.h/.cpp`：新增冷却处理
- `domain_scheduler_service/domain_sched_srv_actor.h/.cpp`：新增 `TenantQuotaExceeded` 转发

---

## 3. 架构设计

### 3.1 组件职责

| 组件 | 所属模块 | 职责 |
|------|---------|------|
| `QuotaManagerActor` | function_master/instance_manager | 用量跟踪、超配检测、LIFO 驱逐、发送超配通知 |
| `QuotaManager` | function_master/instance_manager | 对外接口封装，供外部 Init 使用 |
| `QuotaConfig` | function_master/instance_manager | 加载 JSON 配置，提供 quota 查询接口（预留 per-tenant 差异化）|
| `DomainSchedSrvActor`（修改）| domain_scheduler | 接收 `TenantQuotaExceeded`，转发给 `InstanceCtrlActor` |
| `InstanceCtrlActor`（修改）| domain_scheduler | 接收超配通知，维护 `blockedTenants_` + cooldown Timer |

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
LIFO：从 sortedInstances 尾部（到达时间最晚）逐个 Kill
  └─ Send(instanceMgrAID, "ForwardKill", ForwardKillRequest{instanceID, ...})

④ 冷却通知（QuotaManagerActor → DomainSchedSrvActor → InstanceCtrlActor）
通过 Global 获取 root domain 的 DomainSchedSrvActor AID
  └─ Send(domainSchedSrvAID, "TenantQuotaExceeded", {tenantID, cooldownMs})
  └─ DomainSchedSrvActor 转发给 instanceCtrl_->OnTenantQuotaExceeded(...)

⑤ 调度拦截（InstanceCtrlActor 自主决策）
InstanceCtrlActor::Schedule() 入口最前端（null check 之后）：
  └─ blockedTenants_.count(tenantID) → 直接返回 ERR_RESOURCE_NOT_ENOUGH
  └─ 未被 block → 正常调度流程
```

### 3.3 完整时序

```
InstanceMgrActor   QuotaMgrActor    DomainSchedSrvActor  InstanceCtrlActor
      │                  │                  │                   │
      │──OnInstanceRun──▶│                  │                   │
      │                  │─CheckAndEnforce  │                   │
      │                  │ (超配)           │                   │
      │◀─ForwardKill─────│                  │                   │
      │                  │──TenantQuota────▶│                   │
      │                  │   Exceeded       │──forward─────────▶│
      │                  │                  │                   │ blockedTenants_+Timer
      │──OnInstanceExit─▶│                  │    10s 后         │
      │                  │ 用量更新          │                   │ Timer 到期
      │                  │                  │                   │ erase(tenantID)
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
    // Flag 未配置：使用内置兜底默认值，打印 WARNING，正常启动
    // Flag 已配置但文件不存在或解析失败：FATAL + 进程退出
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
--quota_config_file=<path>   配置文件路径
```

### 4.2 QuotaManagerActor 内部状态

```cpp
struct TenantUsage {
    int64_t cpuMillicores{ 0 };
    int64_t memMb{ 0 };
    // LIFO 驱逐：按到达时间升序存储，驱逐从尾部取（最晚到达）
    // key: arrivalTimeMs（QuotaManagerActor 收到 OnInstanceRunning 时的本地时间戳）
    // 使用 multimap 保证同一毫秒内多个实例有确定顺序
    std::multimap<int64_t, std::string> sortedInstances; // {arrivalTimeMs, instanceID}
};

// QuotaManagerActor 成员（直接持有，不嵌套 Member struct）
QuotaConfig                                     config_;
std::unordered_map<std::string, TenantUsage>    tenantUsage_;
std::unordered_map<std::string, int64_t>        instanceArrivalTime_; // instanceID → arrivalTimeMs
```

**说明：** `InstanceInfo.startTime` 为 string 类型，无 `createTime` 字段；LIFO 排序使用 QuotaManagerActor 本地记录的到达时间戳，保证确定性。

### 4.3 资源提取规则

`InstanceInfo.resources()` 是 `map<string, Resource>`，字段名称常量来自 `resource_type.h`：

```cpp
// resource_type.h
const std::string CPU_RESOURCE_NAME    = "CPU";
const std::string MEMORY_RESOURCE_NAME = "Memory";

// 提取示例
const auto &resMap = instanceInfo.resources().resources();
double cpuCores = resMap.count("CPU")    ? resMap.at("CPU").scalar().value()    : 0.0;
double memMb    = resMap.count("Memory") ? resMap.at("Memory").scalar().value() : 0.0;

// 转换为内部单位
int64_t cpuMillicores = static_cast<int64_t>(cpuCores * 1000);
int64_t memMbInt      = static_cast<int64_t>(memMb);
```

**单位约定：** CPU scalar.value() 单位为核（core），×1000 转毫核；Memory scalar.value() 单位为 MB，直接转 int64。

### 4.4 InstanceCtrlActor 新增状态

```cpp
// 超 quota 租户的冷却集合，Timer 到期自动移除
std::unordered_map<std::string, litebus::Timer> blockedTenants_;
```

### 4.5 新增 Proto 消息

```protobuf
// 文件：proto/posix/message.proto
// QuotaManagerActor → DomainSchedSrvActor → InstanceCtrlActor
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

### 5.2 QuotaManagerActor（quota_manager_actor.h）

```cpp
// Actor 注册名称（类比 INSTANCE_CTRL_ACTOR_NAME_POSTFIX 命名惯例）
constexpr std::string_view QUOTA_MANAGER_ACTOR_NAME = "QuotaManagerActor";

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

    // 直接成员（无 Member 包装层）
    QuotaConfig                                     config_;
    std::unordered_map<std::string, TenantUsage>    tenantUsage_;
    std::unordered_map<std::string, int64_t>        instanceArrivalTime_;
};
```

**AID 获取方式：** `InstanceManagerActor` 通过 `litebus::ActorSystem::GetActor(QUOTA_MANAGER_ACTOR_NAME)` 获取 `QuotaManagerActor` 的 AID，与现有 actor 查找惯例一致。

### 5.3 InstanceCtrlActor 新增处理

**调度拦截**（插入位置：`Schedule()` 最前端，`ASSERT_IF_NULL(req)` 之后）：

```cpp
litebus::Future<std::shared_ptr<messages::ScheduleResponse>>
InstanceCtrlActor::Schedule(const std::shared_ptr<messages::ScheduleRequest> &req)
{
    ASSERT_IF_NULL(req);

    // 租户冷却检查（新增）
    const auto &tenantID = req->instance().tenantid();
    if (blockedTenants_.count(tenantID)) {
        schedule_decision::ScheduleResult result;
        result.code   = static_cast<int32_t>(StatusCode::ERR_RESOURCE_NOT_ENOUGH);
        result.reason = "tenant quota exceeded, retry after cooldown";
        auto promise  = std::make_shared<litebus::Promise<std::shared_ptr<messages::ScheduleResponse>>>();
        promise->SetValue(BuildErrorScheduleRsp(result, req));
        return promise->GetFuture();
    }

    // 原有逻辑保持不变
    if (requestTrySchedTimes_.find(req->requestid()) == requestTrySchedTimes_.end()) {
        requestTrySchedTimes_[req->requestid()] = 0;
    }
    ...
}
```

**冷却 Timer 处理**：

```cpp
void InstanceCtrlActor::OnTenantQuotaExceeded(
    const litebus::AID &from, std::string &&name, std::string &&msg)
{
    messages::TenantQuotaExceeded event;
    event.ParseFromString(msg);
    const auto &tenantID = event.tenantid();
    int64_t cooldownMs   = event.cooldownms();

    // 重置（已有 Timer 则覆盖，相当于刷新冷却）
    blockedTenants_[tenantID] = litebus::Timer(
        std::chrono::milliseconds(cooldownMs),
        [this, tenantID]() { blockedTenants_.erase(tenantID); });
}
```

### 5.4 DomainSchedSrvActor 新增转发

```cpp
// domain_sched_srv_actor.cpp 新增 handler
void DomainSchedSrvActor::OnTenantQuotaExceeded(
    const litebus::AID &from, std::string &&name, std::string &&msg)
{
    // 直接转发给 instanceCtrl_（DomainSchedSrvActor 已持有其引用）
    instanceCtrl_->OnTenantQuotaExceeded(from, std::move(name), std::move(msg));
}
```

### 5.5 驱逐实现（ForwardKill）

```cpp
// QuotaManagerActor::CheckAndEnforce 驱逐部分
auto &usage = tenantUsage_[tenantID];
auto quota  = config_.GetQuota(tenantID);

while ((usage.cpuMillicores > quota.cpuMillicores ||
        usage.memMb > quota.memLimitMb) &&
       !usage.sortedInstances.empty())
{
    auto it = std::prev(usage.sortedInstances.end()); // 最晚到达
    const std::string &instanceID = it->second;

    // 使用 InstanceManagerActor 现有 ForwardKill 消息
    inner_service::ForwardKillRequest killReq;
    killReq.set_instanceid(instanceID);
    // requestID 填 quota_eviction 标识，便于日志追踪
    killReq.set_requestid("quota_eviction_" + instanceID);

    Send(instanceMgrAID_, "ForwardKill", killReq.SerializeAsString());

    // 乐观移除（用量在 OnInstanceExited 回调中最终扣减）
    instanceArrivalTime_.erase(instanceID);
    usage.sortedInstances.erase(it);
}
```

### 5.6 预留 per-tenant 外部接口

```cpp
// QuotaManagerActor 预留，首阶段不实现
void UpdateTenantQuota(const std::string &tenantID, const TenantQuota &quota);
```

内部 `GetQuota()` 查询顺序：
1. `perTenantQuota_[tenantID]`（首阶段为空）
2. `config_.defaultQuota_`

---

## 6. 启动集成

### function_master 启动序列

```
1. 解析 Flag：--quota_config_file
   ├─ Flag 未配置 → WARNING 日志，使用内置兜底默认值，正常启动
   └─ Flag 已配置 → QuotaConfig::LoadFromFile(path)
        ├─ 文件不存在或 JSON 解析失败 → FATAL + 进程退出
        └─ 成功 → 使用加载的配置

2. 创建 QuotaManagerActor，注入 config_，以 QUOTA_MANAGER_ACTOR_NAME 注册

3. QuotaManagerActor::Init()
   ├─ 通过 Global 获取 InstanceManagerActor AID（复用现有机制）
   ├─ 拉取全量 running 实例，调用 RebuildUsageFromSnapshot()
   └─ 对超配租户执行 CheckAndEnforce()
```

### domain_scheduler（变更）

无启动序列变更。新增：
- `InstanceCtrlActor`：注册 `OnTenantQuotaExceeded` handler
- `DomainSchedSrvActor`：注册 `TenantQuotaExceeded` 消息 → 转发给 `instanceCtrl_`

---

## 7. 边界情况 & 错误处理

| 场景 | 处理策略 |
|------|---------|
| 启动时 InstanceManagerActor 未就绪 | `AsyncAfter` 延迟重试 N 次；超限则 WARNING，以空用量启动（不阻塞服务）|
| ForwardKill 后实例长时间未退出 | 乐观移除 sortedInstances，`OnInstanceExited` 做最终扣减；超时重发复用现有异常处理机制 |
| 超配通知发出后用量自然降回 quota 以内 | 冷却 Timer 不提前取消（保守策略，避免抖动），冷却期满自动恢复 |
| 同一租户重复触发超配（冷却中再次超配）| `OnTenantQuotaExceeded` 覆盖 Timer（刷新冷却），保证单一 Timer |
| Flag 未配置 | WARNING 日志，使用内置兜底默认值，正常启动 |
| Flag 已配置，文件不存在或解析失败 | FATAL + 进程退出 |
| `tenantID` 为空 | 跳过 quota 检查，打印 WARNING |
| 系统租户（`systemTenantID`，默认 `"0"`）| 豁免 quota 限制，不参与统计与驱逐 |
| 同一租户多实例并发 Running | Actor 单线程串行处理，天然无并发竞争 |

---

## 8. 不在本期范围内

- per-tenant 差异化 quota 的外部接口实现（接口签名已预留）
- quota 变更的热更新（当前需重启生效）
- 基于 `actualUse`（实际用量测量）的动态统计（当前基于 `resources` 请求值）
- quota 超配的告警/上报
- 单元测试覆盖（建议覆盖：LIFO 驱逐顺序、冷却 Timer 刷新、系统租户豁免）

---

## 9. 关键文件影响范围

| 文件 | 变更类型 |
|------|---------|
| `instance_manager/quota_manager/quota_config.h` | 新增 |
| `instance_manager/quota_manager/quota_manager_actor.h/.cpp` | 新增 |
| `instance_manager/quota_manager/quota_manager.h/.cpp` | 新增 |
| `instance_manager/instance_manager_actor.h/.cpp` | 修改：新增实例状态变更通知 |
| `domain_scheduler/instance_control/instance_ctrl_actor.h/.cpp` | 修改：新增冷却处理、Schedule() 拦截 |
| `domain_scheduler/domain_scheduler_service/domain_sched_srv_actor.h/.cpp` | 修改：新增 TenantQuotaExceeded 转发 |
| `proto/posix/message.proto` | 修改：新增 `TenantQuotaExceeded` 消息 |
| `common_flags/common_flags.h/.cpp` | 修改：新增 `--quota_config_file` Flag |
