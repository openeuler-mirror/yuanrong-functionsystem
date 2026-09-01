<!--
Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
Licensed under the Apache License, Version 2.0.
See the LICENSE file in this repository for the complete license text.
-->

# FS-Large-Firecracker-Checkpoint-20260829：大容量 Firecracker Checkpoint 发布与目录所有权

| 字段 | 值 |
|---|---|
| 编号 | FS-Large-Firecracker-Checkpoint-20260829 |
| 状态 | 已实现 |
| 作者 | ChamberlainJI |
| SIG / 模块 | openYuanRong FunctionSystem / Snapshot Storage、RuntimeManager |
| 评审人 | FunctionAgent、RuntimeManager、sandboxd 与 DataSystem 维护者 |
| 批准人 | FunctionSystem 维护者（代码评审角色） |
| 创建日期 | 2026-08-29 |

## 摘要

当前实现把 sandboxd 产生的 caller-owned checkpoint 目录作为 opaque 本地 artifact，由
FunctionAgent 在物理 Checkpoint 返回后提交、Pin、按固定大小缓冲区编码/压缩，并根据
local_only、distributed_cache、distributed_only 决定保留或发布。DataSystem 使用单 final
对象的文件流接口，避免 temporary/final 双份完整 payload；OBS 仍使用 temporary 与 conditional
copy。Restore 从本地目录或远端 publication file materialize 到空目录，再通过
SandboxService.Start(StartRequest.checkpoint_info) 执行。该实现降低了 GB 级 payload 的进程
内存与分布式容量放大，但没有改变 sandboxd 内部 Firecracker capture/overlay 时序，也没有实现
early source release、增量快照或贯穿发布全过程的绝对 deadline。

## 背景与动机

旧发布路径会把 GB 级 payload 放入完整 std::string，DataSystem temporary 和 final 又会同时
持有完整对象。在高熵 Firecracker 内存场景中，压缩收益有限，双对象容量和进程 RSS 会先于
业务语义成为失败点。sandboxd 的物理输出已经是调用方提供目录，FunctionSystem 没有必要理解
memory、vmstate 或 overlay 文件名。

实现把“物理 capture 的目录所有权”“本地缓存生命周期”和“分布式 publication”分开：
sandboxd 负责 RPC 返回前写完整目录；FunctionAgent 负责返回后的目录、打包和对象发布；
DataSystem/OBS 只接收 publication file，不参与物理 VM 状态。

### 目标

- GB 级 checkpoint 的读取、压缩、上传、下载和 materialize 不构造完整内存 payload。
- DataSystem 每个逻辑 snapshot 只发布一个 complete final 对象。
- 本地目录是 sandboxd opaque artifact；FunctionSystem 仅校验类型、containment 和总 size。
- 三种 storage mode 具有明确的 owner、Pin、LRU 和删除终点。
- 物理 Checkpoint timeout 传到 sandboxd，发布结果未知可用 final Stat reconcile。
- Restore 读取期间目录被 Pin，失败 staging 可精确清理。

### 非目标

- 不改变 sandboxd 内部 Firecracker pause、native capture、overlay 冻结或 resume 顺序。
- 不实现 CaptureCheckpoint、FinalizeCheckpoint 或专用 RestoreCheckpoint RPC。
- 不实现 Firecracker incremental/diff snapshot、base chain 或 compaction。
- 不保证源 VM 停顿只等于 native capture 时间。
- 不把 SDK timeout 解释为压缩和远端发布的强制取消 deadline。
- 不让 FunctionSystem 解析 Firecracker 文件语义，也不把 payload 放入 LiteBus message。

## 方案概述

### 已实现的数据路径

~~~text
Proxy lifecycle request
  -> FunctionAgent Prepare {checkpoint_root}/{snapshotID}
  -> RuntimeManager CheckpointPlan
  -> sandboxd Checkpoint(checkpoint_dir, timeout_seconds, compress=false)
  -> FunctionAgent Commit opaque directory
  -> local_only: READY(local)
  -> distributed_*: archive/gzip -> DataSystem PutFinal or OBS temp/copy
  -> optional local retain/evict
~~~

Restore：

~~~text
local hit -> Pin directory
remote hit -> Prepare empty directory -> download publication file -> materialize -> Commit -> Pin
  -> RuntimeManager Start(checkpoint_info.checkpoint_dir)
  -> SnapStarted
  -> Unpin at attempt/runtime completion
~~~

### 存储策略

| 模式 | publication | 本地生命周期 |
|---|---|---|
| local_only | 无压缩、无远端对象 | 目录是权威 artifact；放置固定源节点 |
| distributed_cache | managed artifact gzip 后发布 | 目录保留在有界 LRU，restore pin 阻止驱逐 |
| distributed_only | managed artifact gzip 后发布 | 发布或 materialize 所需 pin 结束后删除 |

DataSystem 与 OBS 都使用 tenant-scoped logical key。DataSystem SupportsDirectFinalPut=true；
OBS 不支持 direct-final，使用 temporary/final 两阶段。

### 风险与缓解措施

| 风险 | 缓解措施 |
|---|---|
| 打包期间目录内容变化 | 安全相对路径、no-follow 打开、读取后比较 inode/size/mtime/ctime 和目录 identity |
| archive 路径穿越 | 拒绝绝对路径、父目录跳转、重复 entry、超长路径和非法类型 |
| LRU 删除 restore 输入 | Pin 计数；驱逐转为 evict-after-unpin |
| DataSystem 双 payload 容量 | PutFinal 直接从文件创建/填充/seal 一个 final 对象 |
| 上传 response 丢失 | Stat final；exact metadata 收敛为成功，缺失保留原失败 |
| OBS postcondition 服务错误被当 conflict | 当前实现保留该映射；结合 OBS/FunctionAgent 日志区分服务错误和 metadata mismatch |
| client timeout 早于发布完成 | 标记 uncertain；相同 request ID 重放/Stat，不创建另一 identity |
| Agent 重启遗留目录 | process-local recovery candidate 发现依赖内存 index；显式 Pause/Reusable snapshot ID 可验证安全目录并补建最小 pin record |

## 详细设计

### 1. 目录所有权

FunctionAgent 在 checkpoint root 中创建安全单段 snapshotID 目录，并把绝对路径经
SnapshotRuntimeRequest.checkpointDir 传给 RuntimeManager。RuntimeManager 的 CheckpointPlan
再把该目录写入 sandboxd CheckpointRequest.checkpoint_dir。

所有权边界：

1. FunctionAgent 创建空目录并保留逻辑 request。
2. sandboxd Checkpoint RPC 未返回前是唯一 writer。
3. RPC 成功返回后，FunctionAgent 递归检查并 Commit 目录到进程内 index。
4. 发布、restore、LRU 和 cleanup 均由 FunctionAgent 管理。

FunctionSystem 不要求固定 checkpoint.img。目录可以含嵌套子目录和多个普通文件；符号链接、
device、socket 等被拒绝。size 是所有 regular file size 的和，只用于缓存/观测，不是目录 digest。

LocalSnapshotDescriptor 是 process-local record，不落盘。目录存在但 record 缺失时，Prepare
拒绝接管，避免把 crash staging 误认作 READY；List 也不能在 Agent 重启后恢复 process-local
recovery candidate。不同的是，显式 Pause/Reusable restore 携带权威 snapshot ID 时，
`ValidateForRestore` 可检查既有安全、非空目录，`PinForRestore` 可补建只含 ID/size 的最小 record。

### 2. 物理 Checkpoint

CheckpointPlan 包含 sandboxID、checkpointID、caller-owned directory、TTL、timeoutSeconds 和
leaveRuntimeRunning。RuntimeManager 始终设置 compress=false，因为传输压缩由 Agent 在物理
capture 之后处理。

上层 timeoutMs 被向上取整到秒；FunctionSystem 接受 1..3,600,000 毫秒，未提供时内部计划默认
物理 timeout 为 180 秒。
sandboxd response 为空且只表示目录已经完整。当前实现没有独立 SOURCE_RELEASED wire，也不把
capture 与 overlay 打包拆成多个 RPC 阶段。

物理 Checkpoint 前，RuntimeManager 要求启动阶段 `ListAvailableRuntimes` 已初始化，所选 runtime
class 存在且 `supports_checkpoint_restore=true`。同一 capability gate 也在 restore 的
`Start(checkpoint_info)` 前执行，local hit 和 materialized remote hit 都不能绕过。

Pause 的 leave_running=false 使物理 source 在 checkpoint 后退出；Pause gate 接管该 exit。
Reusable 的 leave_running=true 在 sandboxd 完成内部 checkpoint 流程后保持源运行，Proxy 随后
调用 SnapStarted。

### 3. 目录 publication 格式

目录发布器先按安全相对路径排序，输出自描述 stream：

~~~text
directory magic
repeated { entry type, path length, mode, file size, relative path, regular-file bytes }
archive end marker
~~~

每个文件以 1 MiB 固定缓冲区读取，archive relative path 最长 4096 bytes。写入期间不跟随符号
链接，并在文件完成后重新比较 inode、size、mtime、ctime。目录 identity 在结束时复验。输出
写入同一父目录的随机 publish staging；成功后由 Publisher 持有并在完成时删除。

managed distributed artifact 固定 compress=true。目录 archive 使用 gzip level 1 流式写；当前
目录路径在 archive 完成后还会通过 InspectLocalSnapshotFile 再读取 publication file 计算
size/SHA。单一普通文件的 gzip 路径在压缩输出时同步计算 SHA 和 size，避免第二次 digest
遍历。local-only 不进入 publisher，因此保留 raw opaque directory。

### 4. DataSystem 单 final 发布

DataSystemSnapshotStorage 声明 direct-final：

1. Stat final；exact metadata 是重放成功，不同 metadata 是 conflict。
2. PutFile 从 publication file 分块创建 DataSystem object。
3. Stat final 验证 complete metadata。
4. Put 失败返回 Put error；Put 成功但 postcondition 失败当前映射为 conflict。

该路径不创建 temporary payload，也不从 temporary Get 后重新拼一个完整 final std::string。
DataSystem Get 直接把只读 buffer 分块写入 FunctionSystem staging file。

“单 final”消除 FunctionSystem 发布引入的双份完整 payload，不改变 DataSystem server 的
一致性、容量和 shared-memory 配额。

### 5. OBS 发布

OBS 路径：

1. HEAD temporary；exact metadata 重放成功。
2. multipart upload publication file。
3. HEAD temporary postcondition。
4. HEAD final；不存在时以 temporary ETag 做 source-conditional CopyObject 并替换 metadata。
5. HEAD final postcondition。

source ETag 条件不等于 destination create-if-absent；HEAD 与 Copy 之间没有原子目标锁。正常
FunctionSystem 流程依赖稳定 request/object key 和 Master phase/version CAS 保持单逻辑发布者。
postcondition HEAD 的 auth、transport、5xx 与 metadata mismatch 当前都会在上层表现为 conflict；
排障必须结合底层 HEAD 日志，不能把该状态解释为确定的 metadata 冲突。

### 6. Materialize 与目录安全

远端 Get 先写随机 staging file，并校验 object metadata、size、SHA、fsync。目录 materializer
要求目标目录为空，使用 gzip reader（未压缩 stream 也可透明读取），校验 magic、entry header、
relative path、重复 entry、类型、size 和写入 fsync。

materialize 失败时 Agent DiscardPrepared 只删除尚未 Commit 的目录。成功后 Commit record、
设置 restoreSnapshotID，并在 Start 前 Pin。Pin 从 request attempt 提升为 runtime pin；失败
attempt 或 runtime 停止时 Unpin。distributed_only 在最后一个 pin 释放后删除。

### 7. LRU 与清理

distributed_cache 以目录 regular-file 总 bytes 记账；`snapshot_local_cache_max_bytes` 默认 10 GiB。
访问、Commit、Pin 会 Touch 到 LRU 前端。超过预算时从尾部驱逐：

- 当前刚提交 snapshot 受本轮保护；
- pin 大于零的 snapshot 移到前端并跳过；
- 对 pin 中目录的显式删除只设置 evict-after-unpin；
- 删除使用目录 fd 递归 no-follow unlink。

因此该预算是软上限：当前刚提交项和所有 pin 项不能为了满足预算被删除，实际占用可暂时超过
10 GiB，待保护解除后再驱逐。

local-only 不设置 cache budget；目录由 snapshot 生命周期删除。distributed_only 在远端 READY
或 restore pin 结束后主动 evict。内部 checkpoint artifact 始终 local，由新 candidate、实例删除
或 lifecycle finalize 清理。

`PAUSE_ABORTED` 的 disposition 删除本地 artifact 和 Pause temporary key，但不删除可能已经
发布的 final key。远端删除还把 `FILE_NOT_FOUND` 和 DataSystem `BP_DATASYSTEM_ERROR` 接受为
best-effort 完成，Reusable 删除也是如此；这是避免 cleanup 阻塞状态机的取舍，也带来远端 orphan
容量风险，不能表述为精确清理保证。

### 8. Timeout 与结果未知

SDK create_snapshot 与 Pause checkpoint timeout 默认 300 秒，并向 FunctionSystem 传
`checkpointTimeoutMs`；实现接受 1..3,600,000 毫秒。该值向下形成 sandboxd 物理 Checkpoint
budget，同时分别为 Proxy→Agent checkpoint 响应与 publication 响应建立等待窗口。

这些等待窗口不是贯穿 archive、gzip、DataSystem/OBS Put 的共享 absolute deadline，也不会主动
cancel Agent 后台 publication。因此 SDK/Frontend transport 或 Proxy response wait 可能先返回
uncertain，而后台继续。Agent 保存 in-flight/local-ready/completed response；相同 request 重放，
发布失败时 Publisher Stat final 做结果收敛。

### 9. 可观测性

当前 Publisher 记录 checkpoint.compress_ms、checkpoint.published_bytes、
checkpoint.remote_put_ms、checkpoint.total_ms、direct_final 和 success。本地 store 的 size
用于 cache accounting。没有自动记录 FunctionProxy peak RSS，也没有通用
capture/source-release/package/digest 独立 metrics。

## 历史实测记录

以下数据来自 2026-08-29 的单节点 Firecracker 实验，用于解释优化动机和数量级，不是当前版本
SLO、跨硬件承诺或自动化测试结果。

实验条件：2 vCPU、4096 MiB VM、distributed_cache、DataSystem shared-memory 5235 MiB、
sandboxd compress=false、FunctionAgent gzip level 1。

### 优化前基线

| 场景/阶段 | 历史记录 |
|---|---:|
| 低熵 raw checkpoint | 4.004 GiB |
| 低熵 VM paused → resumed | 6.730 s |
| 低熵 published artifact | 37.99 MiB |
| 3072 MiB 随机常驻内存 VM paused → resumed | 28.240 s |
| 随机内存 gzip file | 64.193 s |
| 随机内存 compressed artifact | 3.028 GiB |
| DataSystem temporary put | 9.578 s |
| DataSystem final publish | 21.120 s 后 OOM |
| 历史 FunctionProxy RSS 峰值 | 约 12.25 GiB |
| 历史 client deadline | 65.869 s 时先 DeadlineExceeded |

当时 temporary 已持有约 3 GiB payload，final publish 再创建完整对象，超过 DataSystem 容量。

### 单-final 原型历史验证

| 项目 | 历史记录 |
|---|---:|
| raw checkpoint | 4,299,453,952 bytes |
| gzip publication artifact | 3,250,989,101 bytes |
| RuntimeManager capture → LOCAL_READY | 约 32 s |
| gzip + SHA 处理 | 59.458 s |
| DataSystem 单 final create/fill/set | 3.089 s |
| SDK checkpoint | 91.351 s |
| FunctionProxy VmHWM | 167,348 KiB |

该次 DataSystem access 记录只有 final miss/create/set/get，没有 temporary 完整 payload。数据只
证明该实验条件下的路径行为，不构成回归套件通过声明。

## 兼容性与限制

- sandboxd 必须支持 caller-owned checkpoint_dir 和 Start checkpoint_info；没有专用 Restore RPC。
- archive format 是 FunctionSystem 内部 publication 格式；sandboxd 只读取 materialize 后目录。
- 本地 index 不持久化；Agent restart 后 process-local recovery candidate 消失。显式 Pause/Reusable
  restore 可凭权威 snapshot ID 检查安全非空目录并补建最小 pin record，但不能恢复完整
  owner/backend facts。
- Failover/Reload 按 `localRecoveryCandidate=true` 选择“最新 local recovery candidate”，不检查
  独立 internal-only discriminator；内部 checkpoint 与 Pause artifact 都可能带该标记。
- reusable catalog record ID 由 tenant、source instance 与 request ID 派生，重放只校验
  `createRequestID`；metadata 没有持久 fingerprint，CAS loser 也不回读同 fingerprint winner。
  raw 调用方不能把同一 request ID 用于不同 name/content。
- Proxy 使用通用 `RequestSyncHelper` 等待 SnapshotRuntime 结果，没有 multi-waiter coalescing 或独立
  physical-attempt ID；Agent 仅在进程内按 request ID 复用完全相同的 managed 请求。
- Create-from-Snapshot 当前不校验 source/target tunnel shape；source template options 会覆盖 target。
  tunnel-enabled clone 必须由调用方保证模板的 enablement、WS/HTTP 控制端口与新请求一致。
- raw Snapshot/Pause 的 `timeoutSeconds` 都默认 300、范围 `1..3600` 并转发；SDK HTTP attempt 使用
  logical timeout + 30 秒，Snapshot 一次 transport attempt，Pause 使用 lifecycle retry。
- snapshot metadata 不绑定 runtimeClass/architecture，也没有源/目标字段相等 gate；RuntimeManager
  仍对 local 和 materialized remote restore 强制目标 runtime class 已由 ListAvailableRuntimes
  初始化且声明 `supports_checkpoint_restore=true`。
- 没有 Firecracker immutable-overlay capability 或 early source release；源停顿取决于 sandboxd。
- 目录 gzip 的 SHA 当前需要 archive 后额外文件遍历；单文件 gzip 在写出时同步 digest。
- DataSystem direct-final 不是跨 writer 的全局 CAS；postcondition 与 Master CAS 负责正常收敛。
- OBS 仍可能在 HEAD/Copy 窗口被外部 writer 覆盖。
- publication 不受 physical checkpoint timeout 的主动取消。
- `PAUSE_ABORTED` 可能保留已发布 final，DataSystem 删除错误可被 best-effort 接受；远端 orphan
  需要监控与运维回收。

### 测试策略

- **单元**：覆盖嵌套 regular files、空目录、非法类型/路径、4096-byte 边界、截断/重复 entry、
  gzip round trip、大文件分块、source identity 改变、digest/size、Pin/soft LRU 和显式 ID 补建。
- **集成**：覆盖 RuntimeManager capability 初始化与拒绝、Checkpoint/Start(checkpoint_info)；覆盖
  DataSystem direct-final/GetToFile/postcondition、OBS multipart/source-ETag copy/HEAD error 与 mismatch，
  以及 worker dispatch/staging cleanup。
- **端到端**：在真实 sandboxd/Firecracker 上验证三种 mode、Pause/Resume/Create、GB 级 artifact
  materialize 和两个 backend；采集 bytes、duration、对象数与 RSS，但不把历史数字作固定阈值。
- **故障注入**：覆盖 checkpoint/publication response 超时、迟到成功、final Stat result-unknown、
  DataSystem delete error、PAUSE_ABORTED 已有 final、Agent restart 和中途 archive/upload/download 失败。
- **并发**：覆盖 Pin 与 eviction 竞争、directory mutation、OBS HEAD/Copy 窗口及 publication/finalize
  交错；Reusable 同 ID 并发与 Proxy multi-waiter 是需要显式验证和记录的当前限制。

本次文档重写只有 C++ 源级回归/静态检查证据；没有可执行 C++ 测试二进制。真实
sandboxd/Firecracker 大 artifact E2E、远端 backend 故障注入，以及等待超时后后台发布完成的
result-unknown 窗口均未执行，必须在集成环境补验。

### 升级与回滚策略

DataSystem direct-final reader/writer、FunctionAgent publication format 和 restore materializer 必须
成套升级。已有 SnapshotInfo 的 backend/object key 保持权威；切换 mode 不重解释旧记录。

回滚前停止新发布并等待 Publisher/restore pin 结束。不得删除仍被 PAUSED/Reusable catalog
引用的 final object。本地目录需在 Agent index 尚存时按生命周期删除，否则由运维清理。

## 生产就绪评审

- 监控 checkpoint root、LRU、DataSystem/OBS 容量/auth、gzip CPU、publication duration 和
  uncertain response。
- 大文件工作在 ActorWorker 中，但磁盘、CPU 和 backend 带宽仍是共享资源。
- DataSystem 单-final 减少容量/RSS 放大；不消除最终对象自身容量需求。
- Firecracker source pause 优化需要 sandboxd 内部能力，不属于当前实现。
- 硬边界包括 checkpoint timeout 1..3,600,000 ms、默认 cache 10 GiB、archive relative path
  4096 bytes、1 MiB stream buffer 和 OBS 5 MiB multipart part；cache 在新提交/Pin 保护时是软预算。

## 实施历史

- 2026-08-29：记录大 Firecracker checkpoint 的历史基线与单-final 实验。
- 2026-08-31：按目标分支已实现代码重写为 opaque directory、流式发布、DataSystem 单-final、
  Pin/LRU 和实际 timeout/cleanup 契约。

## 缺点

- 自定义目录 archive 与 materializer 增加格式维护和安全校验负担。
- gzip 对高熵内存仍可能消耗较长 CPU 时间且压缩收益有限。
- process-local index 降低 Agent restart 后的 local artifact 可恢复性。

## 备选方案

- **DataSystem temporary + final copy**：容量与 RSS 放大，已由 direct-final 取代。
- **始终 local-only**：避免远端发布，但失去跨节点 Resume/Create。
- **由 sandboxd 私有保存并提供 lease**：增加跨组件 owner 协议，未采用。
- **新增 Capture/Finalize RPC**：可用于未来 early release，但当前未实现，不属于本文契约。

## 基础设施需求

- 足够容纳 opaque raw 目录与 publication staging 的 checkpoint 磁盘。
- distributed mode 所需 DataSystem 或 OBS 服务。
- 大 artifact 验证环境应提供 Firecracker、进程 RSS 和对象容量观测。
