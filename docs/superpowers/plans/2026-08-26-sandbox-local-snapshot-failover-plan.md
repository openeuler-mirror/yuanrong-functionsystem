<!--
Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
Licensed under the Apache License, Version 2.0.
See the LICENSE file in this repository for the complete license text.
-->

# Sandbox Local Snapshot Failover Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 统一节点侧 checkpoint 目录和恢复入口，并让显式启用 `failover` 的 sandbox 在原节点从最新匿名快照自动恢复，最后提供返回 bool 的主动 reload。

**Architecture:** FunctionAgent 以平面目录和原子 `snapshot.meta` 管理本地制品，FunctionAgentMgr 在 Agent 注册时枚举制品并维护 Proxy 本地视图。Pause、RRT `/checkpoint`、Resume、create-from-snapshot、自动 failover 和 reload 最终都只向 Agent 传 snapshotID；RuntimeManager 通过 sandboxd 公共 Checkpoint 与 Start(checkpoint_info) 执行物理操作。恢复期间 InstanceInfo 保持 RUNNING，不持久化恢复 intent，Proxy 重启依赖现有 runtime reconcile 重新收敛。

**Tech Stack:** C++17、protobuf/gRPC、LiteBus actors/futures、GTest 1.10、Rust/Tokio RRT、Go/Gin Frontend、Python 3.10+ Sandbox SDK、AKernel Python SDK、sandboxd UDS API。

**Spec:** `src/yuanrong/functionsystem/docs/superpowers/specs/2026-08-26-sandbox-local-snapshot-failover-design.md`

## Global Constraints

- 除特别注明外，所有路径和命令均相对 AKernel 顶层仓库执行。
- 本地目录固定为 `<checkpoint_root>/<snapshotID>/checkpoint.img` 与 `snapshot.meta`；禁止重新引入 Pause/Restore 业务子目录。
- `snapshot.meta` 是制品提交记录，不保存 operation phase、retry count 或 recovery intent。
- Pause 与 RRT `/checkpoint` 都生成 `anonymous=true` 的本地快照；本地存储不定义 PAUSE 类型。
- Proxy 选择 snapshotID；FunctionAgent 不按 meta.instanceID 限制恢复目标，只验证路径、内容和 runtime 兼容性。
- InstanceInfo 只新增 `bool failover = 42`；恢复期间保持 RUNNING，不新增实例状态或 CAS 恢复事务。
- 自动恢复只在原 FunctionAgent/原节点执行；快照缺失、Agent 不可用或预算耗尽时进入 FATAL，不 cold redeploy。
- `/checkpoint` 和 failover/reload 不新增 traffic gate；Pause 保留既有 gate 语义。
- 主动 reload 是最后一个功能任务，返回 bool，不创建新 Sandbox 或 handler。
- sandboxd 源码不增加 YuanRong 私有 RPC；Checkpoint 成功响应为空，Restore 使用 StartRequest.checkpoint_info。
- protobuf 已发布字段号和 reserved 名称不得复用；本文指定的新字段号必须通过 descriptor 测试锁定。
- Python SDK 测试使用 Python 3.10+；C++ GTest 命令必须选择非零测试，且不使用 `--gtest_brief`。
- 每个提交使用 Conventional Commits、包含解释原因的正文，并通过 `git commit -s` 添加 DCO sign-off。
- 多仓修改分别在 FunctionSystem、YuanRong、Frontend、sandbox-sdk 和 AKernel 仓库提交，最后由 YuanRong/AKernel gitlink 提交串联；不得把无关 dirty 文件带入提交。

本计划保持单一集成计划，因为 Agent store、Proxy view、RRT trigger 和 recovery decision 通过
同一个 snapshotID/Deploy 契约顺序依赖；每个 Task 仍形成可独立评审和验证的提交。Task 1-9
交付自动 failover 闭环，Task 10 是建立在该闭环之上的可独立延后 reload 入口。

---

### Task 1: 对齐 sandboxd 公共 Checkpoint/Restore 契约

**Files:**
- Modify: `src/yuanrong/functionsystem/proto/posix/sandbox_api.proto`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/runtime_manager/ckpt/checkpoint_plan.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_checkpoint_orchestrator.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_checkpoint_orchestrator.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_request_builder.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_request_builder.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_executor.cpp`
- Test: `src/yuanrong/functionsystem/functionsystem/tests/unit/runtime_manager/executor/sandboxd/sandboxd_executor_test.cpp`
- Test: `src/yuanrong/functionsystem/functionsystem/tests/unit/runtime_manager/executor/sandboxd/sandboxd_request_builder_test.cpp`

**Interfaces:**
- Consumes: sandboxd public `CheckpointRequest{id, checkpoint_dir, timeout_seconds, compress, leave_running}` and `StartRequest.checkpoint_info.checkpoint_dir`.
- Produces: `CheckpointPlan{timeoutSeconds, compress, leaveRuntimeRunning}`、空成功响应映射，以及 `SandboxdRequestBuilder::AttachCheckpointInfo`。

- [ ] **Step 1: 写 descriptor 和 request builder 失败测试**

```cpp
TEST(SandboxdProtoContractTest, UsesPublicCheckpointAndStartRestore)
{
    const auto *service = runtime::v1::SandboxService::descriptor();
    ASSERT_NE(service, nullptr);
    EXPECT_NE(service->FindMethodByName("Checkpoint"), nullptr);
    EXPECT_EQ(service->FindMethodByName("Restore"), nullptr);
    EXPECT_EQ(service->FindMethodByName("DeleteCheckpoint"), nullptr);

    const auto *checkpoint = runtime::v1::CheckpointRequest::descriptor();
    EXPECT_EQ(checkpoint->FindFieldByName("id")->number(), 1);
    EXPECT_EQ(checkpoint->FindFieldByName("checkpoint_dir")->number(), 2);
    EXPECT_EQ(checkpoint->FindFieldByName("timeout_seconds")->number(), 3);
    EXPECT_EQ(checkpoint->FindFieldByName("compress")->number(), 4);
    EXPECT_EQ(checkpoint->FindFieldByName("leave_running")->number(), 5);
    EXPECT_EQ(runtime::v1::CheckpointResponse::descriptor()->field_count(), 0);

    const auto *start = runtime::v1::StartRequest::descriptor();
    ASSERT_NE(start->FindFieldByName("checkpoint_info"), nullptr);
    EXPECT_EQ(start->FindFieldByName("checkpoint_info")->number(), 21);
}

TEST(SandboxdRequestBuilderTest, AttachesCheckpointInfoToStart)
{
    runtime::v1::StartRequest request;
    auto status = SandboxdRequestBuilder::AttachCheckpointInfo(
        request, "/var/lib/akernel/checkpoints/snap-1");
    ASSERT_TRUE(status.IsOk());
    ASSERT_TRUE(request.has_checkpoint_info());
    EXPECT_EQ(request.checkpoint_info().checkpoint_dir(),
              "/var/lib/akernel/checkpoints/snap-1");
}
```

- [ ] **Step 2: 运行测试并确认当前自定义 Restore/DeleteCheckpoint 协议失败**

Run:

```bash
cd src/yuanrong/functionsystem
cmake --build build --target sandboxd_test_bin -j4
build/bin/sandboxd_test_bin --gtest_color=no \
  --gtest_filter='SandboxdProtoContractTest.*:SandboxdRequestBuilderTest.AttachesCheckpointInfoToStart'
```

Expected: 非零测试被选择；descriptor 或缺失 `AttachCheckpointInfo` 断言失败。

- [ ] **Step 3: 替换本地 sandboxd proto 并实现公共请求映射**

`CheckpointPlan` 固定增加：

```cpp
uint32_t timeoutSeconds{180};
bool compress{true};
```

请求构造为：

```cpp
runtime::v1::CheckpointRequest request;
request.set_id(plan.sandboxID);
request.set_checkpoint_dir(plan.checkpointDirectory);
request.set_timeout_seconds(plan.timeoutSeconds);
request.set_compress(plan.compress);
request.set_leave_running(plan.leaveRuntimeRunning);
```

Checkpoint gRPC OK 加空响应映射为 `CheckpointResult{Status::OK()}`；删除对 response
artifact_path/size/SHA 的读取。Restore 路径构建普通 StartRequest 后调用：

```cpp
Status SandboxdRequestBuilder::AttachCheckpointInfo(
    runtime::v1::StartRequest &request, const std::string &directory)
{
    auto path = std::filesystem::path(directory).lexically_normal();
    if (!path.is_absolute()) {
        return Status(StatusCode::ERR_PARAM_INVALID,
                      "checkpoint directory must be absolute");
    }
    request.mutable_checkpoint_info()->set_checkpoint_dir(path.string());
    return Status::OK();
}
```

删除 RuntimeManager 对 `RestoreRequest`、`AsyncRestore`、`DeleteCheckpointRequest` 的生产使用；
本地制品事实和删除在 Task 2 的 LocalSnapshotStore 中完成。

- [ ] **Step 4: 运行 sandboxd adapter 测试**

```bash
cd src/yuanrong/functionsystem
cmake --build build --target sandboxd_test_bin -j4
build/bin/sandboxd_test_bin --gtest_color=no \
  --gtest_filter='SandboxdProtoContractTest.*:SandboxdRequestBuilderTest.*Checkpoint*:SandboxdExecutorTest.*Checkpoint*:SandboxdExecutorTest.*Restore*'
```

Expected: 命令退出 0，选择非零测试且全部通过。

- [ ] **Step 5: 提交 FunctionSystem adapter**

```bash
git -C src/yuanrong/functionsystem add proto/posix \
  functionsystem/src/runtime_manager functionsystem/tests/unit/runtime_manager
git -C src/yuanrong/functionsystem commit -s \
  -m "refactor(sandbox): align checkpoint restore with sandboxd" \
  -m "Use the public empty Checkpoint response and restore through Start checkpoint_info so YuanRong owns local artifact metadata and cleanup."
```

---

### Task 2: 实现 FunctionAgent LocalSnapshotStore

**Files:**
- Create: `src/yuanrong/functionsystem/functionsystem/src/function_agent/snapshot/local_snapshot_store.h`
- Create: `src/yuanrong/functionsystem/functionsystem/src/function_agent/snapshot/local_snapshot_store.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_agent/CMakeLists.txt`
- Create: `src/yuanrong/functionsystem/functionsystem/tests/unit/function_agent/snapshot/local_snapshot_store_test.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/tests/unit/function_agent/CMakeLists.txt`

**Interfaces:**
- Consumes: absolute checkpoint root、snapshotID、source/runtime facts 和 sandboxd 输出的 checkpoint.img。
- Produces: `LocalSnapshotDescriptor`、`LocalSnapshotStore::Prepare/Commit/List/ValidateForRestore/Delete`。

- [ ] **Step 1: 写平面路径、meta commit、List 和精确删除失败测试**

```cpp
TEST_F(LocalSnapshotStoreTest, CommitsAndListsFlatSnapshot)
{
    LocalSnapshotCommitRequest request{
        .snapshotID = "anon-1",
        .anonymous = true,
        .instanceID = "sandbox-a",
        .sourceRuntimeID = "runtime-a",
        .sourceSandboxID = "sbox-runtime-a",
        .sourceInstanceVersion = 12,
        .runtimeClass = "runsc",
        .architecture = "x86_64",
        .createdAtUnixSeconds = 1787670000,
    };
    ASSERT_TRUE(store_->Prepare(request).status.IsOk());
    WriteCheckpoint(root_ / "anon-1" / "checkpoint.img", "checkpoint-state");
    auto committed = store_->Commit(request);
    ASSERT_TRUE(committed.status.IsOk());
    EXPECT_EQ(committed.descriptor.snapshotID, "anon-1");
    EXPECT_TRUE(committed.descriptor.anonymous);
    EXPECT_EQ(committed.descriptor.generation, 1U);
    ASSERT_EQ(store_->List().size(), 1U);
    EXPECT_TRUE(std::filesystem::exists(root_ / "anon-1" / "snapshot.meta"));
}

TEST_F(LocalSnapshotStoreTest, IgnoresDirectoryWithoutCommittedMeta)
{
    std::filesystem::create_directories(root_ / "partial");
    WriteCheckpoint(root_ / "partial" / "checkpoint.img", "partial");
    EXPECT_TRUE(store_->List().empty());
}

TEST_F(LocalSnapshotStoreTest, RefusesDeleteWhenDigestDoesNotMatch)
{
    auto descriptor = CommitSnapshot("anon-2", "sandbox-a", "payload");
    LocalSnapshotDeleteIdentity identity{
        .snapshotID = descriptor.snapshotID,
        .expectedGeneration = descriptor.generation,
        .expectedSize = descriptor.size,
        .expectedSha256 = std::string(64, '0'),
    };
    EXPECT_EQ(store_->Delete(identity).StatusCode(), StatusCode::SCHEDULE_CONFLICTED);
    EXPECT_TRUE(std::filesystem::exists(root_ / "anon-2"));
}
```

测试 fixture 同时增加以下本地 helper，不引用生产代码之外的未定义工具：

```cpp
void WriteCheckpoint(const std::filesystem::path &path,
                     const std::string &contents);
LocalSnapshotDescriptor CommitSnapshot(const std::string &snapshotID,
                                       const std::string &instanceID,
                                       const std::string &contents);
```

- [ ] **Step 2: 运行测试并确认类型缺失**

```bash
cd src/yuanrong/functionsystem
cmake --build build --target functionsystem_unit_test -j4
build/bin/functionsystem_unit_test --gtest_color=no \
  --gtest_filter='LocalSnapshotStoreTest.*'
```

Expected: 编译失败或非零退出，指出 LocalSnapshotStore 类型/实现不存在。

- [ ] **Step 3: 实现安全目录、JSON meta 和原子提交**

在 header 中定义设计文档的 descriptor/request/delete identity。实现必须：

```cpp
std::filesystem::path LocalSnapshotStore::SnapshotDirectory(
    const std::string &snapshotID) const
{
    if (!runtime_manager::IsSafeCheckpointIdentityComponent(snapshotID)) {
        throw std::invalid_argument("invalid snapshot ID");
    }
    auto directory = (checkpointRoot_ / snapshotID).lexically_normal();
    if (directory.parent_path() != checkpointRoot_) {
        throw std::invalid_argument("snapshot path escapes checkpoint root");
    }
    return directory;
}
```

使用现有 `SecureDirectory`/no-follow helpers 打开文件。`Commit` 计算 checkpoint.img size/SHA，
在 per-instance mutex 下计算 anonymous generation，写 `snapshot.meta.tmp`、fsync、rename、目录
fsync。`List` 只枚举 checkpoint root 直接子目录，只接受 schemaVersion=1、目录名与 snapshotID
一致、checkpoint.img regular 且 size 与 meta 一致的条目；Restore 再执行完整 SHA 校验。

- [ ] **Step 4: 增加 replay、symlink、generation 和 restore validation 测试**

加入并运行：

```cpp
TEST_F(LocalSnapshotStoreTest, SelectsNextAnonymousGenerationAfterRestart);
TEST_F(LocalSnapshotStoreTest, ReplaysMatchingCommittedSnapshot);
TEST_F(LocalSnapshotStoreTest, RejectsConflictingCommittedSnapshot);
TEST_F(LocalSnapshotStoreTest, RejectsSymlinkedImageAndMeta);
TEST_F(LocalSnapshotStoreTest, ValidateForRestoreChecksShaAndCompatibility);
TEST_F(LocalSnapshotStoreTest, DeleteMissingSnapshotIsIdempotent);
```

```bash
cd src/yuanrong/functionsystem
cmake --build build --target functionsystem_unit_test -j4
build/bin/functionsystem_unit_test --gtest_color=no \
  --gtest_filter='LocalSnapshotStoreTest.*'
```

Expected: 所有 LocalSnapshotStore 测试通过。

- [ ] **Step 5: 提交 LocalSnapshotStore**

```bash
git -C src/yuanrong/functionsystem add \
  functionsystem/src/function_agent/snapshot \
  functionsystem/src/function_agent/CMakeLists.txt \
  functionsystem/tests/unit/function_agent
git -C src/yuanrong/functionsystem commit -s \
  -m "feat(snapshot): add local snapshot store" \
  -m "Commit flat checkpoint artifacts with durable metadata so agents can enumerate, validate, replay, and delete exact local snapshots after restart."
```

---

### Task 3: 接入 FunctionAgent Checkpoint、List 和 Delete 协议

**Files:**
- Modify: `src/yuanrong/functionsystem/proto/posix/message.proto`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_agent/agent_service_actor.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_agent/agent_service_actor.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_agent/agent_service_snapshot.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/function_agent_manager/function_agent_mgr.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/function_agent_manager/function_agent_mgr.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/function_agent_manager/function_agent_mgr_actor.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/function_agent_manager/function_agent_mgr_actor.cpp`
- Test: `src/yuanrong/functionsystem/functionsystem/tests/unit/function_agent/agent_service/agent_service_actor_test.cpp`
- Test: `src/yuanrong/functionsystem/functionsystem/tests/unit/function_proxy/local_scheduler/function_agent_manager/function_agent_mgr_test.cpp`

**Interfaces:**
- Consumes: Task 2 LocalSnapshotStore 和现有 SnapshotRuntime actor flow。
- Produces: `LocalSnapshotMetadata`、List/Delete actor messages、`SnapshotRuntimeRequest.anonymous=13`、`SnapshotRuntimeResponse.localSnapshot=10`。

- [ ] **Step 1: 添加 proto descriptor 失败测试**

```cpp
TEST(LocalSnapshotProtoTest, UsesStableInternalFieldNumbers)
{
    EXPECT_EQ(messages::SnapshotRuntimeRequest::descriptor()
                  ->FindFieldByName("anonymous")->number(), 13);
    EXPECT_EQ(messages::SnapshotRuntimeResponse::descriptor()
                  ->FindFieldByName("localSnapshot")->number(), 10);
    EXPECT_EQ(messages::ListLocalSnapshotsResponse::descriptor()
                  ->FindFieldByName("snapshots")->number(), 4);
}
```

- [ ] **Step 2: 定义 actor wire messages**

在 `message.proto` 增加：

```proto
message LocalSnapshotMetadata {
  string snapshotID = 1;
  bool anonymous = 2;
  string instanceID = 3;
  uint64 generation = 4;
  string runtimeClass = 5;
  string architecture = 6;
  uint64 size = 7;
  string sha256 = 8;
  int64 createdAtUnixSeconds = 9;
  string sourceRuntimeID = 10;
  string sourceSandboxID = 11;
  int64 sourceInstanceVersion = 12;
  string tenantHash = 13;
  string artifactFormat = 14;
  uint32 artifactFormatVersion = 15;
}

message ListLocalSnapshotsRequest { string requestID = 1; }
message ListLocalSnapshotsResponse {
  int32 code = 1;
  string message = 2;
  string requestID = 3;
  repeated LocalSnapshotMetadata snapshots = 4;
}
message DeleteLocalSnapshotRequest {
  string requestID = 1;
  string snapshotID = 2;
  uint64 expectedGeneration = 3;
  uint64 expectedSize = 4;
  string expectedSha256 = 5;
}
message DeleteLocalSnapshotResponse {
  int32 code = 1;
  string message = 2;
  string requestID = 3;
}
```

为 SnapshotRuntimeRequest 增加 `bool anonymous = 13`，为 SnapshotRuntimeResponse 增加
`LocalSnapshotMetadata localSnapshot = 10`。

- [ ] **Step 3: 写 Agent List/Delete 和 Checkpoint commit 失败测试**

```cpp
TEST_F(AgentServiceActorTest, SnapshotRuntimeCommitsLocalMetadataBeforeSuccess);
TEST_F(AgentServiceActorTest, ListLocalSnapshotsReturnsOnlyCommittedEntries);
TEST_F(AgentServiceActorTest, DeleteLocalSnapshotRequiresExactIdentity);
TEST_F(FunctionAgentMgrTest, RejectsListResponseFromUnexpectedAgent);
```

测试断言 successful SnapshotRuntimeResponse 包含 localSnapshot，且响应发出前 snapshot.meta
已经存在。List/Delete 只接受本地 FunctionAgentMgr AID。

- [ ] **Step 4: 注入 LocalSnapshotStore 并实现 actor handlers**

AgentServiceActor 初始化时构造一个 LocalSnapshotStore，注册：

```cpp
Receive("ListLocalSnapshots", &AgentServiceActor::ListLocalSnapshots);
Receive("DeleteLocalSnapshot", &AgentServiceActor::DeleteLocalSnapshot);
```

SnapshotRuntime 由 Agent 统一解析
`<checkpointRoot>/<snapshotID>`；RuntimeManager 成功后调用 store Commit 并用 descriptor 填充
localSnapshot。删除旧 `ResolveOrdinaryCheckpointPlan` 与 Pause 专属 source path 选择分支，但保留
远端 SnapshotStorage publish 后处理。

FunctionAgentMgr 新增：

```cpp
virtual litebus::Future<messages::ListLocalSnapshotsResponse>
ListLocalSnapshots(const std::string &functionAgentID);

virtual litebus::Future<messages::DeleteLocalSnapshotResponse>
DeleteLocalSnapshot(const std::string &functionAgentID,
                    const messages::DeleteLocalSnapshotRequest &request);
```

- [ ] **Step 5: 运行 Agent 和 Manager 测试**

```bash
cd src/yuanrong/functionsystem
cmake --build build --target functionsystem_unit_test -j4
build/bin/functionsystem_unit_test --gtest_color=no \
  --gtest_filter='AgentServiceActorTest.*LocalSnapshot*:AgentServiceActorTest.*SnapshotRuntime*:FunctionAgentMgrTest.*LocalSnapshot*'
```

Expected: 所选测试全部通过，SnapshotRuntime 成功结果含完整 localSnapshot。

- [ ] **Step 6: 提交 Agent 协议和集成**

```bash
git -C src/yuanrong/functionsystem add proto/posix/message.proto \
  functionsystem/src/function_agent \
  functionsystem/src/function_proxy/local_scheduler/function_agent_manager \
  functionsystem/tests/unit/function_agent \
  functionsystem/tests/unit/function_proxy/local_scheduler/function_agent_manager
git -C src/yuanrong/functionsystem commit -s \
  -m "feat(snapshot): expose local snapshot inventory" \
  -m "Commit SnapshotRuntime output through LocalSnapshotStore and let the owning proxy list or precisely delete durable local artifacts after agent restart."
```

---

### Task 4: 统一 snapshotID Restore 与远端物化目录

**Files:**
- Modify: `src/yuanrong/functionsystem/proto/posix/message.proto`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_agent/common/utils.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_agent/common/utils.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_agent/agent_service_actor.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_executor.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/runtime_manager/ckpt/pause_artifact_path_manager.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/runtime_manager/ckpt/pause_artifact_path_manager.cpp`
- Test: `src/yuanrong/functionsystem/functionsystem/tests/unit/function_agent/common/utils_test.cpp`
- Test: `src/yuanrong/functionsystem/functionsystem/tests/unit/runtime_manager/executor/sandboxd/sandboxd_executor_test.cpp`
- Test: `src/yuanrong/functionsystem/functionsystem/tests/unit/runtime_manager/ckpt/pause_artifact_path_manager_test.cpp`

**Interfaces:**
- Consumes: LocalSnapshotStore::ValidateForRestore、既有 SnapshotStorage materialization 和正常 StartInstanceRequest。
- Produces: `DeployInstanceRequest.restoreSnapshotID=42`、`RuntimeInstanceInfo.restoreSnapshotID=14`，以及只接受 snapshotID 的本地 restore path。

- [ ] **Step 1: 添加 proto 和 restore path 失败测试**

```cpp
TEST(LocalRestoreProtoTest, UsesStableRestoreSnapshotFields)
{
    EXPECT_EQ(messages::DeployInstanceRequest::descriptor()
                  ->FindFieldByName("restoreSnapshotID")->number(), 42);
    EXPECT_EQ(messages::RuntimeInstanceInfo::descriptor()
                  ->FindFieldByName("restoreSnapshotID")->number(), 14);
}

TEST_F(SandboxdExecutorTest, LocalRestoreUsesFlatSnapshotDirectory)
{
    auto request = MakeStartRequest("sandbox-a", "anon-1");
    ASSERT_AWAIT_READY(executor_->StartInstance(request));
    EXPECT_EQ(lastStart_.checkpoint_info().checkpoint_dir(),
              checkpointRoot_ + "/anon-1");
}
```

- [ ] **Step 2: 添加字段并贯穿 Agent 到 RuntimeManager**

```proto
// DeployInstanceRequest
string restoreSnapshotID = 42;

// RuntimeInstanceInfo
string restoreSnapshotID = 14;
```

FunctionAgent 在 Deploy 收到 restoreSnapshotID 时调用 LocalSnapshotStore::ValidateForRestore，
把字段和可信目录传入 StartInstanceRequest；RuntimeManager 不从 Proxy 接收 path。

- [ ] **Step 3: 把 Pause/Reusable 远端 materialization 改为平面目录**

将 `MaterializeImmutableSnapshotArtifact` 的目标从 per-attempt restore path 改为
`<checkpointRoot>/<snapshotID>`。同 snapshotID materialization 由 Agent 内 per-snapshot
singleflight 串行：合法现存 meta 直接复用；无 meta 的不完整目录先按 exact snapshotID 清理，
再下载到 `.download` 文件并通过 LocalSnapshotStore::Commit 提交。

删除 `PlanSourceArtifact/PlanRestoreAttempt/DeleteRestoreAttempt` 的生产依赖；
PauseArtifactPathManager 仅在所有调用移除后删除，CMake/test 同步更新。

- [ ] **Step 4: 运行 Restore、materialization 和 path 测试**

```bash
cd src/yuanrong/functionsystem
cmake --build build --target functionsystem_unit_test sandboxd_test_bin pause_resume_unit_test -j4
build/bin/functionsystem_unit_test --gtest_color=no \
  --gtest_filter='FunctionAgentUtilsTest.*Materializ*:AgentServiceActorTest.*RestoreSnapshotID*'
build/bin/sandboxd_test_bin --gtest_color=no \
  --gtest_filter='SandboxdExecutorTest.*Restore*Snapshot*:SandboxdRequestBuilderTest.*CheckpointInfo*'
build/bin/pause_resume_unit_test --gtest_color=no \
  --gtest_filter='PauseArtifactPathManagerTest.*:SnapCtrlActorResumeTest.*'
```

Expected: 三个命令均退出 0；所有 restore checkpoint_dir 都是 root/snapshotID。

- [ ] **Step 5: 提交统一 Restore**

```bash
git -C src/yuanrong/functionsystem add proto/posix/message.proto \
  functionsystem/src/function_agent functionsystem/src/runtime_manager \
  functionsystem/tests/unit/function_agent functionsystem/tests/unit/runtime_manager
git -C src/yuanrong/functionsystem commit -s \
  -m "refactor(snapshot): restore local artifacts by ID" \
  -m "Resolve and validate checkpoint paths inside FunctionAgent so proxy deploys carry only snapshot IDs and every materialized artifact uses the flat local store."
```

---

### Task 5: 在 Proxy 建立并恢复 LocalSnapshotView

**Files:**
- Create: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/function_agent_manager/local_snapshot_view.h`
- Create: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/function_agent_manager/local_snapshot_view.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/function_agent_manager/function_agent_mgr_actor.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/function_agent_manager/function_agent_mgr_actor.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/function_agent_manager/function_agent_mgr.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/gc_actor/runtime_reconcile_actor.cpp`
- Test: `src/yuanrong/functionsystem/functionsystem/tests/unit/function_proxy/local_scheduler/function_agent_manager/local_snapshot_view_test.cpp`
- Test: `src/yuanrong/functionsystem/functionsystem/tests/unit/function_proxy/local_scheduler/function_agent_manager/function_agent_mgr_test.cpp`
- Test: `src/yuanrong/functionsystem/functionsystem/tests/unit/function_proxy/local_scheduler/gc_actor/runtime_reconcile_actor_test.cpp`

**Interfaces:**
- Consumes: FunctionAgentMgr::ListLocalSnapshots 和 Agent 注册流程。
- Produces: `LocalSnapshotView::ReplaceAgentSnapshots/RecordCommitted/LatestAnonymous/Remove`，并保证 List 完成后才触发首次 runtime reconcile。

- [ ] **Step 1: 写 view winner 和注册顺序失败测试**

```cpp
TEST(LocalSnapshotViewTest, SelectsHighestAnonymousGenerationPerInstance)
{
    LocalSnapshotView view;
    view.ReplaceAgentSnapshots("agent-a", {
        MakeSnapshot("old", "sandbox-a", 1, true),
        MakeSnapshot("new", "sandbox-a", 2, true),
        MakeSnapshot("named", "sandbox-a", 9, false),
    });
    auto latest = view.LatestAnonymous("sandbox-a");
    ASSERT_TRUE(latest.has_value());
    EXPECT_EQ(latest->snapshotid(), "new");
}

TEST_F(FunctionAgentMgrTest, RegistrationListsSnapshotsBeforeFirstReconcile)
{
    RegisterAgent("agent-a");
    ASSERT_AWAIT_TRUE_FOR([&] { return operations_ ==
        std::vector<std::string>{"list-snapshots", "reconcile"}; }, 5000);
}
```

测试文件定义精确 helper：

```cpp
messages::LocalSnapshotMetadata MakeSnapshot(const std::string &snapshotID,
                                             const std::string &instanceID,
                                             uint64_t generation,
                                             bool anonymous);
```

- [ ] **Step 2: 实现 LocalSnapshotView**

使用 `snapshotID -> {agentID, metadata}` 与 `instanceID -> snapshotID` 两张 map。
`ReplaceAgentSnapshots` 先移除该 Agent 的旧项，再按 generation 选择 anonymous winner；同
instance/generation 不同 snapshotID 返回冲突 Status 并不设置 latest。`RecordCommitted` 返回
被替换旧 descriptor，调用方据此发精确 Delete。

- [ ] **Step 3: 将 List 插入 Agent 注册链**

FunctionAgentMgrActor 在 agent AID 可用后调用 List，成功时 ReplaceAgentSnapshots，然后才执行
co-process `RuntimeReconcileActor::TriggerOnce` callback。List 失败保持 Agent RECOVERING，并按现有
注册重试节奏重试，不能先把 unit 标为 NORMAL。

SnapshotRuntimeResponse 包含 localSnapshot 时调用 RecordCommitted；新 snapshot 成为 latest
后发送旧 snapshot Delete，Delete 失败只记录并等待下次 Agent List 收敛。

- [ ] **Step 4: 运行 view、注册和 reconcile 顺序测试**

```bash
cd src/yuanrong/functionsystem
cmake --build build --target functionsystem_unit_test -j4
build/bin/functionsystem_unit_test --gtest_color=no \
  --gtest_filter='LocalSnapshotViewTest.*:FunctionAgentMgrTest.*LocalSnapshot*:RuntimeReconcileActorTest.*SnapshotView*'
```

Expected: winner、冲突、Agent restart replacement 和 List-before-reconcile 全部通过。

- [ ] **Step 5: 提交 Proxy view**

```bash
git -C src/yuanrong/functionsystem add \
  functionsystem/src/function_proxy/local_scheduler/function_agent_manager \
  functionsystem/src/function_proxy/local_scheduler/gc_actor \
  functionsystem/tests/unit/function_proxy/local_scheduler
git -C src/yuanrong/functionsystem commit -s \
  -m "feat(snapshot): rebuild local snapshot view on registration" \
  -m "List committed agent artifacts before runtime reconciliation so proxy restart can select the latest anonymous recovery point and clean stale snapshots safely."
```

---

### Task 6: 接入 Pause 和 RRT `POST /checkpoint`

**Files:**
- Modify: `src/yuanrong/functionsystem/functionsystem/src/common/constants/signal.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/instance_control/instance_ctrl_actor.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/instance_control/instance_ctrl_actor.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/snap_ctrl/snap_ctrl.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/snap_ctrl/snap_ctrl_actor.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/snap_ctrl/snap_ctrl_actor.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/snap_ctrl/snap_ctrl_actor_pause.cpp`
- Modify: `src/yuanrong/api/rust/rrt-daemon/src/runtime/httpserver.rs`
- Modify: `src/yuanrong/api/rust/rrt-daemon/src/runtime/mod.rs`
- Modify: `src/yuanrong/api/rust/rrt-daemon/src/startup.rs`
- Modify: `deploy/akernel/charts/core/values.yaml`
- Modify: `deploy/akernel/charts/core/templates/node/daemonset.yaml`
- Modify: `builder/systemd_services/yuanrong.service`
- Test: `src/yuanrong/functionsystem/functionsystem/tests/unit/function_proxy/local_scheduler/snap_ctrl/snap_ctrl_actor_test.cpp`
- Test: `src/yuanrong/api/rust/rrt-daemon/src/runtime/httpserver.rs`
- Test: `src/yuanrong/api/rust/rrt-daemon/src/runtime/mod.rs`

**Interfaces:**
- Consumes: SnapshotRuntimeRequest.anonymous、existing PrepareSnap/SnapStarted、LocalSnapshotView update。
- Produces: signal 24 `INSTANCE_ANONYMOUS_CHECKPOINT_SIGNAL` 和 UDS HTTP `POST /checkpoint`。

- [ ] **Step 1: 写 signal、同步 HTTP 和配置失败测试**

```rust
#[tokio::test]
async fn checkpoint_endpoint_waits_for_proxy_ack_and_handoff() {
    let (tx, mut rx) = tokio::sync::mpsc::channel(4);
    let request = invoke_checkpoint_handler("sandbox-a", tx);
    let message = rx.recv().await.expect("checkpoint signal");
    let kill = match message.body.expect("body") {
        streaming_message::Body::KillReq(value) => value,
        other => panic!("unexpected body: {other:?}"),
    };
    assert_eq!(kill.instance_id, "sandbox-a");
    assert_eq!(kill.signal, 24);
    coordinator.record_proxy_ack();
    coordinator.record_handoff();
    assert_eq!(request.await.status, 200);
}
```

```cpp
TEST_F(SnapCtrlActorTest, AnonymousCheckpointStopsHeartbeatAndRestartsAfterSnapStarted);
TEST_F(SnapCtrlActorTest, PauseSnapshotIsRecordedAsAnonymous);
TEST_F(SnapCtrlActorTest, AnonymousCheckpointFailureKeepsPreviousSnapshot);
```

- [ ] **Step 2: 增加 signal 24 和 Proxy handler**

在 `signal.h` 定义并加入 SignalToString：

```cpp
const int32_t INSTANCE_ANONYMOUS_CHECKPOINT_SIGNAL = 24;
```

InstanceCtrl 收到 signal 后从当前 state machine 获取真实 instance identity，不信任 payload 中的
目标。handler 停止 heartbeat、触发既有 SnapCtrl 匿名快照流程并立即返回 KillRsp ACK；不等待
PrepareSnap 或 sandboxd checkpoint 完成。异步流程完成后恢复 heartbeat。该路径不调用 traffic
gate，也不改变 InstanceState。

Pause 调用 SnapshotRuntime 时设置 anonymous=true；其后继续既有 publish/source release/PAUSED
流程。FunctionAgentMgr 从响应中的 localSnapshot 自动替换 latest view。

- [ ] **Step 3: 在 RRT 提供 UDS `/checkpoint`**

复用当前 HTTP parser，在 `${YR_RRT_CONTROL_SOCKET_PATH}/rrt.sock` 提供 `POST /checkpoint`。
Helm 参数 `rrtControlSocketPath` 只在非空时注入沙箱环境变量
`YR_RRT_CONTROL_SOCKET_PATH`；环境变量未配置或为空时 RRT 不创建 listener。
node 容器以 systemd 为 PID 1，因此 `yuanrong.service` 必须通过 `PassEnvironment` 将该变量继续
传给 bootstrap 和实际 `function_proxy`，再由 SandboxdExecutor 注入沙箱。

handler 将 self instanceID、operationID 和 signal 24 放入 outbound channel，然后同步等待两个
事实：Proxy 的 KillRsp ACK，以及 `YR_CHECKPOINT_HANDOFF_FILE` 已被 RRT 读取完成。两者都完成后
才返回 HTTP 200；并发 checkpoint 返回冲突，任一阶段失败返回非 2xx。该内存协调只关联当前
请求，不新增持久化 recovery intent。

PrepareSnap handler 必须先发送 PrepareSnapRsp，再等待 checkpoint handoff 文件读取完成并通知
HTTP handler，避免 Proxy 等 PrepareSnapRsp、RRT 等 handoff、sandboxd 等 RRT 读取形成环形等待。
`YR_CHECKPOINT_HANDOFF_FILE` 来自 sandboxd runtime capability；缺失时保留现有
`/proc/gvisor/checkpoint` 兼容回退。

- [ ] **Step 4: 运行 RRT 和 Pause/匿名 checkpoint 测试**

```bash
cargo test --manifest-path src/yuanrong/api/rust/Cargo.toml -p rrt-daemon checkpoint
cd src/yuanrong/functionsystem
cmake --build build --target pause_resume_unit_test -j4
build/bin/pause_resume_unit_test --gtest_color=no \
  --gtest_filter='SnapCtrlActorTest.*Anonymous*:SnapCtrlActorPauseTest.*LocalSnapshot*'
```

Expected: Rust checkpoint tests 和非零 C++ 测试全部通过；Proxy ACK 不等待快照完成，HTTP 200
只在 ACK 与 handoff 均完成后返回，PrepareSnapRsp 严格早于 handoff 等待。

- [ ] **Step 5: 分别提交 RRT/YuanRong 与 FunctionSystem**

```bash
git -C src/yuanrong add api/rust/rrt-daemon
git -C src/yuanrong commit -s \
  -m "feat(rrt): add in-sandbox checkpoint endpoint" \
  -m "Accept local checkpoint requests over the configured Unix socket and return success only after the proxy acknowledges ownership and the runtime handoff is consumed."

git -C src/yuanrong/functionsystem add functionsystem/src/common/constants/signal.h \
  functionsystem/src/function_proxy/local_scheduler \
  functionsystem/tests/unit/function_proxy/local_scheduler
git -C src/yuanrong/functionsystem commit -s \
  -m "feat(snapshot): handle anonymous runtime checkpoints" \
  -m "Reuse PrepareSnap and SnapshotRuntime for RRT-originated checkpoints while keeping the source running and replacing only the committed anonymous recovery point."
```

---

### Task 7: 贯通 `failover` 创建参数

**Files:**
- Modify: `src/yuanrong/functionsystem/proto/posix/core_service.proto`
- Modify: `src/yuanrong/functionsystem/proto/posix/resource.proto`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_master/instance_manager/instance_manager_actor.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/instance_control/instance_ctrl_message.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/common/utils/struct_transfer.h`
- Modify: `src/yuanrong/frontend/posix/proto/core_service.proto`
- Modify: `src/yuanrong/frontend/pkg/frontend/api/sandbox/handler.go`
- Modify: `src/yuanrong/frontend/pkg/frontend/api/sandbox/handler_test.go`
- Modify: `src/yuanrong/sandbox-sdk/python/yr_sandbox/sandbox_api.py`
- Modify: `src/yuanrong/sandbox-sdk/python/tests/unit/test_sdk_contract.py`
- Modify: `src/yuanrong/sandbox-sdk/python/tests/unit/test_lifecycle.py`
- Modify: `sdk/python/akernel_sdk/_backends/base.py`
- Modify: `sdk/python/akernel_sdk/_backends/openyuanrong_sandbox.py`
- Modify: `sdk/python/akernel_sdk/_backends/openyuanrong_sdk.py`
- Modify: `sdk/python/akernel_sdk/_backends/openyuanrong_sdk_impl.py`
- Modify: `sdk/python/akernel_sdk/sandbox.py`
- Modify: `sdk/python/tests/unit/test_sandbox.py`
- Modify: `sdk/python/tests/unit/test_backends.py`

**Interfaces:**
- Consumes: public `Sandbox(..., failover=False)` inputs。
- Produces: `core_service.CreateRequest.failover=10` 和 `resources.InstanceInfo.failover=42`，两个 SDK 均默认 false。

- [ ] **Step 1: 写 FunctionSystem proto propagation 失败测试**

```cpp
TEST(InstanceManagerTest, PersistsTypedFailoverFromCreateRequest)
{
    core_service::CreateRequest request;
    request.set_failover(true);
    auto info = BuildInstanceInfo(request);
    EXPECT_TRUE(info.failover());
    EXPECT_EQ(resources::InstanceInfo::descriptor()
                  ->FindFieldByName("failover")->number(), 42);
}
```

该测试直接调用 InstanceManager 当前从 CreateRequest 构造 ScheduleRequest/InstanceInfo 的入口；
若现有 fixture 没有暴露该入口，在 test fixture 中增加：

```cpp
resources::InstanceInfo BuildInstanceInfoForTest(
    const core_service::CreateRequest &request);
```

生产代码和后续任务统一使用 `BuildInstanceInfoForTest` 所覆盖的同一转换函数，不另建测试专用
failover 实现。

- [ ] **Step 2: 增加 typed proto 字段并贯穿复制路径**

```proto
// core_service.CreateRequest
bool failover = 10;

// resources.InstanceInfo
bool failover = 42;
```

同步 FunctionSystem 和 Frontend proto copy。InstanceManager、ScheduleRequest clone、state-machine
message copy 和 reusable template copy 均显式复制 failover，不通过 createOptions 或
RecoverRetryTimes 推断。

- [ ] **Step 3: 写并实现 Frontend failover forwarding**

Go test：

```go
func TestCreateV1HandlerForwardsFailover(t *testing.T) {
    req := CreateV1Request{Name: "sandbox-a", Namespace: "default", Failover: true}
    create := createRequestFromV1(req, "python:3.12-slim")
    raw, err := buildSandboxRawCreateRequest(
        sandboxInvocation{invokeOpts: api.InvokeOptions{}, snapshotID: create.SnapshotID,
                          failover: create.Failover}, create.Name, create.Namespace)
    require.NoError(t, err)
    require.True(t, raw.GetFailover())
}
```

为 CreateRequest/CreateV1Request/sandboxInvocation 增加 bool，并在 raw CreateRequest 设置字段。

- [ ] **Step 4: 写并实现两个 Python SDK 参数测试**

openyuanrong-sandbox：

```python
def test_failover_defaults_false_and_forwards_true(self):
    SandboxClient.create_info = Mock(return_value={"sandboxId": "sandbox-a"})
    Sandbox(failover=True)
    body = SandboxClient.create_info.call_args.args[0]
    self.assertIs(body["failover"], True)
```

AKernel SDK：

```python
def test_failover_is_typed_and_forwarded(self):
    sandbox = Sandbox(failover=True)
    self.assertTrue(fake_backend.last_spec.failover)
```

在 `SandboxSpec` 增加 `failover: bool`，两个 backend 均向 native create options/typed SDK 参数
传递；构造器拒绝非 bool。

- [ ] **Step 5: 运行 FunctionSystem、Frontend 和 SDK 测试**

```bash
cd src/yuanrong/functionsystem
cmake --build build --target functionsystem_unit_test -j4
build/bin/functionsystem_unit_test --gtest_color=no \
  --gtest_filter='InstanceManagerTest.*Failover*:InstanceCtrlMessageTest.*Failover*'
cd ../frontend
go test ./pkg/frontend/api/sandbox -run 'Failover' -count=1
cd ../sandbox-sdk
python3 -m unittest python.tests.unit.test_sdk_contract python.tests.unit.test_lifecycle -v
cd ../../..
PYTHONPATH=sdk/python python3 -m unittest \
  tests.unit.test_sandbox tests.unit.test_backends -v
```

Expected: Python 使用 3.10+；所有命令退出 0。

- [ ] **Step 6: 分仓提交 failover plumbing**

```bash
git -C src/yuanrong/functionsystem add proto/posix \
  functionsystem/src/function_master functionsystem/src/function_proxy \
  functionsystem/src/common functionsystem/tests/unit
git -C src/yuanrong/functionsystem commit -s \
  -m "feat(instance): persist local failover policy" \
  -m "Carry an explicit boolean from CreateRequest into InstanceInfo so snapshot recovery does not reuse the unrelated runtime redeploy counter."

git -C src/yuanrong/frontend add posix/proto pkg/frontend/api/sandbox
git -C src/yuanrong/frontend commit -s \
  -m "feat(sandbox): forward failover create option" \
  -m "Preserve the typed sandbox failover policy on the raw FunctionSystem create request."

git -C src/yuanrong/sandbox-sdk add python
git -C src/yuanrong/sandbox-sdk commit -s \
  -m "feat(sdk): add sandbox failover option" \
  -m "Expose an opt-in boolean that requests same-node recovery from the latest local checkpoint."

git add sdk/python
git commit -s \
  -m "feat(sdk): expose sandbox failover policy" \
  -m "Forward the typed failover option through both AKernel backends while retaining false as the compatibility default."
```

---

### Task 8: 复用 Wait、心跳和 runtime reconcile 执行自动恢复

**Files:**
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/instance_control/instance_ctrl.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/instance_control/instance_ctrl.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/instance_control/instance_ctrl_actor.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/instance_control/instance_ctrl_actor.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/gc_actor/runtime_reconcile_actor.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/gc_actor/runtime_reconcile_actor.h`
- Test: `src/yuanrong/functionsystem/functionsystem/tests/unit/function_proxy/local_scheduler/instance_control/instance_ctrl_test.cpp`
- Test: `src/yuanrong/functionsystem/functionsystem/tests/unit/function_proxy/local_scheduler/gc_actor/runtime_reconcile_actor_test.cpp`
- Test: `src/yuanrong/functionsystem/functionsystem/tests/unit/runtime_manager/executor/sandboxd/sandboxd_executor_test.cpp`

**Interfaces:**
- Consumes: InstanceInfo.failover、FunctionAgentMgr 最新匿名快照查询、Deploy restoreSnapshotID 和 existing Kill/Deploy/SnapStarted/heartbeat paths。
- Produces: `InstanceCtrl::TryLocalSnapshotFailover(instanceID, sourceRuntimeID)`，以及 missing runtime 先恢复后清理的 reconcile 决策。

- [ ] **Step 1: 写 Wait/heartbeat 去重和 RUNNING 不变失败测试**

```cpp
TEST_F(InstanceCtrlTest, WaitAndHeartbeatShareOneLocalFailover)
{
    SeedRunningInstance("sandbox-a", true, "runtime-a");
    SeedLatestSnapshot("sandbox-a", "anon-1");
    auto first = actor_->TryLocalSnapshotFailover("sandbox-a", "runtime-a");
    auto duplicate = actor_->TryLocalSnapshotFailover("sandbox-a", "runtime-a");
    ASSERT_AWAIT_READY(first);
    ASSERT_AWAIT_READY(duplicate);
    EXPECT_EQ(deployCalls_, 1);
    EXPECT_EQ(CurrentState("sandbox-a"), InstanceState::RUNNING);
}

TEST_F(InstanceCtrlTest, MissingSnapshotFailsClosedWithoutColdDeploy);
TEST_F(InstanceCtrlTest, FailoverFalseUsesExistingFatalPath);
TEST_F(InstanceCtrlTest, StaleRuntimeFailureCannotReplaceCurrentRuntime);
```

InstanceCtrl test fixture 增加：

```cpp
void SeedRunningInstance(const std::string &instanceID,
                         bool failover,
                         const std::string &runtimeID);
void SeedLatestSnapshot(const std::string &instanceID,
                        const std::string &snapshotID);
InstanceState CurrentState(const std::string &instanceID) const;
```

- [ ] **Step 2: 实现最小 recoveringInstances_ 仲裁**

在 InstanceCtrlActor 只新增：

```cpp
std::unordered_set<std::string> recoveringInstances_;

litebus::Future<Status> TryLocalSnapshotFailover(
    const std::string &instanceID,
    const std::string &sourceRuntimeID);
```

入口验证当前 state machine 仍为 RUNNING、runtimeID 相等、failover=true、FunctionAgent 本地
且 latest anonymous snapshot 存在。重复调用复用 `localRecoveryPromises_[instanceID]`；不新增
phase enum，不写 InstanceInfo。

- [ ] **Step 3: 复用原节点 Kill/Deploy/Client/SnapStarted/heartbeat**

构造现有 DeployInstanceRequest，复制当前 InstanceInfo 环境/资源/mount/network/端口输入，设置：

```cpp
deployRequest->set_restoresnapshotid(snapshot.snapshotid());
```

源已由 Wait 证明不存在时跳过 Kill；心跳路径发现故障时先走现有 Kill 并标记 expected stop。
Deploy 成功后复用 `UpdateInstance` 的物理事实处理、CreateInstanceClient、SnapStarted 和
StartHeartbeat，但不执行 RUNNING→FAILED/CREATING 状态转换。成功/失败都清理 set/promise；预算
耗尽转 FATAL。

- [ ] **Step 4: 修改 runtime reconcile missingIDs 决策**

`RuntimeReconcileActor::OnReconcileComplete` 对每个 missing current container 调用：

```cpp
instanceCtrl_->TryLocalSnapshotFailover(instanceID, info.runtimeid())
    .Then(litebus::Defer(GetAID(),
        &RuntimeReconcileActor::OnMissingRuntimeRecoveryDone,
        instanceID, std::placeholders::_1));
```

恢复被接管时不调用 `CleanGhostInstance`；failover=false、无 snapshot 或终态失败才继续现有
FATAL/cleanup。Agent 注册已由 Task 5 保证先 List view 后 TriggerOnce。

- [ ] **Step 5: 运行自动恢复和 reconcile 测试**

```bash
cd src/yuanrong/functionsystem
cmake --build build --target functionsystem_unit_test sandboxd_test_bin -j4
build/bin/functionsystem_unit_test --gtest_color=no \
  --gtest_filter='InstanceCtrlTest.*LocalFailover*:RuntimeReconcileActorTest.*Missing*Snapshot*'
build/bin/sandboxd_test_bin --gtest_color=no \
  --gtest_filter='SandboxdExecutorTest.*Wait*:SandboxdExecutorTest.*Restore*'
```

Expected: Wait、heartbeat、Proxy restart missingID 三个入口均只产生一次 restore，InstanceState 在
成功路径始终为 RUNNING。

- [ ] **Step 6: 提交自动 failover**

```bash
git -C src/yuanrong/functionsystem add \
  functionsystem/src/function_proxy/local_scheduler/instance_control \
  functionsystem/src/function_proxy/local_scheduler/gc_actor \
  functionsystem/tests/unit/function_proxy/local_scheduler \
  functionsystem/tests/unit/runtime_manager
git -C src/yuanrong/functionsystem commit -s \
  -m "feat(sandbox): recover failed runtimes from local snapshots" \
  -m "Route Wait, heartbeat, and startup reconciliation through one same-node restore path while keeping logical instances RUNNING and failing closed without a recovery point."
```

---

### Task 9: 验证自动恢复闭环与运维可观测性

**Files:**
- Modify: `src/yuanrong/functionsystem/docs/sandbox-lifecycle-dashboard.json`
- Create: `src/yuanrong/test/smoke/minimal-python/local_snapshot_failover_e2e.py`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_agent/snapshot/local_snapshot_store.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/instance_control/instance_ctrl_actor.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/tests/integration/merged_process_test.cpp`
- Modify: `src/yuanrong/sandbox-sdk/python/examples/reusable_snapshot.py`

**Interfaces:**
- Consumes: Tasks 1-8 complete automatic recovery path。
- Produces: restart/failure E2E、snapshot/failover metrics 与 operator diagnostics。

- [ ] **Step 1: 添加 merged-process restart 失败测试**

测试序列必须执行：

```text
create failover=true
→ trigger anonymous checkpoint
→ verify root/snapshotID/{checkpoint.img,snapshot.meta}
→ restart FunctionAgent/Proxy
→ verify ListLocalSnapshots precedes first reconcile
→ remove source sandbox through sandboxd
→ wait for automatic restore
→ invoke command through original logical instanceID
```

并断言没有 RECOVERING/CREATING 状态写入 InstanceInfo。

- [ ] **Step 2: 添加 Python RRT E2E**

`local_snapshot_failover_e2e.py` 在 sandbox 内写 marker、启动长进程、调用 UDS `/checkpoint`，
随后修改 marker、强杀 sandboxd sandbox。测试等待原 instanceID 恢复并验证 marker/进程回到
checkpoint 时刻，且新命令可执行。

- [ ] **Step 3: 增加指标和日志字段**

增加 local snapshot READY/invalid/orphan count/bytes，checkpoint/list/delete/failover 的
success/failure/result-unknown counter 和 duration；日志统一包含 requestID、instanceID、
snapshotID、generation、sourceRuntimeID 和 reconcile classification，不输出 credentials/env。

- [ ] **Step 4: 运行完整自动恢复质量门**

```bash
cd src/yuanrong/functionsystem
cmake --build build --target functionsystem_unit_test pause_resume_unit_test sandboxd_test_bin -j4
build/bin/functionsystem_unit_test --gtest_color=no
build/bin/pause_resume_unit_test --gtest_color=no
build/bin/sandboxd_test_bin --gtest_color=no
cd ../frontend && go test ./...
cd ../sandbox-sdk && python3 -m unittest discover -s python/tests/unit -v
cargo test --manifest-path ../api/rust/Cargo.toml -p rrt-daemon
```

Expected: 每个命令退出 0；C++ 二进制选择非零测试且零失败。

- [ ] **Step 5: 在 checkpoint-capable standalone 运行 E2E**

```bash
PYTHONPATH=src/yuanrong/sandbox-sdk/python \
python3 src/yuanrong/test/smoke/minimal-python/local_snapshot_failover_e2e.py
```

Expected: create、checkpoint、强杀、自动 restore、原 ID invoke 全部通过；终态每实例只有一个
匿名 snapshot.meta，且无未解释半成品目录。

- [ ] **Step 6: 提交 E2E 与观测**

```bash
git -C src/yuanrong/functionsystem add \
  docs/sandbox-lifecycle-dashboard.json functionsystem/src \
  functionsystem/tests/integration
git -C src/yuanrong/functionsystem commit -s \
  -m "test(snapshot): cover local failover recovery" \
  -m "Exercise agent and proxy restart, missing runtime reconciliation, and observable failure classifications before enabling the SDK reload entry point."

git -C src/yuanrong add test/smoke/minimal-python/local_snapshot_failover_e2e.py
git -C src/yuanrong commit -s \
  -m "test(sandbox): add local snapshot failover smoke" \
  -m "Validate that an RRT checkpoint restores process and file state through the original logical sandbox identity after a forced runtime exit."
```

---

### Task 10: 最后增加主动 `Sandbox.reload() -> bool`

**Files:**
- Modify: `src/yuanrong/functionsystem/functionsystem/src/common/constants/signal.h`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/instance_control/instance_ctrl_actor.cpp`
- Modify: `src/yuanrong/functionsystem/functionsystem/src/function_proxy/local_scheduler/instance_control/instance_ctrl_actor.h`
- Modify: `src/yuanrong/frontend/pkg/frontend/api/api.go`
- Modify: `src/yuanrong/frontend/pkg/frontend/api/sandbox/handler.go`
- Modify: `src/yuanrong/frontend/pkg/frontend/api/sandbox/handler_test.go`
- Modify: `src/yuanrong/sandbox-sdk/python/yr_sandbox/_transport.py`
- Modify: `src/yuanrong/sandbox-sdk/python/yr_sandbox/sandbox_api.py`
- Modify: `src/yuanrong/sandbox-sdk/python/tests/unit/test_lifecycle.py`
- Modify: `sdk/python/akernel_sdk/_backends/base.py`
- Modify: `sdk/python/akernel_sdk/_backends/openyuanrong_sandbox.py`
- Modify: `sdk/python/akernel_sdk/_backends/openyuanrong_sdk.py`
- Modify: `sdk/python/akernel_sdk/sandbox.py`
- Modify: `sdk/python/tests/unit/test_sandbox.py`
- Modify: `sdk/python/README.md`
- Modify: `sdk/python/examples/basic.py`

**Interfaces:**
- Consumes: Task 8 validated same-node recovery entry。
- Produces: signal 25 `INSTANCE_RELOAD_SIGNAL`、Frontend POST reload 和两个 SDK 的 `reload() -> bool`。

- [ ] **Step 1: 写 FunctionSystem reload 失败测试**

```cpp
TEST_F(InstanceCtrlTest, ReloadWithoutSnapshotReturnsFalseAndKeepsSource)
{
    SeedRunningInstance("sandbox-a", false, "runtime-a");
    auto response = Reload("sandbox-a");
    ASSERT_AWAIT_READY(response);
    EXPECT_NE(response.Get().code(), common::ERR_NONE);
    EXPECT_EQ(killCalls_, 0);
}

TEST_F(InstanceCtrlTest, ReloadKillsSourceAndUsesAutomaticRecoveryPath);
```

reload test fixture 增加 `litebus::Future<KillResponse> Reload(const std::string &instanceID)`，其实现
通过 signal 25 进入生产 `InstanceCtrlActor::ProcessKillRequest`，不直接调用私有恢复函数。

- [ ] **Step 2: 增加 signal 25 和 Proxy reload handler**

```cpp
const int32_t INSTANCE_RELOAD_SIGNAL = 25;
```

Reload 先通过 FunctionAgentMgr view 读取 latest anonymous snapshot；缺失直接失败。存在时标记
source Wait 为 expected、Kill 源 sandbox，然后调用 Task 8 的恢复内部函数并使用选定 snapshotID。
主动 reload 不要求 InstanceInfo.failover=true；它是显式用户操作。响应只含成功/失败，不返回
新 instance handler 或物理详情。

- [ ] **Step 3: 添加 Frontend endpoint 和 bool 响应测试**

注册：

```go
sandboxV1Group.POST("/:sandboxID/reload", sandboxTraceHandler(sandbox.ReloadV1Handler))
```

Handler 复用 Pause/Resume 的 JWT 所有权校验和稳定 requestID 机制，向 DirectProxy 发送 signal
25。成功响应 `{"success":true}`，业务失败响应 `{"success":false}` 与非 2xx 状态。

- [ ] **Step 4: 添加 openyuanrong-sandbox reload bool**

```python
def reload(self) -> bool:
    if self._closed:
        return False
    try:
        return bool(self._client.reload(self._sid).get("success", False))
    except SandboxError:
        return False
```

Transport POST `/api/sandbox/v1/sandboxes/{id}/reload`，使用一次生成、重试复用的
`reload-*` requestID。不得替换 `_client`、`_commands`、`_files`、`_shells` 或返回新 Sandbox。

- [ ] **Step 5: 添加 AKernel facade reload bool**

BackendSession protocol 增加 `reload() -> bool`。openyuanrong-sandbox session 转发 native reload；
openyuanrong-sdk session 使用现有 native KillWithResponse signal 25 并等待原 instance ping 恢复。
`akernel_sdk.Sandbox.reload()` 只调用 session.reload，不重建 facade：

```python
def reload(self) -> bool:
    if self._closed or self._session is None:
        return False
    return self._session.reload()
```

- [ ] **Step 6: 运行 reload 测试和完整 SDK 检查**

```bash
cd src/yuanrong/functionsystem
cmake --build build --target functionsystem_unit_test -j4
build/bin/functionsystem_unit_test --gtest_color=no \
  --gtest_filter='InstanceCtrlTest.*Reload*'
cd ../frontend
go test ./pkg/frontend/api/sandbox -run 'Reload' -count=1
cd ../sandbox-sdk
python3 -m unittest python.tests.unit.test_lifecycle -v
cd ../../..
make sdk-check
```

Expected: reload 无快照不 kill，成功路径复用自动恢复，SDK 原对象/facade identity 不变且返回 bool。

- [ ] **Step 7: 运行主动 reload E2E**

扩展 Task 9 smoke：

```text
create failover=true
→ /checkpoint
→ 保存 completed CommandResult 和 Sandbox facade object IDs
→ 修改文件
→ sb.reload() is True
→ completed result 仍可读
→ facade object IDs 不变
→ 文件回到 checkpoint 状态
→ 原 sb 执行新命令成功
```

- [ ] **Step 8: 分仓提交 reload 与文档**

```bash
git -C src/yuanrong/functionsystem add functionsystem/src/common/constants/signal.h \
  functionsystem/src/function_proxy/local_scheduler/instance_control \
  functionsystem/tests/unit/function_proxy/local_scheduler/instance_control
git -C src/yuanrong/functionsystem commit -s \
  -m "feat(sandbox): add explicit local reload" \
  -m "Let an authorized user kill and restore the current sandbox through the already validated same-node snapshot recovery path."

git -C src/yuanrong/frontend add pkg/frontend/api
git -C src/yuanrong/frontend commit -s \
  -m "feat(sandbox): expose reload endpoint" \
  -m "Forward authorized reload requests to the owning proxy and return only the operation success state."

git -C src/yuanrong/sandbox-sdk add python
git -C src/yuanrong/sandbox-sdk commit -s \
  -m "feat(sdk): add boolean sandbox reload" \
  -m "Reuse the existing Sandbox handle and report whether same-node snapshot restoration completed."

git add sdk/python
git commit -s \
  -m "feat(sdk): expose sandbox reload" \
  -m "Add a backend-neutral boolean lifecycle method without replacing command, file, or sandbox facade objects."
```

---

## Final Integration And Handoff

- [ ] 更新 `src/yuanrong` 中 FunctionSystem、Frontend 和 sandbox-sdk gitlinks，提交 YuanRong
  superproject；更新 AKernel 中 YuanRong gitlink，确保每个 gitlink 指向已推送 commit。
- [ ] 从 AKernel 根执行 `git status --short`，确认提交只包含预期 gitlinks、SDK、文档和测试，
  不包含 `.akernel/`、checkpoint.img、snapshot.meta、credentials 或本地构建产物。
- [ ] 重新运行 Task 9 与 Task 10 的完整质量门和两个 E2E，保存实际通过数量、镜像版本、
  FunctionSystem/sandboxd/RRT revision 和终态磁盘目录证据。
- [ ] 对照设计文档逐条检查目标与非目标；确认没有 RECOVERING 状态、recovery intent journal、
  traffic gate、实例绑定恢复校验或 sandboxd 私有快照 RPC。
