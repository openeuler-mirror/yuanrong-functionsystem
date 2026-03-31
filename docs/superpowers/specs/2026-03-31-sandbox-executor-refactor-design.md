# Design: Sandbox Executor & CommandBuilder Refactor

**Scope:** `runtime_manager/executor/container_executor.*`, `runtime_manager/config/command_builder.*`  
**Goal:** 接口语义清晰、职责单一、消除冗余设计

---

## 背景与问题

当前代码主要问题：

| 文件 | 行数 | 核心问题 |
|---|---|---|
| `container_executor.cpp` | 1522 | 编排 / proto 拼装 / 状态管理 / checkpoint / 连接管理混在一起 |
| `container_executor.h` | 330 | 暴露 30+ 私有方法，内部实现细节泄漏到头文件 |
| `command_builder.cpp` | 603 | Java 有 4 个几乎重复的函数；`GetBuildArgs` 有 `chdir` 副作用；Python 路径修改 request 入参 |
| `command_builder.h` | 118 | 函数指针 map 复杂，语言扩展性差 |

---

## 新文件结构

```
runtime_manager/
├── config/
│   ├── build.h                            # 不变 (RuntimeConfig, Envs)
│   ├── command_builder.h/.cpp             # 重构：薄 dispatcher，持有 LanguageStrategy map
│   │
│   └── language/                          # 新文件夹（语言策略）
│       ├── language_strategy.h            # LanguageCommandStrategy 纯接口
│       ├── cpp_strategy.h/.cpp
│       ├── go_strategy.h/.cpp
│       ├── python_strategy.h/.cpp
│       ├── java_strategy.h/.cpp
│       └── nodejs_strategy.h/.cpp         # 包含 POSIX custom
│
└── executor/
    ├── executor.h                         # 不变（Executor 基类）
    ├── container_executor.h/.cpp          # 保留，转发到 SandboxExecutor（渐进迁移）
    │
    └── sandbox/                           # 新文件夹
        ├── sandbox_executor.h/.cpp        # 薄 Future 编排层
        ├── runtime_state_manager.h/.cpp   # 封装所有 map 的增删查
        ├── sandbox_request_builder.h/.cpp # 拼装 gRPC StartRequest / RestoreRequest
        └── checkpoint_orchestrator.h/.cpp # snapshot / restore 完整生命周期
```

---

## 各模块接口设计

### 1. `LanguageCommandStrategy`（`config/language/language_strategy.h`）

```cpp
struct CommandArgs {
    std::string execPath;
    std::vector<std::string> args;
    std::string workingDir;  // 由 BuildArgs 解析，调用方决定是否 chdir
};

class LanguageCommandStrategy {
public:
    virtual ~LanguageCommandStrategy() = default;

    // 纯函数：不修改入参，不调用 chdir，不持有可变状态
    virtual StatusOr<CommandArgs> BuildArgs(
        const messages::StartInstanceRequest& request,
        const std::string& port,
        const RuntimeConfig& config) const = 0;
};
```

**约束：**
- `BuildArgs` 必须是纯函数，禁止修改入参或产生文件系统副作用
- 工作目录解析结果放进 `CommandArgs.workingDir`，由调用方决定是否 `chdir`

**语言实现：**

| 类 | 对应现有逻辑 |
|---|---|
| `CppCommandStrategy` | `GetCppBuildArgs` |
| `GoCommandStrategy` | `GetGoBuildArgs` |
| `PythonCommandStrategy` | `GetPythonBuildArgs` + `HandleWorkingDirectory`（副作用移除） |
| `JavaCommandStrategy` | `GetJavaBuildArgs*` × 4 → 统一为 `SelectJvmArgs()` 分支 |
| `NodejsStrategy` | `GetNodejsBuildArgs` + `GetPosixCustomBuildArgs` |

---

### 2. `CommandBuilder`（`config/command_builder.h`）

```cpp
class CommandBuilder {
public:
    void RegisterStrategy(const std::string& language,
                          std::unique_ptr<LanguageCommandStrategy> strategy);

    // 无副作用，不修改入参
    StatusOr<CommandArgs> BuildArgs(const std::string& language,
                                    const std::string& port,
                                    const messages::StartInstanceRequest& request,
                                    const RuntimeConfig& config) const;

    // 合并环境变量，precedence 规则：
    //   user envs > posix envs > custom envs > framework envs (只有 LD_LIBRARY_PATH 追加)
    std::map<std::string, std::string> CombineEnvs(const Envs& envs) const;

    std::string GetExecPathFromRuntimeConfig(const messages::RuntimeConfig& config) const;

private:
    std::unordered_map<std::string, std::unique_ptr<LanguageCommandStrategy>> strategies_;
    RuntimeConfig config_;
};
```

`CommandBuilder` 本身不含任何语言特定代码，只做路由和环境变量合并。

---

### 3. `RuntimeStateManager`（`executor/sandbox/runtime_state_manager.h`）

```cpp
struct SandboxInfo {
    std::string runtimeID;
    std::string sandboxID;         // 原 containerID
    std::string checkpointID;      // 空 = 无 checkpoint
    std::string portMappingsJson;  // 空 = 无端口映射
    messages::RuntimeInstanceInfo instanceInfo;
};

class RuntimeStateManager {
public:
    // 注册 / 注销（原子操作，一次清理所有相关状态）
    void Register(SandboxInfo info);
    void Unregister(const std::string& runtimeID);

    // 查询
    std::optional<SandboxInfo> Find(const std::string& runtimeID) const;
    bool IsActive(const std::string& runtimeID) const;
    std::map<std::string, messages::RuntimeInstanceInfo> GetAllInstanceInfos() const;

    // 局部更新（start 完成后补充）
    void UpdateSandboxID(const std::string& runtimeID, const std::string& sandboxID);
    void UpdateCheckpoint(const std::string& runtimeID, const std::string& checkpointID);
    void UpdatePortMappings(const std::string& runtimeID, const std::string& portJson);

    // 进行中状态（替代 inProgressStarts_ 和 pendingDeletes_）
    void MarkStartInProgress(const std::string& runtimeID);
    void MarkStartDone(const std::string& runtimeID);
    bool IsStartInProgress(const std::string& runtimeID) const;
    void MarkPendingDelete(const std::string& runtimeID);
    bool IsPendingDelete(const std::string& runtimeID) const;

    // 预热状态
    void RegisterWarmUp(const std::string& runtimeID, runtime::v1::FunctionRuntime proto);
    std::optional<runtime::v1::FunctionRuntime> GetWarmUp(const std::string& runtimeID) const;
    void UnregisterWarmUp(const std::string& runtimeID);
    bool IsWarmUp(const std::string& runtimeID) const;
};
```

**关键改进：**
- `Unregister()` 一次调用清除全部状态，不会遗漏
- `optional<T>` 替代"查 map 再判断 end()"
- 6 个原始 map 全部内化，外部不可直接访问

---

### 4. `SandboxRequestBuilder`（`executor/sandbox/sandbox_request_builder.h`）

```cpp
struct SandboxStartParams {
    const messages::StartInstanceRequest& request;
    CommandArgs cmdArgs;
    std::string port;
    std::vector<int> cardIDs;
    std::string checkpointID;  // 空 = 普通启动；非空 = restore 路径
};

// restore 统一到 start 后，只需改此 using，调用方不变
using SandboxRequest = std::variant<runtime::v1::StartRequest,
                                    runtime::v1::RestoreRequest>;

class SandboxRequestBuilder {
public:
    explicit SandboxRequestBuilder(const CommandBuilder& cmdBuilder);

    // 单一入口：checkpointID 为空走 StartRequest，非空走 RestoreRequest
    StatusOr<SandboxRequest> BuildRequest(const SandboxStartParams& params) const;

private:
    // Start / Restore 共用的 proto 填充逻辑（不再各写一份）
    void ApplyResources(runtime::v1::FunctionRuntime*, const SandboxStartParams&) const;
    void ApplyEnvsAndLogs(runtime::v1::FunctionRuntime*, const SandboxStartParams&) const;
    StatusOr<std::string> ResolveRootfs(const messages::StartInstanceRequest&) const;

    const CommandBuilder& cmdBuilder_;
};
```

---

### 5. `CheckpointOrchestrator`（`executor/sandbox/checkpoint_orchestrator.h`）

```cpp
class CheckpointOrchestrator {
public:
    CheckpointOrchestrator(
        std::shared_ptr<GrpcClient<runtime::v1::RuntimeLauncher>> grpcClient,
        std::shared_ptr<CkptFileManager> ckptFileManager,
        RuntimeStateManager& stateManager);

    // 对运行中的沙盒做快照（完整操作单元，内部回调链不暴露）
    litebus::Future<messages::SnapshotRuntimeResponse> TakeSnapshot(
        const messages::SnapshotRuntimeRequest& request);

    // 从快照恢复，返回 sandboxID
    litebus::Future<std::string /*sandboxID*/> RestoreFromSnapshot(
        const SandboxStartParams& params);

    // stop 路径清理（与端口释放并行）
    void ReleaseCheckpointRef(const std::string& runtimeID);
};
```

**关键语义：**
- `TakeSnapshot` / `RestoreFromSnapshot` 各自是完整操作单元
- `SandboxExecutor` 不再持有任何 `OnDownloadCheckpoint*` / `OnRegisterCheckpoint*` 回调
- checkpoint 注册 / 注销由 orchestrator 负责调用 `stateManager_`

---

### 6. `SandboxExecutor`（`executor/sandbox/sandbox_executor.h`）

```cpp
class SandboxExecutor : public Executor {
public:
    SandboxExecutor(const std::string& name,
                    const litebus::AID& functionAgentAID,
                    std::unique_ptr<RuntimeStateManager> stateManager,
                    std::unique_ptr<SandboxRequestBuilder> requestBuilder,
                    std::unique_ptr<CheckpointOrchestrator> checkpointOrchestrator,
                    CommandBuilder cmdBuilder);

    // 继承自 Executor（对外接口语义不变）
    litebus::Future<messages::StartInstanceResponse> StartInstance(
        const std::shared_ptr<messages::StartInstanceRequest>& request,
        const std::vector<int>& cardIDs) override;

    litebus::Future<Status> StopInstance(
        const std::shared_ptr<messages::StopInstanceRequest>& request,
        bool oomKilled = false) override;

    litebus::Future<messages::SnapshotRuntimeResponse> SnapshotRuntime(
        const std::shared_ptr<messages::SnapshotRuntimeRequest>& request) override;

    std::map<std::string, messages::RuntimeInstanceInfo> GetRuntimeInstanceInfos() override;
    bool IsRuntimeActive(const std::string& runtimeID) override;
    litebus::Future<bool> StopAllContainers();
    litebus::Future<messages::UpdateCredResponse> UpdateCredForRuntime(
        const std::shared_ptr<messages::UpdateCredRequest>& request) override;
    litebus::Future<Status> NotifyInstancesDiskUsageExceedLimit(
        const std::string& description, const int limit) override;

private:
    // 仅路由和 gRPC 调用，~8 个私有方法（原来 ~30 个）
    litebus::Future<messages::StartInstanceResponse> StartNormal(
        const std::shared_ptr<messages::StartInstanceRequest>& request,
        const CommandArgs& cmdArgs, const std::vector<int>& cardIDs);

    litebus::Future<messages::StartInstanceResponse> StartWarmUp(
        const std::shared_ptr<messages::StartInstanceRequest>& request,
        const CommandArgs& cmdArgs);

    litebus::Future<Status> StopSandbox(
        const std::string& runtimeID, const std::string& requestID, bool oomKilled);

    // gRPC 调用层（薄包装）
    litebus::Future<runtime::v1::StartResponse> DoStart(
        const std::shared_ptr<messages::StartInstanceRequest>&,
        const runtime::v1::StartRequest&);
    litebus::Future<runtime::v1::DeleteResponse> DoDelete(
        const std::string& sandboxID, bool force);

    // 连接管理
    void ReconnectContainerd();
    void CheckConnectivity();

    std::unique_ptr<RuntimeStateManager> stateManager_;
    std::unique_ptr<SandboxRequestBuilder> requestBuilder_;
    std::unique_ptr<CheckpointOrchestrator> checkpointOrchestrator_;
    CommandBuilder cmdBuilder_;
    std::shared_ptr<GrpcClient<runtime::v1::RuntimeLauncher>> grpcClient_;
    litebus::AID functionAgentAID_;
    bool reconnecting_ = false;
    bool synced_ = false;
};
```

**StartInstance 路由逻辑（伪代码）：**

```cpp
Future<StartInstanceResponse> SandboxExecutor::StartInstance(request, cardIDs) {
    const auto& runtimeID = request->runtime_id();

    // 防重入
    if (stateManager_->IsActive(runtimeID))        return AlreadyRunningResponse();
    if (stateManager_->IsStartInProgress(runtimeID)) return GetInProgressFuture(runtimeID);

    // 构建命令参数（无副作用）
    auto cmdArgs = cmdBuilder_.BuildArgs(language, port, *request, config_);
    if (!cmdArgs.ok()) return ErrorResponse(cmdArgs.status());

    stateManager_->MarkStartInProgress(runtimeID);

    // 三条路径，清晰分支
    if (IsWarmUpRequest(*request))
        return StartWarmUp(request, *cmdArgs);
    if (!request->snapshot_info().checkpoint_id().empty())
        return checkpointOrchestrator_->RestoreFromSnapshot({*request, *cmdArgs, port, cardIDs});
    return StartNormal(request, *cmdArgs, cardIDs);
}
```

---

## 规模对比

| 指标 | 重构前 | 重构后 |
|---|---|---|
| 最大单文件行数 | 1522 行 | ~300 行（SandboxExecutor） |
| ContainerExecutor 头文件 | 330 行（30+ 私有方法） | ~80 行（SandboxExecutor，~8 私有方法） |
| CommandBuilder 语言函数 | 10+ 私有方法 + Java × 4 | 5 个策略类，各自独立 |
| BuildArgs 副作用 | chdir + 修改 request | 纯函数 |
| 状态删除操作 | 手动 erase 3+ 个 map | `stateManager_->Unregister()` 一次调用 |

---

## 不变量与 Corner Case 保护

> 目标：关键资源（map 条目、ckpt 引用计数、端口）在任意失败路径下均不泄露。

### 7.1 `SandboxStartGuard`：启动流程的 RAII 守卫

**问题**：`MarkStartInProgress` → gRPC 超时 / 语言策略返回错误 → `MarkStartDone`/`Unregister` 没有被调用，`inProgressStarts_` 永久堆积。

**方案**：在 `SandboxExecutor::StartInstance` 创建 `SandboxStartGuard`，析构时自动回滚。

```cpp
// executor/sandbox/sandbox_executor.h（内部）
class SandboxStartGuard {
public:
    SandboxStartGuard(RuntimeStateManager& mgr, std::string runtimeID)
        : mgr_(mgr), runtimeID_(std::move(runtimeID)) {
        mgr_.MarkStartInProgress(runtimeID_);
    }
    ~SandboxStartGuard() {
        if (!committed_) {
            // 启动未成功提交：清除 in-progress 标记并确保 map 一致
            mgr_.Unregister(runtimeID_);
        }
    }
    // 启动成功后调用，析构时不再回滚
    void Commit() {
        committed_ = true;
        mgr_.MarkStartDone(runtimeID_);
    }
private:
    RuntimeStateManager& mgr_;
    std::string runtimeID_;
    bool committed_ = false;
};
```

**使用约束**：`StartNormal` / `StartWarmUp` / `RestoreFromSnapshot` 三条路径各自在 `.OnError()` 回调里调用 guard 析构，`.OnComplete()` 里调用 `Commit()`。

---

### 7.2 Checkpoint 引用计数不泄露

**问题**：`RestoreFromSnapshot` 的链路为：

```
DownloadCheckpoint → AddReference → DoRestore → OnRestoreCompleted
```

若 `DoRestore` 失败（gRPC 错误、超时），`AddReference` 已增加引用计数，但 `ReleaseCheckpointRef` 不会被调用，导致 ckpt 文件永久不被 GC。

**方案**：`CheckpointOrchestrator::RestoreFromSnapshot` 在 `AddReference` 成功后，立即在 Future 链上注册 `.OnError()` 释放引用：

```cpp
// checkpoint_orchestrator.cpp（关键片段）
Future<string> CheckpointOrchestrator::RestoreFromSnapshot(const SandboxStartParams& p) {
    return ckptFileManager_->DownloadCheckpoint(p.checkpointID)
        .Then([this, p](auto&&) {
            return ckptFileManager_->AddReference(p.checkpointID);
        })
        .Then([this, p](auto&&) {
            auto req = requestBuilder_.BuildRequest(p);  // 构建 RestoreRequest
            return DoRestore(req)
                .OnError([this, id = p.checkpointID](auto&&) {
                    // DoRestore 失败：释放刚才增加的引用
                    ckptFileManager_->RemoveReference(id);
                });
        });
}
```

**调用方合约**：`SandboxExecutor` 在容器正常/异常停止时，均须调用 `checkpointOrchestrator_->ReleaseCheckpointRef(runtimeID)`，包括：
- 正常 `StopInstance`
- OOM Kill 路径（`oomKilled = true`）
- `StopAllContainers`

`RuntimeStateManager::Unregister` 内部**不**隐式释放 ckpt 引用（职责分离：状态清理 vs. 存储引用计数）。

---

### 7.3 端口不泄露

**问题**：`PortManager::RequestPorts` 成功，但后续 `DoStart` 失败，端口未释放。

**方案**：端口分配在 `SandboxRequestBuilder::BuildRequest` 内完成，`BuildRequest` 返回 `StatusOr<SandboxRequest>`；若后续 `DoStart` 返回错误，`SandboxExecutor::StartNormal` 的 `.OnError()` 中调用 `PortManager::ReleasePorts`（通过 `stateManager_->Find(runtimeID)->portMappingsJson`）。

`RuntimeStateManager::Unregister` 不释放端口（同上，职责分离）。

---

### 7.4 Stop 期间收到 Start 的竞态

**问题**：容器正在 `DoDelete`，同时收到新的 `StartInstance` 请求。

**行为约定**：
- `StartInstance` 发现 `IsPendingDelete == true` 时直接返回 `RESOURCE_BUSY` 错误，不进入启动流程。
- `TerminateSandbox` 完成后（`Unregister` 之后），调用方可重新发起 `StartInstance`。

---

### 7.5 不变量汇总

| 不变量 | 保障机制 |
|---|---|
| `inProgressStarts_` 条目最终必须被清除 | `SandboxStartGuard` RAII |
| `ckptFileManager_` 引用计数与 sandbox 生命周期一致 | `OnError` 补偿 + stop 路径显式调用 `ReleaseCheckpointRef` |
| `PortManager` 分配的端口最终必须释放 | `StartNormal.OnError` 补偿 + `Unregister` 后显式 `ReleasePorts` |
| `RuntimeStateManager` 中无孤儿条目（无 sandbox 但有 map 残留） | `Unregister` 原子清除所有 map；`SandboxStartGuard` 在失败路径触发 |
| Stop 期间不允许重新 Start | `IsPendingDelete` 检查前置 |

---

## Unit Test Cases

> 测试框架：Google Test (gtest) + Google Mock (gmock)  
> 异步工具：`future_test_helper.h`（`EXPECT_AWAIT_READY`、`EXPECT_AWAIT_TRUE`、`AsyncReturn`）  
> 新增测试目录：`tests/unit/runtime_manager/executor/sandbox/`、`tests/unit/runtime_manager/config/language/`

---

### 8.1 `LanguageCommandStrategy` 测试（`config/language/`）

#### `JavaStrategyTest`

| 测试名 | 场景 | 关键断言 |
|---|---|---|
| `BuildArgs_Java11_UsesJava11JvmArgs` | config.javaVersion = "11" | args 中包含 Java11 对应的 jvmArgs |
| `BuildArgs_Java17_UsesJava17JvmArgs` | config.javaVersion = "17" | args 中包含 Java17 对应的 jvmArgs |
| `BuildArgs_Java21_UsesJava21JvmArgs` | config.javaVersion = "21" | args 中包含 Java21 对应的 jvmArgs |
| `BuildArgs_DoesNotMutateRequest` | 正常输入 | 调用前后 request 内容完全相同（对比序列化结果） |
| `BuildArgs_DoesNotCallChdir` | 正常输入 | `getcwd()` 在调用前后结果一致 |

#### `PythonStrategyTest`

| 测试名 | 场景 | 关键断言 |
|---|---|---|
| `BuildArgs_ReturnsWorkingDir_NotChdirSideEffect` | 有自定义 workingDir | `CommandArgs.workingDir` 非空，`getcwd()` 未改变 |
| `BuildArgs_DoesNotMutateRequest` | 有 workingDir 字段 | request 的 deployDir 调用前后相同 |
| `BuildArgs_InvalidWorkingDir_ReturnsError` | workingDir 不存在 | `StatusOr` 返回非 OK |

---

### 8.2 `CommandBuilder` 测试

| 测试名 | 场景 | 关键断言 |
|---|---|---|
| `BuildArgs_UnknownLanguage_ReturnsError` | language = "ruby" | status 非 OK，error message 含语言名 |
| `BuildArgs_DispatchesToCorrectStrategy` | language = "python3.9" | 调用 `PythonStrategy::BuildArgs`（gmock verify） |
| `CombineEnvs_UserOverridesFramework` | user env 与 framework env 同名 | user 值胜出 |
| `CombineEnvs_LdLibraryPath_Appended` | 两处都有 `LD_LIBRARY_PATH` | 值拼接而非覆盖 |
| `CombineEnvs_FrameworkEnvNotOverridable` | user 设置 `ENABLE_METRICS=true` | 最终值为 framework 的 `false` |

---

### 8.3 `RuntimeStateManager` 测试

| 测试名 | 场景 | 关键断言 |
|---|---|---|
| `Register_Then_Find_ReturnsInfo` | 正常注册 | `Find` 返回 `optional<SandboxInfo>` 非空，字段匹配 |
| `Unregister_ClearsAllFields` | 注册后注销 | `Find` 返回 `nullopt`；`IsActive` 返回 false；`IsStartInProgress` 返回 false |
| `Unregister_NonExistent_IsNoop` | 注销不存在的 runtimeID | 不抛异常，不崩溃 |
| `MarkStartInProgress_Then_MarkStartDone` | 正常启动完成 | `IsStartInProgress` 先 true 后 false |
| `MarkPendingDelete_BlocksIsActive` | 标记 pending delete | `IsPendingDelete` 返回 true |
| `UpdateSandboxID_Persists` | 启动后补充 sandboxID | `Find()->sandboxID` 更新 |
| `RegisterWarmUp_And_UnregisterWarmUp` | 预热注册/注销 | `IsWarmUp` 先 true 后 false；`GetWarmUp` 先有值后 nullopt |
| `GetAllInstanceInfos_ReturnsAllRegistered` | 注册 3 个 runtime | 返回 map size == 3 |

**Corner Case 专项：**

| 测试名 | 场景 | 关键断言 |
|---|---|---|
| `DoubleRegister_SameRuntimeID` | 对同一 runtimeID 注册两次 | 第二次覆盖，map 中只有一条 |
| `Unregister_AfterPartialUpdate` | 只调用了 `UpdateSandboxID` 未调用 `UpdateCheckpoint`，就 `Unregister` | `Find` 返回 nullopt，无残留 |

---

### 8.4 `SandboxRequestBuilder` 测试

| 测试名 | 场景 | 关键断言 |
|---|---|---|
| `BuildRequest_NoCheckpoint_ReturnsStartRequest` | `checkpointID` 为空 | `std::holds_alternative<StartRequest>(result)` |
| `BuildRequest_WithCheckpoint_ReturnsRestoreRequest` | `checkpointID` 非空 | `std::holds_alternative<RestoreRequest>(result)` |
| `BuildRequest_InvalidRootfs_ReturnsError` | rootfs JSON 格式错误 | `StatusOr` 返回非 OK |
| `BuildRequest_ResourcesApplied` | 有 GPU cardIDs | StartRequest 中 resources 包含 GPU 信息 |
| `BuildRequest_EnvsApplied` | request 含用户 env | StartRequest 中 envs 包含用户值 |

---

### 8.5 `CheckpointOrchestrator` 测试

#### Mock 依赖：`MockCkptFileManager`、`MockGrpcClient`

| 测试名 | 场景 | 关键断言 |
|---|---|---|
| `TakeSnapshot_Success` | gRPC Checkpoint 成功，RegisterCheckpoint 成功 | response.code() == SUCCESS；`stateManager.Find()->checkpointID` 更新 |
| `TakeSnapshot_GrpcFailed_ReturnsError` | DoCheckpoint 返回 gRPC 错误 | Future error；`stateManager` 无 checkpointID 残留 |
| `RestoreFromSnapshot_Success` | Download → AddRef → DoRestore 均成功 | 返回有效 sandboxID |
| **`RestoreFromSnapshot_DoRestoreFailed_RefReleased`** | DoRestore 失败 | `MockCkptFileManager::RemoveReference` 被调用一次（不泄露引用） |
| **`RestoreFromSnapshot_AddRefFailed_NoRemoveCalled`** | AddReference 失败 | `RemoveReference` 未被调用（未增加引用，不需要释放） |
| `ReleaseCheckpointRef_CallsRemoveReference` | 正常调用 | `RemoveReference` 被调用一次 |
| `ReleaseCheckpointRef_NoCheckpoint_IsNoop` | runtimeID 无 checkpoint 记录 | 不崩溃，`RemoveReference` 未被调用 |

---

### 8.6 `SandboxExecutor` 集成测试

#### Mock 依赖：`MockRuntimeStateManager`、`MockSandboxRequestBuilder`、`MockGrpcClient`

| 测试名 | 场景 | 关键断言 |
|---|---|---|
| `StartInstance_Normal_Success` | 普通启动，gRPC 成功 | response.code() == SUCCESS；`stateManager.IsActive()` 为 true |
| `StartInstance_AlreadyActive_ReturnsError` | runtimeID 已在运行 | 立即返回 ALREADY_EXISTS，不发起 gRPC |
| **`StartInstance_InProgress_ReturnsSameFuture`** | 同一 runtimeID 并发两次 Start | 两次调用返回同一个 Future（或等价结果），gRPC 只调用一次 |
| **`StartInstance_BuildArgsFailed_MapNotLeaked`** | `CommandBuilder` 返回错误 | `stateManager.IsStartInProgress()` 最终为 false |
| **`StartInstance_GrpcFailed_MapNotLeaked`** | `DoStart` gRPC 失败 | `stateManager.IsActive()` 为 false；`stateManager.IsStartInProgress()` 为 false |
| **`StartInstance_GrpcFailed_PortReleased`** | `DoStart` gRPC 失败 | `PortManager::ReleasePorts` 被调用 |
| `StartInstance_WarmUp_RegistersInStateManager` | warmup 类型请求 | `stateManager.IsWarmUp()` 为 true |
| `StartInstance_WithCheckpoint_DelegatesOrchestrator` | checkpointID 非空 | `MockCheckpointOrchestrator::RestoreFromSnapshot` 被调用 |
| `StopInstance_Normal_UnregistersState` | 正常停止 | `stateManager.IsActive()` 为 false；gRPC Delete 被调用 |
| **`StopInstance_OomKilled_ReleasesCheckpointRef`** | oomKilled = true | `checkpointOrchestrator.ReleaseCheckpointRef` 被调用 |
| **`StopInstance_DuringStart_MarksPendingDelete`** | start in progress 时发起 stop | `stateManager.IsPendingDelete()` 为 true；start 完成后 container 被删除 |
| **`StartInstance_PendingDelete_ReturnsError`** | `IsPendingDelete` 为 true | 立即返回 RESOURCE_BUSY |
| `StopAllContainers_StopsAll_ReleasesAllRefs` | 3 个活跃 sandbox | 3 次 gRPC Delete；3 次 `ReleaseCheckpointRef`（有 ckpt 的那些） |

---

### 8.7 测试文件位置

```
tests/unit/runtime_manager/
├── config/
│   └── language/
│       ├── CMakeLists.txt
│       ├── java_strategy_test.cpp
│       ├── python_strategy_test.cpp
│       └── command_builder_test.cpp
│
└── executor/
    └── sandbox/
        ├── CMakeLists.txt
        ├── runtime_state_manager_test.cpp
        ├── sandbox_request_builder_test.cpp
        ├── checkpoint_orchestrator_test.cpp
        └── sandbox_executor_test.cpp
```

---

## 迁移策略

1. 保留 `container_executor.h/.cpp`，内部转发到 `SandboxExecutor`，确保调用方零改动
2. 优先实现 `RuntimeStateManager` + `CommandBuilder` 重构（风险最低）
3. 逐步迁移 `SandboxRequestBuilder`、`CheckpointOrchestrator`
4. 待全部迁移完成后，`container_executor.*` 改为 deprecated alias

---

## 实现计划（Task Breakdown）

> 执行顺序按依赖图，带 ✦ 的是 corner case 保护的关键 task。

```
T1 ──► T2 ──────────────────────────────────────► T9（UT）
T3 ──► T4 ✦                                   ► T10（UT）
T3 ──► T6 ✦ ──────────────────────────────────► T11（UT）
T2 ──► T5 ──────────────────────────────────────► T11
T3,T4,T5,T6 ──► T7 ──► T8（转发层）──► T12（UT）
```

| ID | 任务 | 关键产出 | 依赖 |
|---|---|---|---|
| T1 | LanguageCommandStrategy 接口 + 各语言实现 | `config/language/*.h/.cpp` | — |
| T2 | CommandBuilder 重构为薄 dispatcher | `config/command_builder.h/.cpp` 重写 | T1 |
| T3 | RuntimeStateManager 实现 | `executor/sandbox/runtime_state_manager.h/.cpp` | — |
| T4 ✦ | SandboxStartGuard RAII | `SandboxStartGuard`（内嵌于 sandbox_executor.h） | T3 |
| T5 | SandboxRequestBuilder 实现 | `executor/sandbox/sandbox_request_builder.h/.cpp` | T2 |
| T6 ✦ | CheckpointOrchestrator 实现 | `executor/sandbox/checkpoint_orchestrator.h/.cpp` | T3 |
| T7 | SandboxExecutor 实现 | `executor/sandbox/sandbox_executor.h/.cpp` | T3–T6 |
| T8 | container_executor 转发层 | 现有文件改为转发，对外接口不变 | T7 |
| T9 | UT：LanguageStrategy + CommandBuilder | 覆盖纯函数约束、Java 版本分发、env precedence | T1,T2 |
| T10 | UT：RuntimeStateManager | 覆盖 Unregister 原子性、double-register | T3 |
| T11 | UT：RequestBuilder + CheckpointOrchestrator | 覆盖 ckpt 引用不泄露 | T5,T6 |
| T12 | UT：SandboxExecutor | 覆盖并发去重、gRPC 失败清理、OOM/竞态 | T7 |

**可并行执行的 task 组：**
- 第一批（无依赖）：**T1、T3** 可并行
- 第二批：**T2**（依赖 T1）、**T4、T6**（依赖 T3）可并行
- 第三批：**T5**（依赖 T2）、**T9**（依赖 T1,T2）、**T10**（依赖 T3）可并行
- 第四批：**T7**（依赖 T3-T6）
- 第五批：**T8、T11、T12**
