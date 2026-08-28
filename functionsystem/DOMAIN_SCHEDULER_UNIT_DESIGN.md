# FunctionSystem UnitScheduler 设计与实现方案

日期：2026-08-26
基线：FunctionSystem `b3124451d1646db47bf93aa24569cdd277089a44`
验证环境：Linux ARM64、Bazel Debug/Release、`bazel6-linux-aarch64-v1`

## 1. 结论

本实现保留 C++ Actor 串行提交模型，通过 `enable_unit_scheduler` 在同一版本中提供两条启动路径：

| 开关 | Domain | Local |
|---|---|---|
| `false` | `PriorityScheduler + ResourceViewInfo` | `PriorityScheduler + ResourceViewInfo` |
| `true` | `UnitScheduler + immutable ScheduleSnapshot` | `UnitScheduler + ResourceViewInfo mailbox` |

Domain 是读多写少、候选规模大的场景，使用不可变快照消除调度 Actor 对 ResourceViewActor mailbox 和整棵资源树复制的依赖。Local 每次成功调度都会写入实例和资源状态，继续使用 mailbox 提供的顺序一致性屏障；它复用 UnitScheduler、队列、插件和有效资源缓存，但不构造调度快照，也不做聚合。

Primary 和 Virtual 始终是两条独立 scheduling lane。每条 lane 独立持有 ResourceView、ScheduleQueueActor、调度器、队列、上下文和快照存储，不共享运行时资源状态。

## 2. 目标与约束

目标：

- 提升 Domain 同构和 mixed burst 的吞吐。
- 资源高频更新时避免调度读取被 ResourceViewActor mailbox 阻塞。
- 保持现有 Filter/Score、priority、pending、cancel、rollback、preemption 和 GroupSchedule 语义。
- 开关关闭时完整保留当前路径，便于同版本灰度和回退。
- Local 与 Domain 复用同一个 UnitScheduler 抽象，为后续收敛调度实现保留统一入口。

约束：

- Actor 继续负责串行选择和提交，不引入并行提交冲突。
- snapshot 路径按固定请求数形成有界 round；Legacy 路径保持整轮 drain。当前默认 256 requests/round，不增加时间片状态机。
- 插件继续接收 `const ResourceUnit&`，不要求一次性重写 affinity、vector、storage 和 NUMA 插件。
- 集群级 `binpack/spread` 只作用于普通实例聚合，不改变已有 GroupSchedule placement 语义。

## 3. 总体架构

```text
Scheduler::GetResourceType(instance)
        |
        +---------------------------+
        |                           |
  PRIMARY lane                VIRTUAL lane
  ResourceViewActor           ResourceViewActor
  ScheduleQueueActor          ScheduleQueueActor
  Scheduler context           Scheduler context
  SnapshotStore               SnapshotStore

Domain, enable=true:
ResourceViewActor --publish--> shared_ptr<const ScheduleSnapshot>
ScheduleQueueActor --atomic load--> UnitScheduler --> plugins --> overlay commit

Local, enable=true:
ScheduleQueueActor --mailbox GetResourceInfo--> UnitScheduler --> plugins
                  --ALLOCATION/AddInstances--> ResourceViewActor
```

`Scheduler` 仍根据 `scheduleoption.rgroupname` 选择 Primary 或 Virtual AID。空值和 primary 进入 Primary，其他资源组进入 Virtual。相同的 Unit ID 或 request ID 出现在两条 lane 中也不会共享索引、overlay 或缓存。

## 4. Domain 不可变快照

### 4.1 数据结构

```cpp
using UnitPtr = std::shared_ptr<const ResourceUnit>;

struct ScheduleSnapshot {
    ResourceType resourceType;
    uint64_t revision;
    uint64_t publicationSequence;
    uint64_t parentPublicationSequence;
    std::string viewInitTime;
    SCHEDULER_LEVEL schedulerLevel;
    std::vector<UnitPtr> units;
    std::vector<size_t> changedUnitIndices;
    std::shared_ptr<const UnitIndex> unitIndex;
    std::shared_ptr<const RequestPlacementIndex> requestPlacements;
    std::shared_ptr<const OwnerLabelIndex> ownerLabels;
    std::shared_ptr<const MonopolyPrefilterIndex> monopolyIndex;
    std::shared_ptr<const ResourceUnit> rootSummary;
};
```

快照保持扁平 Unit 视图。Domain 不额外表达 Local 层级；多 Agent Local 上报后，最小调度粒度仍是 Unit。

### 4.2 发布机制

ResourceViewActor 是唯一写者。每次成功 mutation 完成后，Builder 根据 dirty set 构造完整 draft：

- 未变化 Unit 复用 `shared_ptr<const ResourceUnit>`。
- 变化 Unit 深拷贝为新的 const 对象。
- Unit 集合未变化时复制顶层 Unit 指针表和索引指针。
- Unit 动态增删时重建顶层结构和索引，保留仍存在 Unit 的 const 对象。
- request placement、owner label、monopoly index 和 root summary 仅在对应 dirty 位变化时重建。
- 实例 delta 只有包含 labels/kvLabels 时才重建 owner label index；plain 实例增删直接复用旧索引。
- monopoly 候选索引对实例 delta 只更新 dirty Unit 在相关 bucket 中的资格和 total，不扫描全部 Unit；结构增删仍全量重建。
- 每次本地发布递增 `publicationSequence`，并记录相对直接父版本的 `changedUnitIndices`。Scheduler 连续消费版本时只失效 K 个 dirty Unit 的有效资源缓存；若跳过中间发布，则回退为完整 Unit shared pointer 比较，避免错误复用缓存。

完成后通过 C++17 原子 shared pointer 发布：

```cpp
auto snapshot = std::atomic_load_explicit(&published_, std::memory_order_acquire);
std::atomic_store_explicit(&published_, ready, std::memory_order_release);
```

读者要么看到旧快照，要么看到完整新快照，不会看到半更新对象。旧快照由 shared pointer 生命周期自动保留到最后一个调度读者释放。

### 4.3 发布粒度与成本

当前实现对非结构 ResourceView mutation 采用“32 次 mutation 或最长 1 ms”合并发布；初始化、Unit 动态增删立即发布。批量 Add/DeleteInstances 和一条 delta 消息仍只计一次外层 mutation。Scheduler 只读取最新完整 revision，不消费中间 snapshot，但通过 request mutation journal 对账被跳过的短生命周期请求。

1000 Units、1000 次动态更新的实测平均 apply cost：

| 路径 | 平均耗时 |
|---|---:|
| legacy | 85.87 us/update |
| snapshot | 122.42 us/update |

额外约 36.5 us 主要来自顶层 1000 个 shared pointer 表复制、dirty Unit 构造和发布。Builder 已按 dirty 位复用 Domain labels、request placements、monopoly index 和 root summary，没有隐藏的整表深拷贝。该固定成本不影响数据一致性，但在 500 updates/s 下会表现为约 4%～11% 吞吐差距和更明显的 P99 波动，需要作为运行指标持续观察。

本方案不引入 page COW，接受最多 1 ms 的发布陈旧上界。实例 ADD/DELETE 的 request mutation 会先写入共享有界 journal，下一次快照发布携带 watermark；因此 publication 合并不会使 reservation 只能依赖最终 placement 猜测确认历史。

持续 5000 inflight 的真实闭环进一步验证了实例 delta 路径：每次调度成功后按目标 Local/Unit 上报 ADD 扣减，再立即 DELETE 恢复，测量区间 5000 个生命周期对应 10000 次 snapshot publication。修复实例 delta 的 label/monopoly dirty 放大后，ADD+DELETE report P50 从约 540 us 降至三轮中位数 161～186 us；所有轮次最终实例数为 0。

## 5. 调度轮次与 overlay

ScheduleQueueActor 在每个 round 开始时原子加载一次快照，最多消费 256 个请求后让出 Actor 并开始下一轮。AggregatedItem 超过本轮剩余额度时按请求边界拆分；GroupSchedule 保持原子语义，不在组内切分。

`RoundAllocationContext` 保存尚未进入 ResourceView 快照的本调度器 reservation：

```text
effective allocatable = snapshot allocatable - reservation overlay
```

每个 snapshot lane 独立持有容量 65536 的 request mutation journal。ResourceViewActor 在 ADD/DELETE 时追加 `{sequence, requestID}`，发布的 snapshot 记录同一 journal 指针和 watermark。Round 开始只读取 `(lastConsumed, watermark]`，删除 reservation 创建之后发生过 mutation 的同 requestID 项；因此即使 ADD 与 DELETE 两个中间 snapshot 都被跳过，reservation 仍能精确收敛。overflow 会计数并回退到 placement 全量对账。

补充对账规则：

- 新快照的 `requestPlacements` 已包含 request：删除对应 reservation。
- 尚未包含：保留 reservation 并继续扣减。
- Unit 已不存在：删除本地 reservation；后续业务处理仍由现有外层流程负责。
- reservation 到期：从 overlay 删除，避免无界增长。

对账只在 round 边界执行一次，不在每个插件或每个请求中遍历全部 reservation。Primary/Virtual 各自持有独立 context 和 journal。

reservation journal 只使用一个以 requestID 为 key 的 map；正常幂等查询为 O(1)。value 不重复保存 requestID，无标签请求不构造空 label map。重复 instanceID 和 rollback 属于异常路径，可线性扫描 journal，避免为低频操作维护第二个常驻索引。过期堆同样保存 requestID，并以 value 中的 `expireAtMs` 排除旧 heap entry。

Local 正常路径通过 mailbox 获取最新 ResourceViewInfo，因此继续使用现有消息顺序保证 `AddInstances` 在下一次视图读取前被处理。当前 Local 启动配置不启用快照，也不写 Domain reservation journal；未来如果 Local 改为直接读取快照，需要重新设计 ALLOCATION 的确认与回收，而不是隐式复用当前 journal。

## 6. 插件执行与有效资源缓存

现有插件接口和执行顺序不变：

```text
PreFilter -> Filter plugins -> Score plugins
```

Framework 在进入一个候选 Unit 时准备一次：

```text
effective = unit.allocatable - context.allocated[unit]
```

同一候选的 Default、heterogeneous、disk 和 NUMA Filter/Score 共享该结果。缓存键同时依赖 Unit 对象身份；Allocate、rollback、dirty Unit 或新 ResourceView round 会失效对应缓存。直接调用单个插件时每次重新计算，保持旧接口允许调用方直接修改 `PreAllocatedContext::allocated` 的行为。

复杂插件仍生成自己的 vector allocation：

- heterogeneous：型号、卡数、physical ID、HBM/stream 等约束；
- storage：disk 容量和 mount allocation；
- NUMA：bind resource、bind strategy 和节点内分配。

公共缓存只去除重复的标量 protobuf 构造，不改变具体 vector 分配结果。

## 7. 聚合与 placement

### 7.1 聚合签名

Domain 只有调度语义相同的请求才能进入同一聚合项。签名覆盖：

- priority、完整资源请求和 sched policy；
- resource/instance/inner affinity 与 anti-affinity；
- resource selector；
- labels、kvLabels；
- NUMA bind extension；
- affinity context 中影响跨层调度的 maxScore 和 isTopDownScheduling。

当前内置插件白名单之外的插件关闭语义聚合。GroupSchedule 不进入普通实例聚合；target、range
和 rgroup 已由不同调度控制器或队列完成路由，不进入聚合签名。

内置 shared 纯标量请求走轻量二进制签名，直接追加资源、静态 resource affinity、resource
selector 和必要 affinity context 字段，不再构造、归一化并序列化完整请求 protobuf。
`receivedTimestamp` 只影响排队年龄，不影响 Filter/Score，因此不进入签名；scheduled score/result
等扫描缓存也不进入签名。出现 instance/inner affinity、NUMA、vector/storage 或其他复杂资源时，
回退到完整确定性签名。

### 7.2 标量快路径

以下请求可共享一次候选扫描，并按候选可用容量连续生成结果：

- 内置插件链；
- shared CPU/Memory 以及其他纯标量资源；
- 可包含只读取 Unit 静态标签的 resource affinity 和 resource selector；
- 无 instance/inner affinity、NUMA、labels/kvLabels；
- 聚合签名一致。

该路径不对每个请求重复执行选中 Unit 的整套 Filter/Score。overlay 每次提交后仍扣减资源，候选容量用尽时刷新候选。

instance/inner affinity、heterogeneous、storage 和 NUMA 请求在选择后重评估目标 Unit，以保持
vector、动态 label 和拓扑语义正确。resource affinity 和 resource selector 只读取 Unit 静态标签，
不受本轮 overlay 影响，因此不需要逐请求重评估。

聚合扫描的停止条件按候选 `availableForRequest` 累计容量判断，不再要求“请求 N 个就找到 N 个 Unit”。例如 256 个请求、每 Unit 可容纳 20 个时，只需容量覆盖 256，同时候选数不低于 `scheduleRelaxed`。`scheduleRelaxed=-1` 仍扫描全部候选，单请求行为不变。

### 7.3 binpack/spread

`schedule_placement_policy` 是集群级配置，不新增请求字段：

- `binpack`：继续从当前高优候选分配，容量或分数变化后刷新候选。
- `spread`：每个候选分配一个请求后轮转到下一候选，提供近似均匀分布。

实例弱反亲和由现有 affinity 插件和完整签名优先表达。vector 请求不会进入纯标量免复核快路径，避免为了 spread 绕过 accelerator 约束。

## 8. Local 复用策略

Local 开启新路径时：

- 创建 `UnitScheduler`，Primary/Virtual 分别实例化。
- 固定 `NO_AGGREGATE_STRATEGY`。
- 保持 `ALLOCATION`、`allocatedPromise`、`AddInstances/DeleteInstances` 和现有插件行为。
- 使用 ResourceViewInfo mailbox 读取，不发布 ScheduleSnapshot。

原因是 Local 的实例集合是高频写状态。若每次 AddInstances 都把含全部实例的 dirty Unit 发布为新 protobuf，单 Unit 连续分配会产生累计深拷贝。采用 mailbox backend 后，Local 1/8/64 Units 的端到端吞吐与 legacy 保持同一水平，同时保留统一调度器入口。

## 9. 功能兼容与双路径门禁

开关关闭时不创建调度快照，继续使用当前 PriorityScheduler。开关开启时才创建 UnitScheduler；Domain ResourceViewActor 才启用快照。

必须保持的协议和不变量：

- 状态码、错误分类和 ScheduleResult 字段语义；
- CPU/Memory、monopoly、heterogeneous、storage、NUMA 资源守恒；
- hard/weak affinity、anti-affinity、selector 和 Unit status；
- priority、FIFO/fairness、pending、cancel、timeout、rollback 和 preemption；
- Local allocation promise 与 ResourceView 实际扣减；
- Domain owner 路由和 already-scheduled 幂等；
- Primary/Virtual 路由、快照和 overlay 隔离；
- GroupSchedule 的现有 PACK/SPREAD 语义。

新增联合回归覆盖同一个 Scheduler 同时挂载 Primary/Virtual 两个 ScheduleQueueActor，分别加载自己的快照，并验证 `rgroupname` 只路由到对应 lane。

## 10. 性能结果

所有表均为 1000 请求、1000 Domain Units 或标注的 Local Units、三次独立运行的中位数；成功率均为 100%，新路径 invalid placement 为 0。

### 10.1 Domain 同构 CPU/Memory

| 路径 | no aggregate | relaxed aggregate | 聚合提升 |
|---|---:|---:|---:|
| legacy | 2469 QPS | 24346 QPS | 9.9x |
| Unit snapshot | 2462 QPS | 21197 QPS | 8.6x |

Unit no-aggregate 与 legacy 持平；新聚合比自身逐请求提高约 8.6 倍。相对 legacy relaxed 仍低 12.9%。已验证将 reservation 操作改为 context 虚接口不能缩小差距，因此撤回该实验；剩余阶段主要在逐请求 `IsScheduled/PreAllocated/reservation payload` 和安全语义，而不是 snapshot load 或 RTTI 本身。

### 10.2 Domain mixed

mixed 包含 plain、required affinity、weak anti-affinity、NPU、storage 和 NUMA：

| 路径 | no aggregate | relaxed aggregate | invalid placement |
|---|---:|---:|---:|
| legacy | 604 QPS | 非稳定有效对照 | 317–374 |
| Unit snapshot | 573 QPS | 4346 QPS | 0 |

Unit mixed 聚合相对自身 no-aggregate 提高约 7.6 倍。删除重复的 affinity plugin context protobuf 赋值后，mixed 聚合吞吐有明确改善。legacy 聚合的宽签名产生错误落点，因此其聚合吞吐不作为有效对照。

### 10.3 高频资源更新，500 reports/s

| scheduleRelaxed | legacy QPS / P99 | Unit QPS / P99 |
|---:|---:|---:|
| -1 | 94.38 / 12258.8 us | 86.49 / 14363.9 us |
| 1 | 9628.95 / 134.0 us | 8560.98 / 284.3 us |
| 32 | 2230.27 / 524.7 us | 2144.26 / 790.2 us |

没有出现 mailbox 队列持续增长，但 snapshot 的固定发布成本仍会反映到 P99；特别是 `scheduleRelaxed=32` 时吞吐只低约 3.9%，P99 中位数高约 50.6%。因此新路径应先灰度并观测，不能表述为高频更新下全面优于 legacy。

### 10.4 持续 5000 inflight 真实结果增删

拓扑为 1000 Domain Units、5000 固定 inflight；每个完成结果依次执行真实 ADD 扣减、DELETE 恢复并补入新请求。该用例推翻了初版验收结论：此前 Legacy 使用 binpack、Unit 使用 spread，不能直接比较；统一 binpack 后，Unit 的 lifecycle QPS 仍受 snapshot publication 写路径限制。

更关键的是，5000 warmup + 5000 measured 期间 Unit 发布 10000 次 snapshot，但 no-aggregate 只开始 1 个 schedule round，relaxed 只开始 2 个；最终物理实例为 0 时，overlay 仍分别保留约 10031/15000 条 reservation。扩展到 20000 measured 后只有 15000 成功，5000 因虚假预占失败，pending reservation 达 20000。

上述两个门槛已通过 256-request/10-ms 双边界 round 和 request mutation journal
实现。空队列不创建 round，短暂 refill gap 可复用仍在边界内的 snapshot；非结构
snapshot 进一步按 32 次 mutation 或 1 ms 合并发布。普通 ADD/DELETE 对账按
requestID 增量减去 overlay，全量 rebuild 只用于初始化、不连续结构变化或 journal
overflow；prepared resource move 进 reservation，避免第二次 protobuf 拷贝。

最终实现重新编译后的复跑中，5000 measured 时 Unit no-aggregate 为 2613 QPS、
relaxed 为 7501 QPS，均 5000/5000 成功；Legacy 分别为 2536/8625 QPS，
即无聚合高约 3.0%，relaxed 仍有约 13.0% 的固定成本。20000 measured 时 Legacy
no-aggregate 仅成功 15000 次并降到 966 QPS，Unit no-aggregate 仍为
20000/20000、2521 QPS（2.61 倍）。Unit relaxed 为 7637 QPS，相对 Legacy
8743 QPS 保留约 12.7% trade-off。Unit 两条路径在持续测试中都只有初始化的 1 次
overlay rebuild。

Unit-only 50000 measured 产生约 110000 条 mutation，超过 journal 65536 容量后仍无 overflow：no-aggregate/relaxed 均 50000/50000 成功、final instances 0，分别为 2573/7722 QPS。说明 overlay 已持续增量收敛，不依赖 TTL 或 ring 容量碰巧覆盖全程。

### 10.5 journal/reservation 极致化后的最终成本

`RequestMutationJournal` 使用预分配 ring，通过 sequence 直接定位待读区间；读取复杂度为
O(delta)，不再随保留历史长度增长。latest watermark 使用 acquire/release atomic，
`RoundAllocationContext` 复用 mutation buffer。

普通 CPU/Memory overlay 使用原地 scalar 加减，并跳过空 label overlay 与重复的
instance-to-Unit map/set 写入。vector、storage、heterogeneous、NUMA 仍保持现有插件和
protobuf 运算路径；标量快路条件不满足时立即回退，功能边界不扩大。

20000 条历史中消费最新 64 条的 journal 微测从 82.36 us/轮降到 2.11 us/轮；5000 条
reservation 的 record 从 5.24 us/条降到 2.63 us/条，精确回收从 3.21 us/条降到
0.861 us/条。微测旧/新执行封装不同，仅用于热点判断。

同一最终二进制的 legacy/Unit 同进程 A/B 中，homogeneous no-aggregate 为 2798/2883
QPS，relaxed 为 35724/37239 QPS；mixed no-aggregate 为 686/726 QPS。mixed relaxed
的 legacy 有 334 个错误落点，Unit 为 7088 QPS 且错误落点 0。

真实 5000 inflight ADD+DELETE 中，legacy/Unit no-aggregate 为 2537/2658 QPS，relaxed
为 8258/7397 QPS；四项 report P50 均约 92～94 us，Unit 两种模式 journal overflow
均为 0。同步 update apply 三次中位数为 legacy 76.1 us、Unit 86.4 us，额外成本约
13.5%，已从上一轮约 43% 明显收窄。

publication 剩余约 19～32 us/版，继续下降需要 Page COW/稳定槽位或延迟发布。前者与
当前接受的扁平 Unit 顺序、动态增删取舍冲突，后者降低视图新鲜度；当前不引入。

### 10.6 Local 冲突 retry 的即时收敛

Local 返回资源冲突后，Domain 不再把相同 requestID 直接作为普通请求重新入队。重试使用
同一条 Actor 入队消息携带 `conflictedUnitID`，在入队前执行：

- Legacy 按 requestID 回滚本轮保存的精确 resource/label delta；
- Unit 按 requestID 删除 reservation 和对应 overlay；
- 本次 retry 不进入聚合，Filter/Score 重新执行并排除上次失败 Unit；
- 下一次普通请求不继承该排除信息，原有 requestID 幂等语义不变。

该设计没有新增 confirm/ack mailbox 消息；TTL 仅保留为异常兜底，不再承担正常冲突回收。
当没有其他可行 Unit 时，本次 retry 正常返回资源不足，由外部既有业务重试框架决定后续行为。

等价新鲜度复测要求 Legacy 和 Unit 同样最多处理 256 个请求后让出 Actor。5000 inflight
真实 ADD+DELETE 三轮中位数显示，Unit no-aggregate QPS +24.5%、P99 -19.3%，relaxed
QPS +34.1%、P99 -25.9%。因此 bounded round 对 Legacy 不只是长尾保护：它会迫使 Legacy
重新通过 ResourceView mailbox 获取并深拷贝视图，而 Unit 只同步加载最新不可变指针；
这正是 Snapshot 方案在相同新鲜度约束下的核心收益。

### 10.7 Local ALLOCATION

结果包含 `allocatedPromise` 完成和 ResourceView 实际扣减：

| Units | legacy | Unit mailbox |
|---:|---:|---:|
| 1 | 11129 QPS | 11485 QPS |
| 8 | 11280 QPS | 11162 QPS |
| 64 | 10859 QPS | 11041 QPS |

Local 新路径未引入稳定吞吐回退。

## 11. 验证状态

已通过的受影响 suite：

- `ScheduleSnapshotTest`
- `PreAllocatedContextTest`
- `UnitSchedulerTest`
- `PrioritySchedulerTest`
- `AggregatedQueueTest`
- `ScheduleTest` / `ScheduleSnapshotPathTest`
- `FrameworkImplTest`
- Default、heterogeneous、disk、NUMA、label affinity、resource selector Filter/Scorer suites
- `DomainSchedulerDriverTest` / `DomainSchedLauncherTest`
- `InstanceCtrlTest`
- `CommonFlagsTest`
- `DomainSchedulerCurrentPathBenchmark`

高级调度额外增加 Legacy/Unit snapshot 全路径 A/B，覆盖 StrictPack、普通 gang、Range
`min/max/step` 对齐以及整组失败回滚。测试发现并修复了 Range 多余结果回滚的零基下标偏一问题；
修复后两条路径的返回码、结果数量和 placement 语义一致，Unit Range reservation 从 7 收敛为
6，失败 gang 收敛为 0。既有 `SchedulerPerformerTest`、`PrioritySchedulerTest`、`ScheduleTest`
也全部通过。Group item 即使队列配置为 `relaxed` 仍不进入普通实例聚合，已有 GroupSchedule
placement 语义未被集群级 binpack/spread 改写。

storage + NPU 组合的退出阶段回归已固定为默认 benchmark UT，并在 legacy/Unit 两条路径各执行 400 请求。BUILD 统一使用共享 logs/metrics SDK，避免测试二进制同时静态链接并动态加载同一日志实现导致重复析构。

全量单二进制 `*.*` 入口存在两个测试基础设施限制：integration 用例要求外部 `BIN_PATH`，unit 全集合在 300 秒总超时内只运行到 MetaStore suites。调度相关 suite 已按测试集拆分执行并通过；Release 同二进制 A/B 已完成，真实集群 E2E 仍应作为合入流水线门禁。

## 12. 运行指标与验收门槛

建议增加以下指标：

- snapshot revision、publish count、publish latency、dirty Unit count；
- ResourceView applied revision 与 Scheduler consumed revision/age；
- reservation count、expiry count、overlay rebuild count；
- aggregation bucket size、shared scan count、selected Unit reevaluation count；
- 按请求类型和 placement policy 的 QPS、P50、P99、invalid placement。

合入门槛：

- 开关关闭的现有 UT 全部保持原行为；
- 开关开启的 Primary/Virtual、Local/Domain、插件和资源守恒回归通过；
- homogeneous 和 mixed 新路径 invalid placement 为 0；
- Local 1/8/64 Units 无稳定回退；
- Domain 聚合相对新路径 no-aggregate 有明确提升；
- 资源更新 P99 不出现数量级劣化或持续增长。

## 13. 主要实现文件

- `src/common/resource_view/schedule_snapshot.{h,cpp}`
- `src/common/resource_view/resource_view_actor.{h,cpp}`
- `src/common/schedule_decision/scheduler/unit_scheduler.{h,cpp}`
- `src/common/schedule_plugin/common/preallocated_context.h`
- `src/common/scheduler_framework/framework/framework_impl.{h,cpp}`
- `src/common/schedule_decision/performer/aggregated_schedule_performer.{h,cpp}`
- `src/common/schedule_decision/queue/aggregated_queue.{h,cpp}`
- `src/domain_scheduler/startup/domain_scheduler_driver.cpp`
- `src/function_proxy/local_scheduler/instance_control/instance_ctrl.cpp`
- `tests/unit/common/schedule_decision/domain_scheduler_current_path_benchmark_test.cpp`

## 附录 A：最终测试报告

最终测试报告作为本设计的验收附件独立保留：

[Domain 调度最终测试报告](../docs/domain-scheduler-benchmark-report.md)

独立附件集中记录最终性能数字、测试口径、缓存边界和高级调度回归；本设计只保留
合入判断所需摘要，避免两份完整数据随代码演进产生漂移。

Release 同二进制三轮中位数的核心结果如下：

| 场景 | Legacy | Unit | Unit 变化 |
|---|---:|---:|---:|
| homogeneous no-aggregate | 18138 QPS | 18515 QPS | +2.1% |
| homogeneous relaxed | 112235 QPS | 113303 QPS | +1.0% |
| 持续 ADD+DELETE no-aggregate | 10122 QPS / P99 504.9 ms | 13729 QPS / P99 369.9 ms | QPS +35.6%，P99 -26.7% |
| 持续 ADD+DELETE relaxed | 8995 QPS / P99 576.1 ms | 13942 QPS / P99 373.0 ms | QPS +55.0%，P99 -35.3% |
| mixed no-aggregate | 4569 QPS | 4767 QPS | +4.3%，两者错误落点 0 |
| 1000 次资源更新 apply | 32.48 us/次 | 36.48 us/次 | 写侧 +12.3% |

高级调度验收：StrictPack、普通 gang、Range step 和整组失败回滚的 Legacy/Unit 全路径 A/B
均通过；Range 多余 reservation 下标问题已修复并由默认 UT 固化。
