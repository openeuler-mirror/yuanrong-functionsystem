# Domain 调度最终测试报告

## 1. 最终结论

Unit snapshot 调度路径已达到合入验证条件：

- 与完全未优化的 master 做同为 Linux ARM64 Bazel Debug 的直接对比，最终 Unit 路径的同构无聚合、同构聚合和 mixed 无聚合吞吐分别达到 `3.96x`、`2.26x` 和 `5.45x`；500 reports/s 下吞吐达到 `3.69x`、P99 降低 `80.3%`。
- 静态调度吞吐与优化后的 Legacy 路径保持同一量级：同构无聚合 `+1.8%`、同构聚合 `-1.6%`、mixed 无聚合 `+10.5%`。
- 在相同 256-request 资源新鲜度约束、持续 5000 inflight 且每次调度均真实执行 ADD+DELETE 时，无聚合吞吐提升 `17.7%`、P99 降低 `17.6%`；聚合吞吐提升 `26.1%`、P99 降低 `23.5%`。
- 500 reports/s 短队列场景基本持平：QPS `-0.4%`、P99 `+1.0%`。
- 冲突 retry 风暴中 QPS 提升 `26.6%`，P50/P99 分别降低 `25.2%`/`30.1%`；旧 reservation 会被立即清理，不依赖 TTL 老化。
- mixed 聚合在 Unit 路径保持 `1000/1000` 成功且错误落点为 `0`；Legacy 聚合存在错误落点，因此不比较其吞吐。
- snapshot 实时发布的写侧代价为单次资源更新 apply 增加 `2.4%`。
- Local 1/8/64 Units 的差异为 `+2.2%/-0.6%/-0.1%`，未观察到稳定性能回归。
- Primary/Virtual 双资源视图、Local mailbox、Group、Range、affinity、弱反亲和、NPU、storage、NUMA 等功能回归通过。

最终实现保留同一版本内的双路径开关：关闭走优化后的 Legacy 路径，开启走 Unit snapshot 路径。

## 2. 测试口径

### 2.1 最终 A/B

- Legacy 与 Unit 使用同一个 Release 二进制、同一个进程顺序执行。
- 平台：Linux ARM64；Bazel Release；常规组独立执行 3 轮并取中位数。对首轮方差较大的同构静态和 500 reports/s 组，额外 warmup 1 轮后执行 7 轮并取中位数。
- 拓扑：1000 个扁平 Units，cluster-level binpack。
- 静态及 mixed 场景：每轮 1000 个请求。
- 持续场景：5000 固定 inflight、5000 warmup、5000 measured；每次成功后真实 ADD 并立即 DELETE。
- Legacy 与 Unit 均使用相同的 256-request freshness bound。
- 最终二进制 SHA-256：`8d78ae746a6c05edb3455b87d7634872e36207829d48c5f7f2663ca092f3ac26`。
- 构建继续使用 `bazel6-linux-aarch64-v1` cache namespace 和独立 Release profile；未 clean 或复用原 UT output/action 目录。

### 2.2 完全未优化 master 与最终 Unit 的直接对比

该基线来自 FunctionSystem `b3124451d1646db47bf93aa24569cdd277089a44` 的原始生产调度代码，仅移植 benchmark 和测试构建适配。它不包含插件计算去重、`PreAllocatedContext` 缓存、Unit snapshot、mutation journal 或新聚合语义。

原始 master 与最终 Unit 均使用 Linux ARM64 Bazel Debug、同一 workload，每项 3 轮取中位数。因此本表用于直接计算总体优化收益；第 3 节再用 Release 同二进制 A/B 隔离最终 Unit 相对优化后 Legacy 的收益。

| 场景 | 完全未优化 master | 最终 Unit | 总体变化 / 正确性 |
|---|---:|---:|---|
| homogeneous no-aggregate | 745 QPS | 2,947 QPS | `3.96x`；两者错误落点 0 |
| homogeneous relaxed | 16,336 QPS | 36,843 QPS | `2.26x`；两者错误落点 0 |
| mixed no-aggregate | 136 QPS | 742 QPS | `5.45x`；两者错误落点 0 |
| mixed relaxed | 719 QPS / 错误落点中位数 279 | 7,515 QPS / 错误落点 0 | master 语义不正确，不计算吞吐提升 |
| 500 reports/s 短队列 | 664 QPS / P99 2,353.8 us | 2,450 QPS / P99 463.1 us | QPS `3.69x`，P99 `-80.3%` |
| 单次同步资源 update apply | 110.248 us | 85.50 us | `-22.4%` |

## 3. 最终 Release 性能结果

| 场景 | Legacy 中位数 | Unit 中位数 | Unit 相对变化 / 正确性 |
|---|---:|---:|---|
| homogeneous no-aggregate | 18,299 QPS | 18,630 QPS | `+1.8%`；两者 1000/1000、错误落点 0 |
| homogeneous relaxed | 107,537 QPS | 105,857 QPS | `-1.6%`；两者 1000/1000、错误落点 0 |
| mixed no-aggregate | 3,192 QPS | 3,529 QPS | `+10.5%`；两者 1000/1000、错误落点 0 |
| mixed relaxed | 错误落点中位数 321 | 28,119 QPS | Unit 1000/1000、错误落点 0；Legacy QPS 不比较 |
| 500 reports/s 短队列 | 11,012 QPS / P99 120.3 us | 10,965 QPS / P99 121.5 us | QPS `-0.4%`，P99 `+1.0%`，基本持平 |
| 持续 ADD+DELETE no-aggregate | 6,647 QPS / P99 787.8 ms | 7,825 QPS / P99 649.5 ms | QPS `+17.7%`，P99 `-17.6%` |
| 持续 ADD+DELETE relaxed | 6,428 QPS / P99 820.3 ms | 8,104 QPS / P99 627.2 ms | QPS `+26.1%`，P99 `-23.5%` |
| 冲突 retry 风暴 | 3,347 QPS / P50 146.8 us / P99 218.2 us | 4,239 QPS / P50 109.8 us / P99 152.5 us | QPS `+26.6%`，P50 `-25.2%`，P99 `-30.1%` |
| 1000 次资源更新 apply | 56.28 us/次 | 57.62 us/次 | Unit 写侧 `+2.4%` |

持续场景四组结果均为：

- 5000/5000 成功；
- ADD/DELETE 各 5000；
- 错误落点、report failure、journal overflow 均为 0；
- 最终物理实例为 0。

## 4. Local 最终回归结果

Local 不做聚合。以下为 Linux ARM64 Bazel Debug、同一二进制三轮中位数：

| Local Units | 完全未优化 master | 优化后 Legacy | Unit mailbox | Unit/master | Unit/Legacy |
|---:|---:|---:|---:|---:|---:|
| 1 | 6,839 QPS | 10,953.1 QPS | 11,190.0 QPS | `+63.6%` | `+2.2%` |
| 8 | 6,633 QPS | 11,091.6 QPS | 11,019.6 QPS | `+66.1%` | `-0.6%` |
| 64 | 6,779 QPS | 10,968.3 QPS | 10,961.7 QPS | `+61.7%` | `-0.1%` |

1/8/64 Units 未观察到稳定回归。Local 路径不记录 Domain reservation journal，也不构造 Domain snapshot。

## 5. 功能正确性结果

### 5.1 调度能力

| 能力 | 最终结果 |
|---|---|
| Primary / Virtual 双资源视图 | 独立发布、跨 lane 拒绝、动态 Unit 增删和视图切换 UT 通过 |
| CPU/Memory | Legacy 与 Unit 静态及持续场景均成功，错误落点 0 |
| required affinity | Legacy 与 Unit 无聚合结果一致，错误落点 0 |
| weak anti-affinity | Unit 聚合及无聚合回归通过 |
| NPU vector | 分配和 teardown 回归通过 |
| storage vector | 分配和 teardown 回归通过 |
| NUMA | filter/scorer 与 mixed 回归通过 |
| Local mailbox | 1/8/64 Units 正确性与性能回归通过 |
| 冲突 retry | 1000/1000 避开失败 Unit；旧 reservation 立即删除，最终 reservation 不翻倍 |

### 5.2 Group / Range

测试从 `Scheduler::GroupScheduleDecision` 入口分别驱动 Legacy 与 Unit snapshot 完整链路：

| 场景 | Legacy | Unit | 验证结果 |
|---|---|---|---|
| StrictPack，3 请求、3 Units | 成功 | 成功 | 3 个结果落在同一 Unit |
| 普通 gang，3 请求、每 Unit 容量 1 | 成功 | 成功 | 结果覆盖 3 个 Units |
| Range `min=5,max=10,step=2`，实际容量 7 | 成功 | 成功 | 保留 6 个结果；Unit reservation 收敛为 6 |
| 非 Range gang，3 请求、总容量 2 | `RESOURCE_NOT_ENOUGH` | 同左 | 整组失败；Unit reservation 回滚为 0 |

### 5.3 通过的回归套件

- `ScheduleSnapshotTest`
- `PreAllocatedContextTest`
- `UnitSchedulerTest`
- `PrioritySchedulerTest`
- `AggregatedQueueTest`
- `ScheduleTest`
- `SchedulerPerformerTest`
- `FrameworkImplTest`
- `CommonFlagsTest`
- `DomainInstanceCtrlTest`
- `DomainSchedulerDriverTest`
- `InstanceCtrlTest`
- `AdvancedGroupAndRangeSemanticsMatchLegacyAndUnitSnapshot`

本轮合并过滤器共执行 361 个测试，其中 346 个非禁用用例全部通过，15 个用例保持禁用，失败与错误均为 0。

## 6. 验收判断

最终结果支持以双路径开关方式合入：

- Unit 路径在静态读场景相对 Legacy 无显著吞吐回归（全部落在既定 `±5%` 持平区间），在持续真实资源回写及冲突 retry 场景显著改善吞吐和端到端长尾。
- mixed 聚合、Primary/Virtual、Local、Group/Range 和 vector/storage/NUMA 功能保持正确。
- 明确代价是资源写侧 apply 增加 `2.4%`，应在生产灰度中持续观测 snapshot publication、applied revision lag、journal overflow 和调度 P99。
