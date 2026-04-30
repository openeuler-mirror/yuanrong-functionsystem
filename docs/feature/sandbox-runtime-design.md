# Sandbox Runtime 设计

## 背景与动机

在引入 Sandbox 特性之前，functionsystem 通过 `ContainerExecutor` 管理函数实例的生命周期，
该方案强依赖 `containerd` 的原生容器 API。

Sandbox 模式在此基础上引入了更轻量的隔离单元，核心变化有两点：

1. **独立的 RuntimeLauncher gRPC 接口**：通过 `CONTAINER_EP` 环境变量指向 containerd
   的 UDS 端点，所有容器操作均走 `runtime.v1.RuntimeLauncher` gRPC stub，与上层解耦。
2. **三条启动路径**：普通启动（Normal）、预热启动（WarmUp）、从 Checkpoint 恢复（Restore），
   三者共用同一 `SandboxExecutor` 入口，通过请求字段路由。

## 架构总览

```
                    StartInstance / StopInstance
                           │
                ┌──────────▼──────────────┐
                │    SandboxExecutorProxy  │
                │  (actor-message bridge)  │
                └──────────┬──────────────┘
                           │  litebus::Async
                ┌──────────▼──────────────────────────────────┐
                │             SandboxExecutor                  │
                │                                              │
                │  ┌────────────────┐  ┌───────────────────┐  │
                │  │  RouteStart()  │  │   StopInstance()  │  │
                │  │  ┌──────────┐  │  │  ┌─────────────┐  │  │
                │  │  │ Normal   │  │  │  │  Sandbox    │  │  │
                │  │  │ WarmUp   │  │  │  │  WarmUp     │  │  │
                │  │  │ Restore  │  │  │  └─────────────┘  │  │
                │  │  └──────────┘  │  └───────────────────┘  │
                │  └────────────────┘                          │
                │                                              │
                │  ┌─────────────────────────────────────────┐ │
                │  │         RuntimeStateManager             │ │
                │  │  sandboxes_ / inProgressStarts_        │ │
                │  │  pendingDeletes_ / warmUpMap_          │ │
                │  └─────────────────────────────────────────┘ │
                │                                              │
                │  ┌───────────────────┐  ┌────────────────┐  │
                │  │ SandboxReqBuilder │  │ CkptOrchestrat │  │
                │  │ (proto builder)   │  │ (ckpt lifecycle│  │
                │  └───────────────────┘  └────────────────┘  │
                └──────────────────────┬──────────────────────┘
                                       │  gRPC (UDS)
                        ┌──────────────▼─────────────┐
                        │  runtime.v1.RuntimeLauncher │
                        │  (containerd / sandbox-shim)│
                        └────────────────────────────┘
```

### 组件职责

| 组件 | 职责 |
|------|------|
| `SandboxExecutor` | 生命周期编排：路由 Start / Stop，链接 Future 回调，委托 gRPC 调用 |
| `SandboxExecutorProxy` | actor 消息桥：将公开调用包装为 `litebus::Async`，是调用方的 drop-in 替换 |
| `RuntimeStateManager` | 沙箱状态的唯一来源：注册/注销、start 去重、pending-delete、warm-up 映射 |
| `SandboxRequestBuilder` | 无状态 proto 构造：将 `SandboxStartParams` 组装为 `StartRequest` |
| `CheckpointOrchestrator` | Checkpoint 完整生命周期：拍快照、下载、AddRef、ReleaseRef |
| `SandboxStartGuard` | RAII 启动保护：保证失败时状态自动回滚，不留泄漏 |
| `PortManager` | 端口分配：为每个 sandbox 分配 host port，并在 Stop 时释放 |

## 核心设计

### 1. 三条启动路径

`StartInstance` 根据请求字段路由到三条路径，互不重叠：

```
StartInstance(request)
    │
    ├── warmupType != NONE           → StartWarmUp
    │       ↓
    │   DoRegisterWarmUp (gRPC Register)
    │       ↓
    │   OnWarmUpRegistered → Commit guard → return success
    │
    ├── snapshotInfo.checkpointID != ""  → StartBySnapshot
    │       ↓
    │   CheckpointOrchestrator::DownloadForRestore
    │       ↓
    │   CheckpointOrchestrator::AddRef
    │       ↓
    │   SandboxRequestBuilder::Build (with ckpt_dir)
    │       ↓
    │   DoStart (gRPC Start)
    │       ↓
    │   OnRestoreDone → Commit guard → return success
    │
    └── (otherwise)                  → StartNormal
            ↓
        ParseForwardPorts → PortManager::RequestPorts
            ↓
        SandboxRequestBuilder::Build
            ↓
        DoStart (gRPC Start)
            ↓
        OnStartDone → Commit guard → return success
```

#### Normal 启动

1. 从 `deployOptions["network"]` 解析 `portForwardings`，调用 `PortManager` 分配 host port。
2. 端口映射编码为 `"protocol:hostPort:containerPort"` 格式，写入 `StartRequest.ports` 和 `RuntimeStateManager`。
3. 调用 `SandboxRequestBuilder::Build` 组装 proto。
4. gRPC `Start` 成功后，更新 `sandboxID`，提交 guard，返回响应（携带 port 映射 JSON）。

#### WarmUp 启动

WarmUp（预热/seed）使用独立的 gRPC `Register` 接口，**不分配 host port**，不调用 `DoStart`：

1. 构造 `RegisterRequest`，填入 bootstrap 挂载和运行时环境变量。
2. 调用 `DoRegisterWarmUp` 注册到 sandbox-shim 的预热池。
3. 成功后将 `FunctionRuntime` proto 存入 `RuntimeStateManager::warmUpMap_`。

停止时通过 `UnregisterWarmUp` 从预热池中移除，不走 `TerminateSandbox` 路径。

#### Restore 启动（从 Checkpoint 恢复）

1. 通过 `CkptFileManager` 下载 checkpoint 到本地路径。
2. 调用 `CheckpointOrchestrator::AddRef` 增加引用计数，防止文件被提前清理。
3. `SandboxRequestBuilder::Build` 填入 `ckpt_dir`，其余字段与 Normal 相同。
4. 启动失败时，补偿调用 `ReleaseRef` 恢复引用计数。

### 2. SandboxStartGuard — 失败安全的 RAII 保护

```
SandboxStartGuard guard(mgr_, runtimeID, future);
     │
     ├── 构造时：mgr_.MarkStartInProgress(runtimeID, future)
     │
     ├── Commit() 调用时：committed_ = true; mgr_.MarkStartDone(runtimeID)
     │
     └── 析构时：
           if (!committed_) → mgr_.Unregister(runtimeID)  // 回滚所有状态
```

**作用**：任何启动路径中的中间失败（gRPC 错误、proto 构造失败等）都会触发 guard 析构，
自动清除 `sandboxes_`、`inProgressStarts_`、`portMappings` 等所有相关状态，
消除手动清理的遗漏风险。

### 3. RuntimeStateManager — 状态内聚

所有沙箱状态集中在一个管理器，避免跨多个 map 维护一致性：

```cpp
struct SandboxInfo {
    std::string runtimeID;
    std::string sandboxID;
    std::string checkpointID;
    std::string portMappingsJson;
    messages::RuntimeInstanceInfo instanceInfo;
};
```

四个内部数据结构：

| 字段 | 类型 | 作用 |
|------|------|------|
| `sandboxes_` | `unordered_map<runtimeID, SandboxInfo>` | 核心运行时状态 |
| `inProgressStarts_` | `unordered_map<runtimeID, Future>` | StartInstance 去重：相同 runtimeID 并发时返回同一 Future |
| `pendingDeletes_` | `unordered_set<runtimeID>` | Start 进行中收到 Stop 请求：标记，等 Start 完成后立即清理 |
| `warmUpMap_` | `unordered_map<runtimeID, FunctionRuntime>` | 预热实例的 proto，用于 WarmUp 路径 |

**invariant**：`warmUpMap_` 中的 runtimeID 不会同时出现在 `sandboxes_` 中，两者互斥。

### 4. SandboxRequestBuilder — 无状态 proto 构造

`SandboxRequestBuilder` 是纯函数风格的 proto 组装器，不持有可变状态，仅持有 `CommandBuilder` 的常量引用。

输入：

```cpp
struct SandboxStartParams {
    shared_ptr<StartInstanceRequest> request;
    CommandArgs  cmdArgs;        // 来自 CommandBuilder::BuildArgs
    Envs         envs;           // 来自 GenerateEnvs
    string       runtimeID;
    string       checkpointID;   // 非空 = restore 路径
    vector<string> portMappings; // "protocol:hostPort:containerPort"
};
```

构造顺序（`BuildStart`）：

1. `BuildRootfs` — 从 `deployOptions["rootfs"]` 或 container.rootfsConfig 解析 rootfs
2. `ApplyExtraConfig` — 从 `deployOptions["extra_config"]` 设置 extraConfig
3. `ApplyPortMappings` — 填入端口映射列表
4. `ApplyBootstrapMount` — 挂载 bootstrap 工作根目录到 `/__yuanrong/`
5. `ApplyCommands` — 合并 bootstrapConfig entrypoint/cmd 与 cmdArgs.args
6. `ApplyCodeMounts` — 挂载函数代码目录（bind 或 erofs）
7. `ApplyResources` — 设置 CPU / Memory 资源限制（cgroup 级）
8. `ApplyEnvsAndLogs` — 合并环境变量，设置 stdout/stderr 日志路径

#### Rootfs 来源优先级

```
deployOptions["rootfs"] 存在
  └── 解析 JSON → 支持 type: s3 / image / local
deployOptions["rootfs"] 不存在
  └── 直接复用 container.rootfsConfig（由 FunctionAgent 填充）
```

#### Bootstrap 挂载规则

`bootstrapConfig.root` 非空 **且** `deployOptions["rootfs"]` 存在时，
将宿主机上的 `bootstrapConfig.root` 挂载到容器内 `/__yuanrong/`，
并将 `workingRoot = "/__yuanrong/"` 写入 `YR_RT_WORKING_DIR` 环境变量，
使运行时在挂载目录内寻找 entryfile。

### 5. CheckpointOrchestrator — Checkpoint 生命周期

```
TakeSnapshot
    │
    DoCheckpoint (gRPC Checkpoint)
        │
    OnCheckpointDone → CkptFileManager::RegisterCheckpoint (上传/注册)
        │
    OnRegisterDone → stateManager_.UpdateCheckpoint(runtimeID, checkpointID)

DownloadForRestore
    │
    CkptFileManager::DownloadCheckpoint → 返回本地路径

AddRef
    │
    CkptFileManager::AddReference → stateManager_.SetCheckpointID

ReleaseRef
    │
    stateManager_.GetCheckpointID → CkptFileManager::RemoveReference
        │
    stateManager_.ClearCheckpointID
```

**引用计数不变量**：每次 `AddRef` 必须对应一次 `RemoveReference`——
在 Stop 时由 `StopSandbox` 主动调用，在 Restore 失败时由补偿逻辑调用，
保证 checkpoint 存储无引用泄漏。

### 6. 端口转发

端口转发配置从 `deployOptions["network"]` JSON 中读取：

```json
{
  "portForwardings": [
    { "port": 8080, "protocol": "tcp" },
    { "port": 9090 }
  ]
}
```

解析后为每个 sandbox 申请相应数量的 host port：

```
ParseForwardPorts(networkJson)
    │
    ├── 解析 portForwardings 数组
    └── 返回 vector<PortForwardConfig>{containerPort, protocol}

PortManager::RequestPorts(runtimeID, count)
    └── 返回 vector<uint32_t> hostPorts

编码为 "tcp:40001:8080" 格式，写入:
    - StartRequest.ports
    - RuntimeStateManager portMappingsJson
    - StartInstanceResponse.port（JSON 数组）
```

Stop 时 `PortManager::ReleasePorts(runtimeID)` 归还 host port。

### 7. 连接管理

SandboxExecutor 在初始化时通过 `CONTAINER_EP` 环境变量读取 containerd UDS 端点，
创建 gRPC 长连接。如果连接断开，会进入重连循环：

```
CheckConnectivity (每 5s 触发)
    │
    containerd_.IsConnected()? ──yes──► skip
                                │
                               no
                                │
                        ReconnectContainerd
                                │
                    ActorWorker::AsyncWork (阻塞等待重连)
                                │
                        OnReconnectContainerd
                                │
                    IsConnected()? ──yes──► reconnecting_ = false, log
                                │
                               no
                                │
                        AsyncAfter(5s, ReconnectContainerd)
```

`reconnecting_` 标志防止并发重连。

## 状态机

```
 Register()
    │
    ▼
[registered / start-in-progress]
    │
    ├── Commit() on success ──► [active] ──► Unregister() on stop ──► [removed]
    │
    └── guard dtor (failure) ──► [removed]  (all maps cleaned atomically)


 WarmUp 独立路径:
    RegisterWarmUp() ──► [warmUpMap_] ──► UnregisterWarmUp() ──► [removed]
```

## 线程安全说明

`SandboxExecutor` 及 `RuntimeStateManager` 均运行在同一 actor 线程上（litebus actor 模型），
**所有状态访问均为单线程**，不需要额外锁。跨 actor 的调用通过 `litebus::Async`
和 `litebus::Defer` 保证回调投递到正确线程。

`SandboxExecutorProxy` 是唯一的跨线程调用入口，其职责是将所有调用路由回 executor 的 actor 线程。

## 关键环境变量与配置

| 变量 / 字段 | 来源 | 作用 |
|-------------|------|------|
| `CONTAINER_EP` | 进程环境变量 | containerd UDS 端点地址 |
| `YR_LANGUAGE` | 写入容器 userenvs | entryfile.sh 选择 Python 版本 |
| `YR_RT_WORKING_DIR` | 写入容器 runtimeenvs | 运行时 working root 路径 |
| `YR_ONLY_STDOUT` | 写入容器 userenvs | 启用纯 stdout 日志重定向 |
| `YR_ENV_FILE` | 宿主机环境变量，透传 | WarmUp 路径的环境文件路径 |
| `YR_SEED_FILE` | 宿主机环境变量，透传 | WarmUp seed 就绪标志文件 |
| `deployOptions["rootfs"]` | StartInstanceRequest | 自定义 rootfs（JSON），支持 s3/image/local |
| `deployOptions["network"]` | StartInstanceRequest | 端口转发配置（JSON） |
| `deployOptions["mounts"]` | StartInstanceRequest | 额外挂载点（JSON 数组） |
| `deployOptions["extra_config"]` | StartInstanceRequest | 传递给 sandbox-shim 的扩展配置 |

## 测试覆盖

### sandbox_executor_test.cpp

| 测试 | 覆盖场景 |
|------|----------|
| `GuardWithoutCommitUnregistersOnDestruct` | guard 析构未 Commit → 状态回滚 |
| `GuardAfterCommitKeepsEntryInMgr` | guard Commit 后析构 → 状态保留 |
| `MultipleGuardsAreIndependent` | 多 guard 互不干扰，各自独立回滚或保留 |

### sandbox_executor_network_test.cpp

覆盖 `ParseForwardPorts` 的各种输入场景（空 JSON、缺字段、非法端口、协议转小写等）。

### sandbox_request_builder_test.cpp

覆盖 `SandboxRequestBuilder::Build` 的端到端构造逻辑：
rootfs 解析（s3/image/local/默认）、挂载、环境变量、资源设置、checkpoint 路径填充等。

## 与其他组件的关系

| 依赖方向 | 关系描述 |
|----------|----------|
| `SandboxExecutor` → `RuntimeLauncher` gRPC | 通过 gRPC stub 下发 Start/Delete/Register/Unregister/Checkpoint |
| `SandboxExecutor` → `PortManager` | 申请/释放 host port |
| `SandboxExecutor` → `CkptFileManager` | 通过 `CheckpointOrchestrator` 间接使用，管理 checkpoint 文件 |
| `SandboxExecutor` → `HealthCheck` | 实例健康探测（由外部注入） |
| `FunctionProxy` → `SandboxExecutorProxy` | 通过 `Executor` 接口统一调用，透明替换 `ContainerExecutor` |
| Traefik 路由 | 端口映射 JSON 由 `SandboxExecutor` 生成后，由上层 `InstanceManagerActor` 消费并注册路由 |

更完整的 SDK 侧（yuanrong/Python）设计见 `yuanrong/docs/features/sandbox-implementation.md`。
Checkpoint / Snapshot 协议设计见 `yuanrong/docs/features/snapshot-checkpoint.md`。
Traefik 路由集成设计见 `docs/traefik-http-provider-design.md`。
