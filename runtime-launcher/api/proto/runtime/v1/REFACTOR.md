# RuntimeLauncher Proto 重构设计与变更记录

## 概述

`runtime_launcher.proto` 从面向函数运行时（Function Runtime）的接口设计重构为**面向 Sandbox** 的抽象，同时增强了扩展性并消除了重复定义。

| 维度 | 重构前 | 重构后 |
|------|--------|--------|
| 包名 | `runtime.v1` | `sandbox.v1` |
| 服务名 | `RuntimeLauncher` | `SandboxService` |
| 核心抽象 | `FunctionRuntime` | `SandboxTemplate` + `SandboxConfig` |
| RPC 数量 | 8 | 10 |

---

## 1. Service 与 RPC 变更

### 重命名的 RPC

| 重构前 | 重构后 | 说明 |
|--------|--------|------|
| `Start` | `CreateSandbox` | 统一 sandbox 前缀命名 |
| `Delete` | `DeleteSandbox` | |
| `Wait` | `WaitSandbox` | |
| `Register` | `RegisterTemplates` | 改为面向 Template 的抽象 |
| `Unregister` | `UnregisterTemplates` | 字段名改为 `template_ids` |
| `GetRegistered` | `ListTemplates` | 支持按 labels 过滤 |

### 合并的 RPC

| 重构前 | 重构后 | 说明 |
|--------|--------|------|
| `Start` + `Restore` | `CreateSandbox` | Restore 合并为 `CreateSandboxRequest` 中的可选字段 `RestoreOptions`。设置则走恢复流程，不设置则走正常创建 |

### 新增的 RPC

| RPC | 用途 |
|-----|------|
| `StatusSandbox` | 查询 sandbox 运行状态、时间戳、扩展详情 |
| `GetTemplate` | 按 `template_id` 查询单个模板 |

### 删除的 RPC

| RPC | 原因 |
|-----|------|
| `Restore` | 合并到 `CreateSandbox`，通过 `RestoreOptions` 区分 |

---

## 2. 枚举变更

### `RootfsSrcType` — 未变更

```protobuf
enum RootfsSrcType {
  S3    = 0;
  IMAGE = 1;
  LOCAL = 2;
}
```

### `NetworkMode` — 新增

替代原来的 `string network` 字段，提供类型安全的网络模式选择。

```protobuf
enum NetworkMode {
  SANDBOX = 0; // 独立 sandbox 网络命名空间 (默认)
  HOST    = 1; // 共享宿主机网络命名空间
  NONE    = 2; // 无网络
}
```

### `SandboxState` — 新增

描述 sandbox 生命周期状态，用于 `StatusSandboxResponse` 和 `WaitSandboxResponse`。

```protobuf
enum SandboxState {
  SANDBOX_STATE_UNSPECIFIED  = 0;
  SANDBOX_STATE_CREATING     = 1;
  SANDBOX_STATE_RUNNING      = 2;
  SANDBOX_STATE_STOPPED      = 3;
  SANDBOX_STATE_FAILED       = 4;
  SANDBOX_STATE_CHECKPOINTED = 5;
}
```

---

## 3. 消息变更

### `FunctionRuntime` → `SandboxTemplate`

核心模板/蓝图定义，重构后可通过 `template_id` 注册和引用。

| 序号 | 重构前 (`FunctionRuntime`) | 重构后 (`SandboxTemplate`) | 变更说明 |
|------|---------------------------|---------------------------|----------|
| 1 | `string id` | `string template_id` | 重命名，语义更明确 |
| 2 | `string sandbox` | `string name` | 改为可读名称，便于管理界面展示 |
| 3 | — | `string sandbox_type` | **新增**：sandbox 类型（runc, microvm, wasm） |
| 4 | `RootfsConfig rootfs` | `RootfsConfig rootfs` | 无变更 |
| 5 | `bool makeSeed` | `bool make_seed` | snake_case 规范化 |
| 6 | `repeated string command` | `repeated string command` | 无变更 |
| 7 | `map<string,string> runtimeEnvs` | `map<string,string> runtime_envs` | snake_case 规范化 |
| 8 | `string cwd` | `string cwd` | 无变更 |
| 9 | `repeated Mount mounts` | `repeated Mount mounts` | 无变更 |
| 10 | — | `map<string,string> labels` | **新增**：用于调度、筛选、分组 |
| 11 | — | `map<string,string> annotations` | **新增**：携带业务自定义元数据 |

### `StartRequest` / `RestoreRequest` → `SandboxConfig` + `CreateSandboxRequest`

原来的 `StartRequest` 和 `RestoreRequest` 几乎完全重复（各 11 个字段）。重构后拆分为：

- **`SandboxConfig`** — 共享的实例级运行配置
- **`CreateSandboxRequest`** — 请求包装，含可选 `RestoreOptions`

#### `SandboxConfig`（新增）

| 序号 | 字段 | 类型 | 来源 |
|------|------|------|------|
| 1 | `template_id` | `string`（oneof） | 引用已注册模板 |
| 2 | `inline_template` | `SandboxTemplate`（oneof） | 内联模板，用于一次性 sandbox |
| 3 | `extra_mounts` | `repeated Mount` | 原 `StartRequest.mounts` |
| 4 | `resources` | `ResourceSpec` | 原 `map<string,double> resources` → 结构化 |
| 5 | `user_envs` | `map<string,string>` | 原 `StartRequest.userEnvs` |
| 6 | `io` | `IORedirect` | 原 `stdout` + `stderr` → 结构化 |
| 7 | `network` | `NetworkMode` | 原 `string network` → 枚举 |
| 8 | `ports` | `repeated PortSpec` | 原 `repeated string ports` → 结构化 |
| 9 | `extensions` | `map<string,string>` | 原 `string extraConfig`（JSON）→ 结构化 KV |

#### `CreateSandboxRequest`

| 序号 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 1 | `config` | `SandboxConfig` | 完整 sandbox 配置 |
| 2 | `trace_id` | `string` | 分布式链路追踪 |
| 3 | `restore` | `RestoreOptions` | 可选：设置则走恢复流程，不设置则冷启动 |

#### `RestoreOptions`（新增）

| 序号 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 1 | `ckpt_dir` | `string` | Checkpoint 目录路径 |
| 2 | `options` | `map<string,string>` | 可扩展恢复策略选项 |

### 新增的消息

| 消息 | 用途 |
|------|------|
| `IORedirect` | 结构化 stdout/stderr 重定向（替代原来的裸字符串字段） |
| `ResourceSpec` | 结构化资源需求：`cpu_millicore`、`memory_mb`、`disk_mb`、`extended` |
| `PortSpec` | 结构化端口映射（替代 `repeated string ports`） |
| `RestoreOptions` | Checkpoint 恢复配置 |
| `StatusSandboxRequest/Response` | Sandbox 状态查询，含时间戳和扩展详情 |
| `GetTemplateRequest/Response` | 按 `template_id` 查询单个模板 |

### 删除的消息

| 消息 | 原因 |
|------|------|
| `FunctionRuntime` | 由 `SandboxTemplate` 替代 |
| `StartRequest` | 由 `CreateSandboxRequest` + `SandboxConfig` 替代 |
| `StartResponse` | 由 `CreateSandboxResponse` 替代 |
| `RestoreRequest` | 合并到 `CreateSandboxRequest`（通过 `RestoreOptions`） |
| `RestoreResponse` | 合并到 `CreateSandboxResponse` |
| `NormalResponse` | 由各 RPC 独立响应消息替代，统一 `code` + `message` 格式 |
| `RegisterRequest` | 由 `RegisterTemplatesRequest` 替代 |
| `UnregisterRequest` | 由 `UnregisterTemplatesRequest` 替代 |
| `GetRegisteredRequest/Response` | 由 `ListTemplatesRequest/Response` 替代 |

### 响应格式统一

所有响应消息统一使用 `int32 code` + `string message` 模式。

| 重构前 | 重构后 |
|--------|--------|
| `bool success`（`NormalResponse`、`CheckpointResponse`） | `int32 code`（0 = 成功） |
| `int32 code`（`StartResponse`） | `int32 code`（0 = 成功） |
| 空的 `DeleteResponse` | `DeleteSandboxResponse`，含 `code` + `message` |

---

## 4. 字段级改进

### 类型安全

| 重构前 | 重构后 | 改进 |
|--------|--------|------|
| `string network`（"sandbox", "host", "none"） | `NetworkMode network` | 枚举防止非法值 |
| `repeated string ports` | `repeated PortSpec ports` | 结构化 container_port / host_port / protocol |
| `map<string,double> resources` | `ResourceSpec resources` | 具名字段 + 可扩展 `extended` map |
| `string stdout` + `string stderr` | `IORedirect io` | 分组管理，含扩展 `extensions` map |
| `string extraConfig`（JSON 字符串） | `map<string,string> extensions` | 结构化 KV，无需 JSON 解析 |

### 命名规范

所有字段名统一为 proto3 `snake_case` 风格：

| 重构前 | 重构后 |
|--------|--------|
| `makeSeed` | `make_seed` |
| `runtimeEnvs` | `runtime_envs` |
| `userEnvs` | `user_envs` |
| `funcRuntime` | `config` / `template` |
| `Endpoint`（S3Config） | `endpoint` |
| `Bucket` | `bucket` |
| `AccessKeyID` | `access_key_id` |
| `AccessKeySecret` | `access_key_secret` |

### 扩展性点位

| 位置 | 机制 | 用途 |
|------|------|------|
| `SandboxTemplate.labels` | `map<string,string>` | 调度、筛选、分组 |
| `SandboxTemplate.annotations` | `map<string,string>` | 业务自定义元数据 |
| `SandboxConfig.extensions` | `map<string,string>` | 实例级自定义配置 |
| `ResourceSpec.extended` | `map<string,double>` | GPU、带宽等资源维度 |
| `IORedirect.extensions` | `map<string,string>` | 未来 streaming sink 扩展 |
| `CheckpointRequest.options` | `map<string,string>` | 增量 checkpoint、加密等 |
| `RestoreOptions.options` | `map<string,string>` | 恢复策略选项 |
| `StatusSandboxResponse.details` | `map<string,string>` | 运行时状态元数据 |
| `ListTemplatesRequest.label_selector` | `map<string,string>` | 模板过滤 |

---

## 5. 架构图

```
CreateSandboxRequest
├── config (SandboxConfig)
│   ├── template_source (oneof)
│   │   ├── template_id ──────► 引用已注册的 SandboxTemplate
│   │   └── inline_template ──► 内联 SandboxTemplate 定义
│   ├── extra_mounts           实例级额外挂载
│   ├── resources (ResourceSpec)
│   ├── user_envs              用户自定义环境变量
│   ├── io (IORedirect)        IO 重定向
│   ├── network (NetworkMode)  网络模式
│   ├── ports (PortSpec[])     端口映射
│   └── extensions             可扩展配置
├── trace_id                   链路追踪 ID
└── restore (RestoreOptions)   ← 可选
    ├── ckpt_dir               checkpoint 目录
    └── options                恢复策略选项
```

- `restore` 未设置 → 冷启动（从模板创建新 sandbox）
- `restore` 已设置 → 热恢复（基于模板配置 + 从 checkpoint 恢复 sandbox）
