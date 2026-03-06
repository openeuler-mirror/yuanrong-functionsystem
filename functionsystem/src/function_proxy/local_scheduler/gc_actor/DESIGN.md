# LocalGcActor 设计文档

**模块路径：** `functionsystem/src/function_proxy/local_scheduler/gc_actor/`
**所属分支：** `feat/br_local_gc`
**文档版本：** 1.0.0
**日期：** 2026-03-03

---

## 1. 背景与动机

`local_scheduler` 负责管理本节点上所有函数实例的生命周期。在实际运行中，实例可能因网络抖动、运行时崩溃、资源争抢等原因停留在以下异常状态：

| 类别 | 状态 | 产生原因 |
|------|------|----------|
| 终态异常 | `FAILED` | 部署失败、运行时报错 |
| 终态异常 | `EXITED` | 实例正常/异常退出后未被及时清理 |
| 终态异常 | `EVICTED` | 被驱逐后资源未释放 |
| 卡滞过渡态 | `CREATING` | 部署流程超时/失联 |
| 卡滞过渡态 | `SCHEDULING` | 调度流程卡死 |

这些实例既占用元数据存储空间，又可能影响资源视图的准确性，甚至导致新实例无法调度。

当前系统中没有统一的后台机制对这些异常实例进行周期性回收，`LocalGcActor` 正是为此目的而引入的垃圾回收组件。

---

## 2. 设计目标

1. **周期性扫描**：在后台以可配置间隔（默认 60 秒）自动扫描本节点所有实例。
2. **保留窗口保护**：终态异常实例在超过保留时间（默认 5 分钟）后才触发清理，避免误删正在被其他组件处理的实例。
3. **卡滞超时检测**：过渡态实例超过超时阈值（默认 10 分钟）后强制回收，防止死锁。
4. **不影响正常实例**：`RUNNING`、`SUSPENDED` 等健康状态的实例不受 GC 影响。
5. **与现有架构兼容**：遵循 LiteBus Actor 编程范式，通过依赖注入与 `InstanceCtrl`、`InstanceControlView` 解耦，支持单元测试。

---

## 3. 系统架构

### 3.1 整体位置

```
LocalSchedDriver
├── InstanceCtrl          ← 实例控制核心，提供 ForceDeleteInstance()
├── SnapCtrl              ← 快照控制
├── AbnormalProcessor     ← 异常状态上报（非回收）
├── BundleMgrActor
├── LocalGroupCtrlActor
└── LocalGcActor  [NEW]   ← 周期性 GC，清理异常实例
```

### 3.2 组件依赖关系

```
LocalGcActor
  │
  ├── InstanceControlView::GetInstances()
  │     └── 获取所有实例状态机（只读枚举）
  │
  └── InstanceCtrl::ForceDeleteInstance(instanceID)
        └── 触发实例强制删除（写操作）
```

`LocalGcActor` 仅持有上述两个依赖的共享指针，通过 `Bind` 方法注入，不直接依赖任何其他 Actor。

---

## 4. 核心数据结构

### 4.1 类定义

```cpp
class LocalGcActor : public BasisActor {
    std::string member_nodeID;
    uint32_t    member_gcIntervalMs;        // GC 扫描周期（毫秒）
    uint32_t    member_terminalRetentionMs; // 终态保留时间（毫秒）
    uint32_t    member_stuckTimeoutMs;      // 过渡态超时时间（毫秒）

    std::shared_ptr<InstanceControlView> member_instanceControlView;
    std::shared_ptr<InstanceCtrl>        member_instanceCtrl;

    // 记录每个异常实例首次被发现的时间点
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>
        member_abnormalFirstSeenTimes;
};
```

### 4.2 默认参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `gcIntervalMs` | 60,000 ms (60 s) | 每轮 GC 扫描间隔 |
| `terminalRetentionMs` | 300,000 ms (5 min) | 终态异常实例保留时间 |
| `stuckTimeoutMs` | 600,000 ms (10 min) | 过渡态卡滞超时时间 |

---

## 5. 核心流程

### 5.1 生命周期

```
litebus::Spawn(gcActor_)
       │
       ▼
   LocalGcActor::Init()
       │
       └── litebus::AsyncAfter(gcIntervalMs, &RunGcCycle)
                    │
                    ▼ (每隔 gcIntervalMs)
           LocalGcActor::RunGcCycle()
               │
               ├── CleanupAbnormalInstances()
               │
               └── litebus::AsyncAfter(gcIntervalMs, &RunGcCycle)  ← 自我重调度
```

`RunGcCycle()` 在执行完清理逻辑后，将自身再次注册到 `AsyncAfter` 队列，形成无锁的周期性调度，与 `InstanceCtrlActor::ClearRateLimiterRegularly()` 采用完全相同的模式。

### 5.2 单轮扫描逻辑（CleanupAbnormalInstances）

```
GetInstances() → 遍历所有实例
       │
       ├─ stateMachine == nullptr? → 跳过
       │
       ├─ 状态正常（非 FAILED/EXITED/EVICTED/CREATING/SCHEDULING）?
       │     └── 从 member_abnormalFirstSeenTimes 中移除（清理健康实例的残留记录）
       │
       ├─ 状态异常 & 首次发现?
       │     └── 记录当前时间到 member_abnormalFirstSeenTimes，本轮不删除
       │
       ├─ 状态异常 & 已记录 & 未超阈值? → 等待下一轮
       │
       └─ 状态异常 & 已记录 & 超过阈值?
             ├── 从 member_abnormalFirstSeenTimes 移除
             └── 调用 instanceCtrl_->ForceDeleteInstance(instanceID)

最后：清理 member_abnormalFirstSeenTimes 中已不在 GetInstances() 返回集合中的条目
（处理实例在视图外消失的情况，避免内存泄漏）
```

### 5.3 状态分类

```
实例状态
  ├── 终态异常（terminalRetentionMs 阈值）
  │     ├── FAILED
  │     ├── EXITED
  │     └── EVICTED
  │
  ├── 过渡态卡滞（stuckTimeoutMs 阈值）
  │     ├── CREATING
  │     └── SCHEDULING
  │
  └── 健康态（不处理）
        ├── RUNNING
        ├── SUSPENDED
        ├── EVICTING
        └── 其他
```

---

## 6. 集成方式

### 6.1 驱动层启动（LocalSchedDriver::Start）

```cpp
// 在 snapCtrl_ 绑定完成、instanceCtrl_ 完全就绪之后创建 GC Actor
gcActor_ = std::make_shared<LocalGcActor>(LOCAL_GC_ACTOR_NAME, param_.nodeID);
gcActor_->BindInstanceControlView(instanceCtrl_->GetInstanceControlView());
gcActor_->BindInstanceCtrl(instanceCtrl_);
litebus::Spawn(gcActor_);
```

### 6.2 停止与等待（Stop / Await）

```cpp
// Stop()
if (gcActor_) {
    litebus::Terminate(gcActor_->GetAID());
}

// Await()
if (gcActor_) {
    litebus::Await(gcActor_->GetAID());
}
```

### 6.3 新增的辅助接口

为向 `LocalGcActor` 暴露 `InstanceControlView`，在 `InstanceCtrlActor` 和 `InstanceCtrl` 中各新增了一个只读 getter：

```cpp
// InstanceCtrlActor（内部）
std::shared_ptr<InstanceControlView> GetInstanceControlView() const
{
    return instanceControlView_;
}

// InstanceCtrl（对外暴露）
std::shared_ptr<InstanceControlView> GetInstanceControlView() const
{
    ASSERT_IF_NULL(instanceCtrlActor_);
    return instanceCtrlActor_->GetInstanceControlView();
}
```

---

## 7. 变更文件清单

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `gc_actor/local_gc_actor.h` | 新增 | Actor 类定义 |
| `gc_actor/local_gc_actor.cpp` | 新增 | Actor 实现 |
| `gc_actor/CMakeLists.txt` | 新增 | CMake 编译配置 |
| `local_scheduler/BUILD.bazel` | 修改 | 新增 `"gc_actor/**/*.cpp"` glob |
| `local_scheduler/CMakeLists.txt` | 修改 | 新增 `add_subdirectory(gc_actor)` |
| `local_sched_driver.h` | 修改 | 新增 `gcActor_` 成员及头文件引用 |
| `local_sched_driver.cpp` | 修改 | 启动/停止/等待 GC Actor |
| `instance_control/instance_ctrl_actor.h` | 修改 | 新增 `GetInstanceControlView()` |
| `instance_control/instance_ctrl.h` | 修改 | 新增 `GetInstanceControlView()` 封装 |
| `common/constants/actor_name.h` | 修改 | 新增 `LOCAL_GC_ACTOR_NAME` 常量 |
| `tests/unit/.../gc_actor/local_gc_actor_test.cpp` | 新增 | 8 个单元测试用例 |
| `tests/unit/.../gc_actor/CMakeLists.txt` | 新增 | 测试 CMake 配置 |
| `tests/unit/.../local_scheduler/CMakeLists.txt` | 修改 | 注册 gc_actor 测试子目录 |

---

## 8. 单元测试

测试文件：`tests/unit/function_proxy/local_scheduler/gc_actor/local_gc_actor_test.cpp`

测试框架：Google Test + Google Mock，使用 `MockInstanceControlView`、`MockInstanceCtrl`、`MockInstanceStateMachine` 进行依赖隔离。

测试配置（加速时序验证）：

| 参数 | 测试值 |
|------|--------|
| `gcIntervalMs` | 50 ms |
| `terminalRetentionMs` | 75 ms |
| `stuckTimeoutMs` | 175 ms |

### 测试用例列表

| 编号 | 用例名称 | 验证场景 |
|------|----------|----------|
| T01 | `TerminalInstance_CleanedAfterRetentionWindow` | FAILED 实例在保留窗口后被清理 |
| T02 | `ExitedInstance_CleanedAfterRetentionWindow` | EXITED 实例在保留窗口后被清理 |
| T03 | `EvictedInstance_CleanedAfterRetentionWindow` | EVICTED 实例在保留窗口后被清理 |
| T04 | `StuckCreatingInstance_CleanedAfterTimeout` | CREATING 状态卡滞超时后被清理 |
| T05 | `StuckSchedulingInstance_CleanedAfterTimeout` | SCHEDULING 状态卡滞超时后被清理 |
| T06 | `RunningInstance_NeverCleaned` | RUNNING 实例不被清理 |
| T07 | `EmptyInstanceMap_NoCleaning` | 实例列表为空时无清理操作 |
| T08 | `MixedInstances_OnlyAbnormalCleaned` | 混合列表中仅异常实例被清理 |
| T09 | `VanishedInstance_StaleEntryPurged` | 实例从视图中消失时，内部跟踪记录被安全清理且不触发删除 |

---

## 9. 关键设计决策

### 决策 1：两轮扫描才触发清理（非即时清理）

**原因**：实例可能正处于被其他 Actor（如 `AbnormalProcessor`、`SubscriptionMgrActor`）处理的过程中，首次观察到异常状态时立即删除会引发竞态。
**方案**：首次发现记录时间，后续扫描检测是否超过阈值，给系统足够的时间完成正常的状态转换。

### 决策 2：使用 `ForceDeleteInstance` 而非普通 Kill 流程

**原因**：GC 针对的是已经处于终态的实例（无运行时需要通知），或已卡滞无法响应 Kill 信号的实例。使用强制删除可以绕过心跳、运行时交互等环节，直接从元数据层清除。

### 决策 3：`member_abnormalFirstSeenTimes` 使用内存时钟（`steady_clock`）

**原因**：保留时间判断只需要相对时长，不需要挂钟时间。`steady_clock` 单调递增，不受系统时间调整影响，更可靠。

### 决策 4：不修改 `InstanceCtrl::Create()` 签名，通过新增 getter 暴露 view

**原因**：`InstanceControlView` 在 `InstanceCtrlActor` 构造时创建并管理，对外暴露只读 getter 是最小侵入性的方案，不影响现有构造函数和工厂方法。

### 决策 5：GC Actor 独立于 Sync/Recover 流程

**原因**：`LocalSchedDriver::Sync()` 和 `Recover()` 用于状态同步与恢复，这是业务逻辑相关的操作。GC 是后台清理任务，不参与状态机同步，因此不加入 `ActorSync` / `ActorRecover` 的调用列表，只需正确的 Stop/Await。

---

## 10. 已知限制与后续优化方向

| 项目 | 说明 |
|------|------|
| 阈值不可动态调整 | 当前通过构造函数参数传入，重启后才生效；后续可通过配置中心下发动态更新 |
| 无告警上报 | GC 清理动作目前仅写日志，未接入告警/指标；后续可接入 OpenTelemetry Metrics 统计清理次数 |
| 卡滞过渡态缺乏根因诊断 | `CREATING`/`SCHEDULING` 卡滞可能有多种原因，GC 只做兜底清理；建议结合心跳检测完善诊断链路 |
| 不支持按租户/函数粒度配置 | 当前所有实例使用统一阈值；如有需要可扩展为按租户配置不同保留时间 |
