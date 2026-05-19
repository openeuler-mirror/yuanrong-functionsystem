# Sandbox 特性概述

## 1. 什么是 Sandbox

Sandbox 是 functionsystem 中**容器级函数执行后端**，通过 `containerd` + sandbox-shim 管理函数实例的完整生命周期。

functionsystem 有两条执行路径，由 `EXECUTOR_TYPE` 枚举区分：

| 类型 | Executor | 隔离级别 | 适用场景 |
|------|----------|----------|----------|
| `RUNTIME` | `RuntimeExecutor` | 进程级 | Python / Java / Go 等语言 runtime |
| `CONTAINER` | `SandboxExecutor` | 容器级 | 需要独立容器隔离、端口暴露或 Checkpoint 能力 |

Sandbox 路径由 `RuntimeManager::FindExecutor(EXECUTOR_TYPE::CONTAINER)` 按需创建，对上层完全透明——调用方统一使用 `Executor` 接口。

---

## 2. 核心能力一览

| 能力 | 说明 |
|------|------|
| **容器生命周期管理** | 通过 `runtime.v1.RuntimeLauncher` gRPC（UDS）下发 Start / Delete |
| **三条启动路径** | Normal（普通）/ WarmUp（预热）/ Restore（从 Checkpoint 恢复） |
| **端口转发** | `PortManager` 分配宿主机端口，供 Traefik 动态路由 |
| **Checkpoint / Restore** | 拍快照、上传远端、下载恢复，支持冷启动加速 |
| **WarmUp 预热池** | 向 sandbox-shim 注册预热容器，用于秒级实例复用 |
| **失败安全启动** | `SandboxStartGuard` RAII 保护，失败时自动回滚所有状态 |
| **gRPC 自动重连** | 每 5s 检测连接，断连后自动重试，不阻塞正常请求 |

---

## 3. 架构位置

```
FunctionAgent
     │  StartInstance / StopInstance / SnapshotRuntime
     ▼
RuntimeManager
     │  FindExecutor(EXECUTOR_TYPE::CONTAINER)
     ▼
SandboxExecutorProxy  ← 跨线程入口（litebus actor 消息桥）
     │  litebus::Async
     ▼
SandboxExecutor  ← actor 线程，持有所有可变状态
     │
     ├── RuntimeStateManager  (状态：sandboxes / warmUpMap / inProgressStarts / pendingDeletes)
     ├── SandboxRequestBuilder (proto 组装：rootfs / mount / envs / port / resources)
     ├── CheckpointOrchestrator (ckpt 生命周期：拍照 / 下载 / AddRef / ReleaseRef)
     ├── PortManager (Singleton，host port 分配与释放)
     │
     │  gRPC over UDS (CONTAINER_EP)
     ▼
containerd / sandbox-shim
     │
     ▼
容器实例（用户函数）
```

---

## 4. 三条启动路径

`StartInstance` 根据请求字段分发到三条互斥路径：

```
StartInstance(request)
    │
    ├── warmupType != NONE            → StartWarmUp
    │     DoRegisterWarmUp (gRPC Register，不分配端口，不启动容器)
    │     → 写入 warmUpMap_
    │
    ├── snapshotInfo.checkpointID != "" → StartBySnapshot
    │     DownloadForRestore → AddRef → Build(with ckpt_dir) → DoStart
    │     → 激活容器（从内存快照恢复）
    │
    └── (otherwise)                   → StartNormal
          ParseForwardPorts → PortManager::RequestPorts
          → SandboxRequestBuilder::Build → DoStart
          → 激活容器（全量冷启动）
```

### Normal 启动
标准容器冷启动。从 `deployOptions["network"]` 解析端口转发配置，由 `PortManager` 分配宿主机端口，并将端口映射 JSON 写入响应，供 Traefik HTTP Provider 注册路由。

### WarmUp 启动
预热实例注册。通过 gRPC `Register` 接口将 `FunctionRuntime` proto 写入 sandbox-shim 的预热池，**不启动容器、不分配端口**。后续 `StartNormal` 可直接从预热池复用，缩短首次响应延迟。停止时通过 `UnregisterWarmUp` 从预热池移除。

### Restore 启动（Checkpoint 恢复）
1. `CkptFileManager::DownloadCheckpoint` 将 checkpoint 文件拉取到本地。
2. `CheckpointOrchestrator::AddRef` 增加引用计数，防止文件被提前清理。
3. `SandboxRequestBuilder::Build` 填入 `ckpt_dir`，其余字段与 Normal 相同。
4. 启动失败时执行补偿 `ReleaseRef`，保证引用计数不泄漏。

---

## 5. 关键设计决策

### SandboxStartGuard — 失败安全 RAII
所有三条启动路径共用一个 `SandboxStartGuard`，构造时向 `RuntimeStateManager` 注册 in-progress 状态，**Commit 前任意中间失败都触发 guard 析构**，自动清理 `sandboxes_`、`inProgressStarts_`、port 映射等全部状态，无需手工补偿。

### RuntimeStateManager — 状态内聚
所有沙箱状态集中在一个管理器，避免跨 map 一致性问题：
- `sandboxes_`：活跃实例
- `inProgressStarts_`：启动去重（相同 runtimeID 并发返回同一 Future）
- `pendingDeletes_`：启动中收到 Stop 请求时标记，Start 完成后立即清理
- `warmUpMap_`：预热实例（与 `sandboxes_` 互斥）

### 单线程 actor 模型
`SandboxExecutor` 和 `RuntimeStateManager` 均运行在同一 litebus actor 线程，**所有状态访问无锁**。`SandboxExecutorProxy` 是唯一的跨线程入口，将调用路由回 executor actor 线程。

---

## 6. 端口转发与 Traefik 集成

```json
// deployOptions["network"]
{
  "portForwardings": [
    { "port": 8080, "protocol": "tcp" }
  ]
}
```

启动时 `PortManager` 分配宿主机端口（如 `40001`），编码为 `"tcp:40001:8080"` 写入：
- `StartRequest.ports`（发往 sandbox-shim）
- `RuntimeStateManager::portMappingsJson`（内部状态）
- `StartInstanceResponse.port`（返回给 FunctionAgent）

FunctionAgent 上报端口后，`InstanceManagerActor` 将路由条目注册到 `TraefikRouteCache`，Traefik 通过 `GET /traefik/config` 拉取生效。

---

## 7. 关键配置

| 变量 / 字段 | 来源 | 说明 |
|-------------|------|------|
| `CONTAINER_EP` | 进程环境变量 | containerd UDS 端点（必填） |
| `deployOptions["rootfs"]` | StartInstanceRequest | 自定义 rootfs，支持 `s3` / `image` / `local` |
| `deployOptions["network"]` | StartInstanceRequest | 端口转发配置（JSON） |
| `deployOptions["mounts"]` | StartInstanceRequest | 额外挂载点（JSON 数组） |
| `deployOptions["extra_config"]` | StartInstanceRequest | 透传给 sandbox-shim 的扩展配置 |
| `YR_LANGUAGE` | 容器环境变量 | entryfile.sh 选择 Python 版本 |
| `YR_RT_WORKING_DIR` | 容器环境变量 | bootstrap 挂载目录（`/__yuanrong/`） |

---

## 8. 代码导航

```
functionsystem/src/runtime_manager/
├── executor/
│   ├── executor.h                        # Executor 接口（SandboxExecutor 实现此接口）
│   └── sandbox/
│       ├── sandbox_executor.h/cpp        # 核心编排层：路由、链式 Future、gRPC 调用
│       ├── runtime_state_manager.h/cpp   # 沙箱状态唯一来源
│       ├── sandbox_request_builder.h/cpp # 无状态 proto 组装
│       └── checkpoint_orchestrator.h/cpp # Checkpoint 完整生命周期
├── port/
│   └── port_manager.h/cpp                # host port 分配（Singleton）
├── ckpt/
│   ├── ckpt_file_manager.h/cpp           # checkpoint 文件管理接口
│   └── ckpt_file_manager_actor.h/cpp     # actor 实现（下载 / 上传 / 引用计数）
└── manager/
    └── runtime_manager.cpp               # FindExecutor：CONTAINER 类型创建 SandboxExecutor

functionsystem/tests/unit/runtime_manager/executor/sandbox/
├── sandbox_executor_test.cpp             # SandboxStartGuard 行为
├── sandbox_executor_network_test.cpp     # ParseForwardPorts 各种输入
└── sandbox_request_builder_test.cpp      # proto 端到端构造
```

---

## 9. 延伸阅读

| 文档 | 内容 |
|------|------|
| [sandbox-runtime-design.md](./sandbox-runtime-design.md) | 本文档各节的详细实现说明（状态机、数据结构、全量流程图）|
| [../traefik-http-provider-design.md](../traefik-http-provider-design.md) | Traefik HTTP Provider：路由表生成与动态配置拉取 |
| [../traefik-leader-forward-design.md](../traefik-leader-forward-design.md) | Traefik Leader Forward：非 Leader 节点请求转发 |
