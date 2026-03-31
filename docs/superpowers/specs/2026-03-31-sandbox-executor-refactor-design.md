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

## 迁移策略

1. 保留 `container_executor.h/.cpp`，内部转发到 `SandboxExecutor`，确保调用方零改动
2. 优先实现 `RuntimeStateManager` + `CommandBuilder` 重构（风险最低）
3. 逐步迁移 `SandboxRequestBuilder`、`CheckpointOrchestrator`
4. 待全部迁移完成后，`container_executor.*` 改为 deprecated alias
