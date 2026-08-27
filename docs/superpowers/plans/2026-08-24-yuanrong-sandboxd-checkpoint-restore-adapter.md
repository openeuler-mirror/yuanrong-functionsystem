<!--
Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
Licensed under the Apache License, Version 2.0.
See the LICENSE file in this repository for the complete license text.
-->

# YuanRong to sandboxd Checkpoint/Restore Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make YuanRong FunctionSystem consume sandboxd's public `Checkpoint` plus `Start(checkpoint_info)` contract without changing sandboxd, while preserving Pause/Resume, reusable Snapshot, deterministic Resume, published-port recovery, and exact artifact cleanup.

**Architecture:** Treat sandboxd as the immutable physical-runtime API. FunctionAgent owns checkpoint naming, local artifact inspection, immutable publication, and deletion; RuntimeManager invokes sandboxd, supplies deterministic sandbox/label/port facts, and reconciles ambiguous Restore results through `List`. Runtime-launcher-only RPCs move to a private service so they cannot alter `runtime.v1.SandboxService`.

**Tech Stack:** C++17, protobuf/gRPC, LiteBus actors/futures, GTest 1.10, Go runtime-launcher, CMake, Bazel, DataSystem/OBS SnapshotStorage, sandboxd UDS API.

**Spec:** `docs/superpowers/specs/2026-08-17-pause-checkpoint-convergence-design.md` supplies upper-layer PAUSED/Resume invariants. Its sandboxd wire and artifact-ownership sections are superseded by sandboxd commit `99791478526fd7628d7aba46537d3ad029c971ab`, `api/runtime/v1/sandbox-api.proto`, and `doc/checkpoint-restore.md`.

## Global Constraints

- Do not modify sandboxd source or add a YuanRong-only sandboxd RPC.
- `runtime.v1.SandboxService` methods and field numbers match sandboxd commit `9979147`.
- Restore is `StartRequest.checkpoint_info.checkpoint_dir`; there is no Restore RPC.
- A successful Checkpoint response is empty; YuanRong computes size and SHA-256 locally.
- FunctionAgent/YuanRong owns output naming, remote storage, retention, and deletion.
- Pause and reusable Snapshot always use `leave_running=true`.
- Timeout nesting is sandboxd 180 seconds, gRPC 190 seconds, Proxy/Agent synchronization 210 seconds.
- Initially checkpoint-capable runtime classes are exactly `runsc` and `firecracker`.
- No FunctionAgent, RuntimeManager, checkpoint-root, or PortManager journal is introduced.
- ETCD remains logical authority; sandboxd List remains physical sandbox authority.
- Concrete port mappings and Restore fingerprints are persisted through YuanRong-owned sandbox labels.
- GTest 1.10 runs explicit filters and verifies non-zero selected tests; never use `--gtest_brief`.
- Commits use Conventional Commits, a prose body, and `git commit -s`.

---

### Task 1: Isolate runtime-launcher extensions and lock the sandboxd wire contract

**Files:**
- Create: `proto/posix/runtime_launcher_api.proto`
- Modify: `proto/posix/sandbox_api.proto`
- Modify: `proto/posix/BUILD.bazel`
- Modify: `functionsystem/src/common/proto/CMakeLists.txt`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_executor.h`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_executor.cpp`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/CMakeLists.txt`
- Modify: `runtime-launcher/scripts/generate-proto.sh`
- Modify: `runtime-launcher/internal/server/server.go`
- Modify: `runtime-launcher/internal/service/launcher.go`
- Modify: `runtime-launcher/internal/service/launcher_test.go`
- Modify: `runtime-launcher/internal/state/manager.go`
- Regenerate: `runtime-launcher/api/proto/runtime/v1/sandbox_api.pb.go`
- Regenerate: `runtime-launcher/api/proto/runtime/v1/sandbox_api_grpc.pb.go`
- Create/Regenerate: `runtime-launcher/api/proto/launcher/v1/runtime_launcher_api.pb.go`
- Create/Regenerate: `runtime-launcher/api/proto/launcher/v1/runtime_launcher_api_grpc.pb.go`
- Create: `functionsystem/tests/unit/runtime_manager/executor/sandboxd/sandboxd_proto_contract_test.cpp`
- Modify: `functionsystem/tests/unit/runtime_manager/executor/sandboxd/CMakeLists.txt`

**Interfaces:**
- Consumes: sandboxd `runtime.v1.SandboxService` descriptor at `9979147`.
- Produces: exact public SandboxService and private `runtime.launcher.v1.RuntimeLauncherExtensionService`.

- [ ] **Step 1: Add a failing descriptor contract test**

```cpp
TEST(SandboxdProtoContractTest, MatchesPublicCheckpointRestoreWire)
{
    const auto *service = runtime::v1::SandboxService::descriptor();
    ASSERT_NE(service, nullptr);
    EXPECT_NE(service->FindMethodByName("Checkpoint"), nullptr);
    EXPECT_EQ(service->FindMethodByName("Restore"), nullptr);
    EXPECT_EQ(service->FindMethodByName("DeleteCheckpoint"), nullptr);
    EXPECT_EQ(service->FindMethodByName("Register"), nullptr);

    const auto *start = runtime::v1::StartRequest::descriptor();
    ASSERT_NE(start->FindFieldByName("checkpoint_info"), nullptr);
    EXPECT_EQ(start->FindFieldByName("checkpoint_info")->number(), 21);

    const auto *checkpoint = runtime::v1::CheckpointRequest::descriptor();
    EXPECT_EQ(checkpoint->FindFieldByName("id")->number(), 1);
    EXPECT_EQ(checkpoint->FindFieldByName("checkpoint_dir")->number(), 2);
    EXPECT_EQ(checkpoint->FindFieldByName("timeout_seconds")->number(), 3);
    EXPECT_EQ(checkpoint->FindFieldByName("compress")->number(), 4);
    EXPECT_EQ(checkpoint->FindFieldByName("leave_running")->number(), 5);
    EXPECT_EQ(runtime::v1::CheckpointResponse::descriptor()->field_count(), 0);
}
```

- [ ] **Step 2: Run it and verify the current fork fails**

```bash
cmake --build build --target sandboxd_test_bin -j4
build/bin/sandboxd_test_bin --gtest_color=no \
  --gtest_filter='SandboxdProtoContractTest.*'
```

Expected: non-zero selected tests; failures identify Restore/DeleteCheckpoint or incompatible Checkpoint fields.

- [ ] **Step 3: Move runtime-launcher template RPCs to a private service**

Create this interface in `runtime_launcher_api.proto`:

```proto
syntax = "proto3";
package runtime.launcher.v1;
import "sandbox_api.proto";
option go_package = "runtime-launcher/api/proto/launcher/v1;launcherv1";

service RuntimeLauncherExtensionService {
  rpc Register(RegisterRequest) returns (NormalResponse) {}
  rpc Unregister(UnregisterRequest) returns (NormalResponse) {}
  rpc GetRegistered(GetRegisteredRequest) returns (GetRegisteredResponse) {}
}

message SandboxTemplate {
  string id = 1;
  string runtime = 2;
  runtime.v1.RootfsConfig rootfs = 3;
  bool make_seed = 4;
  repeated string command = 5;
  map<string, string> envs = 6;
  string cwd = 7;
  repeated runtime.v1.Mount mounts = 8;
}
message RegisterRequest { repeated SandboxTemplate templates = 1; }
message UnregisterRequest { repeated string ids = 1; }
message GetRegisteredRequest {}
message GetRegisteredResponse { repeated SandboxTemplate templates = 1; }
message NormalResponse { bool success = 1; string message = 2; }
```

- [ ] **Step 4: Replace the shared proto with sandboxd's public descriptor**

Copy sandboxd `9979147` `sandbox-api.proto` without renumbering or retaining YuanRong-only methods. Preserve only the local Go package option, which does not change wire compatibility.

- [ ] **Step 5: Register both Go services and regenerate**

```go
runtimev1.RegisterSandboxServiceServer(grpcServer, launcher)
launcherv1.RegisterRuntimeLauncherExtensionServiceServer(grpcServer, launcherExtensions)
```

Remove runtime-launcher's old Restore method. Keep its public Checkpoint method returning `codes.Unimplemented`.

Change `internal/state.Manager` template storage from `runtimev1.SandboxTemplate` to `launcherv1.SandboxTemplate`. Keep running-container state on public Start/List types; only template registration belongs to the private package.

```bash
bash runtime-launcher/scripts/generate-proto.sh
cd runtime-launcher && go test ./...
```

Expected: generated public code has checkpoint_info and no Restore/DeleteCheckpoint; Go tests pass.

- [ ] **Step 6: Switch C++ warmup calls to the private extension stub**

Create both clients from the same configured UDS endpoint:

```cpp
sandboxd_ = GrpcClient<runtime::v1::SandboxService>::CreateUdsGrpcClient(endpoint);
runtimeLauncherExtensions_ =
    GrpcClient<runtime::launcher::v1::RuntimeLauncherExtensionService>::CreateUdsGrpcClient(endpoint);
```

Change StartWarmUp, UnregisterWarmUp, DoRegister, DoUnregister, and DoGetRegistered to use `runtime::launcher::v1` request/response types and the private stub. A real sandboxd endpoint returns Unimplemented only when a warmup operation is requested; ordinary Start/Checkpoint/Restore never calls the private service.

- [ ] **Step 7: Run the C++ descriptor and warmup tests**

```bash
cmake --build build --target sandboxd_test_bin -j4
build/bin/sandboxd_test_bin --gtest_color=no \
  --gtest_filter='SandboxdProtoContractTest.*:SandboxdExecutorTest.*WarmUp*'
```

Expected: non-zero tests and all pass; public sandboxd calls compile without private messages.

- [ ] **Step 8: Commit**

```bash
git add proto/posix runtime-launcher functionsystem/src/common/proto \
  functionsystem/src/runtime_manager/executor/sandboxd \
  functionsystem/tests/unit/runtime_manager/executor/sandboxd
git commit -s \
  -m "refactor(sandbox): align the sandboxd wire contract" \
  -m "Move runtime-launcher template operations to a private service and make SandboxService match the pinned sandboxd API."
```

---

### Task 2: Adapt Checkpoint requests and accept the empty response

**Files:**
- Modify: `functionsystem/src/runtime_manager/ckpt/checkpoint_plan.h`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_checkpoint_orchestrator.h`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_checkpoint_orchestrator.cpp`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_executor.cpp`
- Modify: `functionsystem/src/function_agent/agent_service_snapshot.cpp`
- Modify: `functionsystem/src/function_proxy/local_scheduler/function_agent_manager/function_agent_mgr_actor.h`
- Modify: `functionsystem/tests/unit/runtime_manager/executor/sandboxd/sandboxd_executor_test.cpp`
- Modify: `functionsystem/tests/unit/function_agent/agent_service/agent_service_actor_test.cpp`

**Interfaces:**
- Consumes: Task 1 public Checkpoint request/empty response.
- Produces: `CheckpointPlan.timeoutSeconds`, `CheckpointResult{Status}`, and Agent-inspected Snapshot facts.

- [ ] **Step 1: Add failing request and response tests**

```cpp
TEST(CheckpointPlanTest, BuildsSandboxdPublicCheckpointRequest)
{
    CheckpointPlan plan;
    plan.sandboxID = "sbox-source";
    plan.checkpointDirectory = "/checkpoints/attempt-1";
    plan.timeoutSeconds = 180;
    plan.compress = true;
    plan.leaveRuntimeRunning = true;
    const auto request = SandboxdCheckpointOrchestrator::BuildRequest(plan);
    EXPECT_EQ(request.timeout_seconds(), 180U);
    EXPECT_TRUE(request.compress());
    EXPECT_TRUE(request.leave_running());
}

TEST(CheckpointResultTest, EmptySuccessfulResponseIsSuccess)
{
    const auto result = SandboxdCheckpointOrchestrator::MapCheckpointCompletion(
        Status::OK(), runtime::v1::CheckpointResponse{});
    EXPECT_TRUE(result.status.IsOk());
}
```

- [ ] **Step 2: Run and verify failure**

```bash
cmake --build build --target sandboxd_test_bin -j4
build/bin/sandboxd_test_bin --gtest_color=no \
  --gtest_filter='CheckpointPlanTest.BuildsSandboxdPublicCheckpointRequest:CheckpointResultTest.EmptySuccessfulResponseIsSuccess'
```

Expected: compile/assertion failure for missing plan fields, builder, or empty-response mapping.

- [ ] **Step 3: Define the adapter-owned plan and result**

```cpp
struct CheckpointPlan {
    std::string sandboxID;
    std::string checkpointID;
    std::string checkpointDirectory;
    int32_t ttlSeconds{0};
    uint32_t timeoutSeconds{180};
    ArtifactLifecycle lifecycle{ArtifactLifecycle::USER_MANAGED};
    bool compress{true};
    bool leaveRuntimeRunning{true};
};

struct CheckpointResult { Status status; };
```

Reject zero timeout. Do not transmit checkpointID. Force leave-running for Pause and reusable Snapshot.

- [ ] **Step 4: Build the exact request and set the gRPC deadline**

```cpp
runtime::v1::CheckpointRequest SandboxdCheckpointOrchestrator::BuildRequest(
    const CheckpointPlan &plan)
{
    runtime::v1::CheckpointRequest request;
    request.set_id(plan.sandboxID);
    request.set_checkpoint_dir(plan.checkpointDirectory);
    request.set_timeout_seconds(plan.timeoutSeconds);
    request.set_compress(plan.compress);
    request.set_leave_running(plan.leaveRuntimeRunning);
    return request;
}
```

Call `CallAsyncX` with 190 seconds. Treat gRPC OK plus empty response as success; remove artifact-path/size/SHA validation.

- [ ] **Step 5: Move fact establishment to FunctionAgent**

Managed RuntimeManager success contains checkpoint ID but no size/SHA. Let `SnapshotArtifactPublisher` inspect the local artifact; its zero-size/empty-SHA inputs mean “establish facts.” Add an Agent test that the final READY SnapshotInfo contains the inspected size and SHA.

- [ ] **Step 6: Set outer synchronization to 210 seconds and run tests**

```bash
cmake --build build --target sandboxd_test_bin functionsystem_unit_test -j4
build/bin/sandboxd_test_bin --gtest_color=no \
  --gtest_filter='CheckpointPlanTest.*:CheckpointResultTest.*'
build/bin/functionsystem_unit_test --gtest_color=no \
  --gtest_filter='AgentServiceActorTest.*Snapshot*'
```

Expected: both commands select non-zero tests and pass.

- [ ] **Step 7: Commit**

```bash
git add functionsystem/src/runtime_manager functionsystem/src/function_agent \
  functionsystem/src/function_proxy/local_scheduler/function_agent_manager \
  functionsystem/tests/unit/runtime_manager functionsystem/tests/unit/function_agent
git commit -s \
  -m "fix(sandbox): adapt checkpoint calls to sandboxd" \
  -m "Send the public timeout, compression, and leave-running fields and derive artifact facts in FunctionAgent."
```

---

### Task 3: Own and clean sandboxd artifacts locally

**Files:**
- Create: `functionsystem/src/runtime_manager/ckpt/sandboxd_artifact_v1.h`
- Create: `functionsystem/src/runtime_manager/ckpt/sandboxd_artifact_v1.cpp`
- Modify: `functionsystem/src/runtime_manager/ckpt/CMakeLists.txt`
- Modify: `functionsystem/src/runtime_manager/ckpt/pause_artifact_path_manager.h`
- Modify: `functionsystem/src/runtime_manager/ckpt/pause_artifact_path_manager.cpp`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_checkpoint_orchestrator.h`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_checkpoint_orchestrator.cpp`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_executor.cpp`
- Modify: `functionsystem/tests/unit/runtime_manager/ckpt/pause_artifact_path_manager_test.cpp`
- Modify: `functionsystem/tests/unit/runtime_manager/executor/sandboxd/sandboxd_executor_test.cpp`

**Interfaces:**
- Consumes: caller-owned sandboxd v1 output directory.
- Produces: `InspectSandboxdV1Artifact` and `DeleteSandboxdV1Artifact`.

- [ ] **Step 1: Add failing exact-delete tests**

```cpp
TEST_F(PauseArtifactPathManagerTest, DeletesOnlyMatchingCallerOwnedArtifact)
{
    const auto source = manager_->PlanSourceArtifact("snapshot-1");
    std::filesystem::create_directories(source.path.parent_path());
    WriteFile(source.path, "checkpoint-payload");
    const auto facts = InspectSandboxdV1Artifact(source.path.parent_path());
    ASSERT_TRUE(facts.status.IsOk());
    EXPECT_TRUE(DeleteSandboxdV1Artifact(
        source.path.parent_path(), facts.size, facts.sha256).IsOk());
    EXPECT_FALSE(std::filesystem::exists(source.path.parent_path()));
}

TEST_F(PauseArtifactPathManagerTest, DigestMismatchPreservesArtifact)
{
    const auto source = manager_->PlanSourceArtifact("snapshot-2");
    std::filesystem::create_directories(source.path.parent_path());
    WriteFile(source.path, "checkpoint-payload");
    EXPECT_EQ(DeleteSandboxdV1Artifact(source.path.parent_path(), 1, "wrong").StatusCode(),
              StatusCode::SCHEDULE_CONFLICTED);
    EXPECT_TRUE(std::filesystem::exists(source.path));
}
```

- [ ] **Step 2: Run and verify the helper is absent**

```bash
cmake --build build --target pause_resume_unit_test -j4
build/bin/pause_resume_unit_test --gtest_color=no \
  --gtest_filter='PauseArtifactPathManagerTest.DeletesOnlyMatchingCallerOwnedArtifact:PauseArtifactPathManagerTest.DigestMismatchPreservesArtifact'
```

Expected: compile failure for missing artifact helpers.

- [ ] **Step 3: Implement the v1 adapter boundary**

```cpp
struct SandboxdArtifactFacts {
    Status status;
    std::filesystem::path directory;
    std::filesystem::path image;
    uint64_t size{0};
    std::string sha256;
};
```

Require an absolute symlink-free directory and exactly one regular `checkpoint.img`; compute size/SHA without following symlinks. Keep that filename inside this adapter. Exact delete reopens and revalidates path, size, and digest immediately before unlinking.

- [ ] **Step 4: Establish facts for legacy DUMPSTATE before registration**

In `PublishUserManagedSnapshot`, call `InspectSandboxdV1Artifact` after the empty successful Checkpoint response. Fill legacy SnapshotInfo size/SHA before `CkptFileManager::RegisterCheckpoint` zips and uploads the directory. Managed Pause/reusable Snapshot continues to establish facts in FunctionAgent.

- [ ] **Step 5: Remove DeleteCheckpoint RPC usage**

Delete orchestrator DeleteCheckpoint methods and AsyncDeleteCheckpoint calls. Replace Pause-source and reusable-Snapshot cleanup with local exact delete plus ancestor pruning.

- [ ] **Step 6: Run tests and scan symbols**

```bash
cmake --build build --target pause_resume_unit_test sandboxd_test_bin -j4
build/bin/pause_resume_unit_test --gtest_color=no \
  --gtest_filter='PauseArtifactPathManagerTest.*'
build/bin/sandboxd_test_bin --gtest_color=no \
  --gtest_filter='CheckpointPlanTest.*:SandboxdExecutorTest.*Checkpoint*'
rg -n 'DeleteCheckpoint|AsyncDeleteCheckpoint' functionsystem/src/runtime_manager
```

Expected: tests pass with non-zero counts; scan returns no matches.

- [ ] **Step 7: Commit**

```bash
git add functionsystem/src/runtime_manager/ckpt \
  functionsystem/src/runtime_manager/executor/sandboxd functionsystem/tests/unit/runtime_manager
git commit -s \
  -m "refactor(snapshot): own sandboxd artifacts in YuanRong" \
  -m "Inspect and delete exact caller-owned checkpoint artifacts locally instead of using a nonexistent sandboxd cleanup RPC."
```

---

### Task 4: Restore through StartRequest.checkpoint_info

**Files:**
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_executor.h`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_executor.cpp`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_request_builder.h`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_request_builder.cpp`
- Modify: `functionsystem/tests/unit/runtime_manager/executor/sandboxd/sandboxd_executor_test.cpp`
- Modify: `functionsystem/tests/unit/runtime_manager/executor/sandboxd/sandboxd_request_builder_test.cpp`

**Interfaces:**
- Consumes: public StartRequest checkpoint_info and materialized checkpoint directories.
- Produces: `AttachCheckpointInfo` and `DoStartFromCheckpoint`.

- [ ] **Step 1: Add failing Restore-as-Start tests**

```cpp
TEST(SandboxdRequestBuilderTest, RestoreUsesCheckpointInfoOnStart)
{
    runtime::v1::StartRequest request;
    request.set_sandbox_id("sbox-resume-attempt");
    const auto status = SandboxdRequestBuilder::AttachCheckpointInfo(
        request, "/checkpoints/restore/attempt-1");
    EXPECT_TRUE(status.IsOk());
    ASSERT_TRUE(request.has_checkpoint_info());
    EXPECT_EQ(request.checkpoint_info().checkpoint_dir(),
              "/checkpoints/restore/attempt-1");
}
```

- [ ] **Step 2: Run and verify failure**

```bash
cmake --build build --target sandboxd_test_bin -j4
build/bin/sandboxd_test_bin --gtest_color=no \
  --gtest_filter='SandboxdRequestBuilderTest.Restore*'
```

Expected: non-zero tests; failure reports missing AttachCheckpointInfo.

- [ ] **Step 3: Attach a normalized absolute directory**

```cpp
Status SandboxdRequestBuilder::AttachCheckpointInfo(
    runtime::v1::StartRequest &request, const std::string &directory)
{
    const auto normalized = std::filesystem::path(directory).lexically_normal();
    if (!normalized.is_absolute()) {
        return Status(StatusCode::ERR_PARAM_INVALID,
                      "sandboxd restore checkpoint directory must be absolute");
    }
    request.mutable_checkpoint_info()->set_checkpoint_dir(normalized.string());
    return Status::OK();
}
```

- [ ] **Step 4: Replace Restore RPC calls**

Build the normal Start request, set deterministic sandbox_id for trusted Resume, attach the directory containing checkpoint.img, and call AsyncStart. Map StartResponse code/id into `SandboxdRestoreResult`; preserve pre-Start and post-transport-error List reconciliation.

- [ ] **Step 5: Remove old symbols and run tests**

```bash
cmake --build build --target sandboxd_test_bin -j4
build/bin/sandboxd_test_bin --gtest_color=no \
  --gtest_filter='SandboxdRequestBuilderTest.Restore*:SandboxdExecutorTest.*Restore*'
rg -n 'RestoreRequest|AsyncRestore|DoRestore\(' functionsystem/src runtime-launcher
```

Expected: tests pass with non-zero counts; scan returns no production matches.

- [ ] **Step 6: Commit**

```bash
git add functionsystem/src/runtime_manager/executor/sandboxd \
  functionsystem/tests/unit/runtime_manager/executor/sandboxd
git commit -s \
  -m "fix(sandbox): restore checkpoints through Start" \
  -m "Attach checkpoint_info to deterministic Start requests and retain List-based convergence without a Restore RPC."
```

---

### Task 5: Recover concrete port facts from YuanRong-owned sandbox labels

**Files:**
- Create: `functionsystem/src/runtime_manager/executor/sandboxd/restore_physical_identity.h`
- Create: `functionsystem/src/runtime_manager/executor/sandboxd/restore_physical_identity.cpp`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/CMakeLists.txt`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_executor.cpp`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_executor.h`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_request_builder.cpp`
- Modify: `functionsystem/tests/unit/runtime_manager/executor/sandboxd/sandboxd_executor_test.cpp`
- Modify: `functionsystem/tests/unit/runtime_manager/port/port_manager_test.cpp`

**Interfaces:**
- Consumes: YuanRong-preallocated StartRequest ports and sandboxd-persisted labels.
- Produces: `RestorePhysicalIdentity::Encode`, `Decode`, and `Fingerprint`.

- [ ] **Step 1: Add a failing label round-trip test**

```cpp
TEST(RestorePhysicalIdentityTest, RoundTripsCanonicalPortsThroughLabels)
{
    RestorePhysicalIdentity expected;
    expected.runtimeID = "runtime-attempt";
    expected.targetAttemptID = "attempt-1";
    expected.ports = {"tcp:32001:8080", "udp:32002:5353"};
    const auto labels = expected.Encode();
    const auto decoded = RestorePhysicalIdentity::Decode(labels);
    ASSERT_TRUE(decoded.status.IsOk());
    EXPECT_EQ(decoded.identity.ports, expected.ports);
    EXPECT_EQ(decoded.identity.Fingerprint(), expected.Fingerprint());
}
```

- [ ] **Step 2: Run and verify the type is missing**

```bash
cmake --build build --target sandboxd_test_bin -j4
build/bin/sandboxd_test_bin --gtest_color=no \
  --gtest_filter='RestorePhysicalIdentityTest.*'
```

Expected: compile failure for missing RestorePhysicalIdentity.

- [ ] **Step 3: Implement canonical labels and validation**

```cpp
inline constexpr char RESTORE_PORTS_LABEL[] = "openyuanrong.restore.ports";
inline constexpr char RESTORE_FINGERPRINT_LABEL[] = "openyuanrong.restore.fingerprint";
inline constexpr char RESTORE_ATTEMPT_LABEL[] = "openyuanrong.restore.attempt";
```

Serialize ports as a compact JSON array in request order. Fingerprint length-prefixed runtime ID, target attempt ID, snapshot ID, expected size/SHA, and canonical port JSON. Reject malformed protocols, duplicate protocol/host-port pairs, ports outside 1–65535, and mismatched fingerprints.

- [ ] **Step 4: Attach labels before Start**

After port allocation and before AsyncStart, add the encoded identity labels to trusted Restore requests. Do not read StartResponse ports.

- [ ] **Step 5: Recover ports on every convergence path**

- Fresh Start success uses the exact StartRequest ports sent to sandboxd.
- Preexisting exact sandbox from List decodes and validates labels.
- Start response unavailable followed by exact List success decodes labels and rebuilds PortManager.
- Startup Sync decodes labels for every running sandbox and rebuilds reservations.

Delete all reads of StartResponse ports and SandboxStatus ports.

- [ ] **Step 6: Run tests**

```bash
cmake --build build --target sandboxd_test_bin functionsystem_unit_test -j4
build/bin/sandboxd_test_bin --gtest_color=no \
  --gtest_filter='RestorePhysicalIdentityTest.*:SandboxdExecutorTest.*Port*:SandboxdExecutorTest.*Restore*'
build/bin/functionsystem_unit_test --gtest_color=no \
  --gtest_filter='PortManagerRecoveryTest.*'
```

Expected: both commands select non-zero tests and pass.

- [ ] **Step 7: Commit**

```bash
git add functionsystem/src/runtime_manager/executor/sandboxd \
  functionsystem/tests/unit/runtime_manager
git commit -s \
  -m "feat(sandbox): persist restore ports in sandbox labels" \
  -m "Recover YuanRong-allocated port mappings from sandboxd List labels after response loss or process restart."
```

---

### Task 6: Replace Checkpoint replay with unique caller-owned physical attempts

**Files:**
- Modify: `functionsystem/src/runtime_manager/ckpt/pause_artifact_path_manager.h`
- Modify: `functionsystem/src/runtime_manager/ckpt/pause_artifact_path_manager.cpp`
- Modify: `functionsystem/src/function_agent/agent_service_actor.h`
- Modify: `functionsystem/src/function_agent/agent_service_snapshot.cpp`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_executor.cpp`
- Modify: `functionsystem/tests/unit/runtime_manager/ckpt/pause_artifact_path_manager_test.cpp`
- Modify: `functionsystem/tests/unit/function_agent/agent_service/agent_service_actor_test.cpp`
- Modify: `functionsystem/tests/unit/function_proxy/local_scheduler/snap_ctrl/snap_ctrl_actor_test.cpp`

**Interfaces:**
- Consumes: sandboxd's absent-or-empty output-directory precondition and immutable SnapshotStorage publication.
- Produces: `PlanSourceAttempt`, `DeleteSourceAttempt`, and `DeleteAllSourceAttempts`.

- [ ] **Step 1: Add a failing unique-attempt path test**

```cpp
TEST_F(PauseArtifactPathManagerTest, SourceAttemptsNeverReuseOutputDirectory)
{
    const auto first = manager_->PlanSourceAttempt("snapshot-1", "physical-a");
    const auto second = manager_->PlanSourceAttempt("snapshot-1", "physical-b");
    EXPECT_NE(first.path.parent_path(), second.path.parent_path());
    EXPECT_EQ(first.path.filename(), "checkpoint.img");
    EXPECT_EQ(second.path.filename(), "checkpoint.img");
}
```

- [ ] **Step 2: Run and verify the API is absent**

```bash
cmake --build build --target pause_resume_unit_test -j4
build/bin/pause_resume_unit_test --gtest_color=no \
  --gtest_filter='PauseArtifactPathManagerTest.SourceAttemptsNeverReuseOutputDirectory'
```

Expected: compile failure for missing PlanSourceAttempt.

- [ ] **Step 3: Use per-physical-attempt directories**

Use this layout:

```text
<checkpointRoot>/pause/<tenantHash>/<instanceID>/<snapshotID>/attempts/<physicalAttemptID>/checkpoint.img
```

Generate physicalAttemptID once per FunctionAgent pending operation with a random UUID. Same-process retries reuse the in-flight future; retries after Agent restart get a new empty output directory.

- [ ] **Step 4: Converge duplicate physical attempts through immutable storage**

The logical final object remains tenant/instance/snapshot scoped. Conditional publication behaves as follows:

- matching metadata returns replay success;
- conflicting digest returns SCHEDULE_CONFLICTED without replacing the published object;
- transport-unknown publication Stats the final object before deciding.

Exact finalize deletes the physical attempt and prunes the logical attempts subtree when empty.

- [ ] **Step 5: Keep unknown Checkpoint results conservative**

On gRPC failure, List the source sandbox:

- RUNNING reports SOURCE_RUNNING and allows a new physical attempt;
- EXITED reports terminal failure;
- unavailable/UNKNOWN reports resultUnknown and retains the local attempt until exact finalize.

Never infer success from a non-empty local directory.

- [ ] **Step 6: Run path, Agent replay, and Pause tests**

```bash
cmake --build build --target pause_resume_unit_test functionsystem_unit_test -j4
build/bin/pause_resume_unit_test --gtest_color=no \
  --gtest_filter='PauseArtifactPathManagerTest.*:SnapCtrlActorPauseTest.*Checkpoint*'
build/bin/functionsystem_unit_test --gtest_color=no \
  --gtest_filter='AgentServiceActorTest.*Snapshot*:AgentServiceActorTest.*Pause*'
```

Expected: non-zero tests and all pass; no physical attempt reuses a sandboxd output directory.

- [ ] **Step 7: Commit**

```bash
git add functionsystem/src/runtime_manager/ckpt functionsystem/src/function_agent \
  functionsystem/src/runtime_manager/executor/sandboxd functionsystem/tests/unit
git commit -s \
  -m "fix(snapshot): retry checkpoint with unique local attempts" \
  -m "Use a fresh caller-owned output directory per physical checkpoint and converge retries through immutable storage."
```

---

### Task 7: Gate runtime capability and persist restore compatibility metadata

**Files:**
- Modify: `proto/posix/resource.proto`
- Modify: `proto/posix/message.proto`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_executor.h`
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_executor.cpp`
- Modify: `functionsystem/src/function_agent/agent_service_snapshot.cpp`
- Modify: `functionsystem/src/function_agent/common/utils.cpp`
- Modify: `functionsystem/src/common/utils/resume_identity.cpp`
- Modify: `functionsystem/src/common/utils/resume_identity.h`
- Modify: `functionsystem/tests/unit/runtime_manager/executor/sandboxd/sandboxd_executor_test.cpp`
- Modify: `functionsystem/tests/unit/function_agent/common/utils_test.cpp`
- Modify: `functionsystem/tests/unit/function_master/snap_manager/reusable_snapshot_proto_test.cpp`

**Interfaces:**
- Consumes: sandboxd runtime names and YuanRong node architecture.
- Produces: CheckpointCompatibility metadata and `SupportsCheckpoint`.

- [ ] **Step 1: Add a failing runtime-capability test**

```cpp
TEST(SandboxdCheckpointCapabilityTest, SupportsOnlyPublicRuntimes)
{
    EXPECT_TRUE(SandboxdExecutor::SupportsCheckpoint("runsc"));
    EXPECT_TRUE(SandboxdExecutor::SupportsCheckpoint("firecracker"));
    EXPECT_FALSE(SandboxdExecutor::SupportsCheckpoint("kata"));
    EXPECT_FALSE(SandboxdExecutor::SupportsCheckpoint("runc"));
}
```

- [ ] **Step 2: Add backward-compatible SnapshotInfo fields**

```proto
string runtimeClass = 9;
string architecture = 10;
string artifactFormat = 11;
uint32 artifactFormatVersion = 12;
```

Use format `sandboxd-checkpoint-v1`, version 1. Set reusable SnapshotArtifact to the same format instead of `gvisor-checkpoint`.

- [ ] **Step 3: Run and verify capability/fields are absent**

```bash
cmake --build build --target sandboxd_test_bin pause_resume_unit_test -j4
build/bin/sandboxd_test_bin --gtest_color=no \
  --gtest_filter='SandboxdCheckpointCapabilityTest.*'
build/bin/pause_resume_unit_test --gtest_color=no \
  --gtest_filter='ReusableSnapshotProtoTest.*'
```

Expected: compile or descriptor failures.

- [ ] **Step 4: Fail unsupported runtimes before Prepare/Checkpoint**

Intersect ListAvailableRuntimes with the immutable allowlist `{runsc, firecracker}`. Return RUNTIME_MANAGER_PARAMS_INVALID before physical Checkpoint for Kata or runc.

- [ ] **Step 5: Write and validate compatibility facts**

On publish, store source runtime class and architecture. Before materialization/Start, require equal runtime class and architecture, format/version match, and target runtime availability. Document that all-in-one image version skew remains unsupported because sandboxd exposes no runtime-binary compatibility negotiation.

- [ ] **Step 6: Run tests**

```bash
cmake --build build --target sandboxd_test_bin pause_resume_unit_test functionsystem_unit_test -j4
build/bin/sandboxd_test_bin --gtest_color=no \
  --gtest_filter='SandboxdCheckpointCapabilityTest.*:SandboxdExecutorTest.*Unsupported*'
build/bin/functionsystem_unit_test --gtest_color=no \
  --gtest_filter='FunctionAgentUtilsTest.*Snapshot*Compatibility*'
build/bin/pause_resume_unit_test --gtest_color=no \
  --gtest_filter='ReusableSnapshotProtoTest.*'
```

Expected: all commands select non-zero tests and pass.

- [ ] **Step 7: Commit**

```bash
git add proto/posix functionsystem/src/runtime_manager functionsystem/src/function_agent \
  functionsystem/src/common/utils functionsystem/tests/unit
git commit -s \
  -m "feat(snapshot): validate sandboxd artifact compatibility" \
  -m "Persist runtime class, architecture, and artifact format and reject unsupported checkpoint placement."
```

---

### Task 8: Isolate legacy CkptFileManager from managed namespaces

**Files:**
- Modify: `functionsystem/src/runtime_manager/executor/sandboxd/sandboxd_executor.cpp`
- Modify: `functionsystem/src/runtime_manager/ckpt/ckpt_file_manager_actor.cpp`
- Modify: `functionsystem/src/runtime_manager/ckpt/ckpt_file_manager_actor.h`
- Modify: `functionsystem/tests/unit/runtime_manager/ckpt/ckpt_file_manager_actor_test.cpp`
- Modify: `functionsystem/tests/unit/runtime_manager/ckpt/pause_artifact_path_manager_test.cpp`

**Interfaces:**
- Consumes: configured checkpoint root.
- Produces: disjoint `legacy`, `pause`, and `restore` subroots.

- [ ] **Step 1: Add a failing restart-scan test**

```cpp
TEST_F(CkptFileManagerActorTest, RestoreScanNeverIndexesManagedNamespaces)
{
    std::filesystem::create_directories(root_ / "pause/tenant/instance/snapshot");
    std::filesystem::create_directories(root_ / "restore/tenant/instance/snapshot");
    actor_->RestoreCheckpointsFromLocal();
    EXPECT_FALSE(actor_->HasCheckpoint("pause"));
    EXPECT_FALSE(actor_->HasCheckpoint("restore"));
}
```

- [ ] **Step 2: Run and verify current scan fails**

```bash
cmake --build build --target functionsystem_unit_test -j4
build/bin/functionsystem_unit_test --gtest_color=no \
  --gtest_filter='CkptFileManagerActorTest.RestoreScanNeverIndexesManagedNamespaces'
```

Expected: a non-zero selected test reports pause/restore was indexed.

- [ ] **Step 3: Move legacy cache under checkpointRoot/legacy**

Construct CkptFileManagerActor with `${checkpointRoot}/legacy`; never enumerate its parent. Existing remote legacy snapshots remain downloadable, so do not migrate or automatically delete old root-level entries.

- [ ] **Step 4: Run cleanup isolation tests**

```bash
cmake --build build --target functionsystem_unit_test pause_resume_unit_test -j4
build/bin/functionsystem_unit_test --gtest_color=no \
  --gtest_filter='CkptFileManagerActorTest.*'
build/bin/pause_resume_unit_test --gtest_color=no \
  --gtest_filter='PauseArtifactPathManagerTest.*'
```

Expected: tests pass with non-zero counts; TTL cleanup cannot remove pause/restore trees.

- [ ] **Step 5: Commit**

```bash
git add functionsystem/src/runtime_manager functionsystem/tests/unit/runtime_manager
git commit -s \
  -m "fix(snapshot): isolate legacy checkpoint cleanup" \
  -m "Keep CkptFileManager under a dedicated legacy subroot so it cannot claim Pause or Restore artifacts."
```

---

### Task 9: Add a real sandboxd contract test and complete the failure matrix

**Files:**
- Create: `functionsystem/tests/integration/sandboxd_checkpoint_restore_contract_test.cpp`
- Modify: `functionsystem/tests/integration/CMakeLists.txt`
- Modify: `functionsystem/tests/unit/runtime_manager/executor/sandboxd/sandboxd_executor_test.cpp`
- Modify: `functionsystem/tests/unit/function_agent/agent_service/agent_service_actor_test.cpp`
- Modify: `functionsystem/tests/unit/function_proxy/local_scheduler/snap_ctrl/snap_ctrl_actor_test.cpp`
- Modify: `docs/superpowers/specs/2026-08-17-pause-checkpoint-convergence-design.md`
- Modify: `docs/superpowers/plans/2026-08-17-pause-checkpoint-convergence-plan.md`

**Interfaces:**
- Consumes: real sandboxd UDS from CONTAINER_EP and a runsc rootfs from YR_SANDBOXD_TEST_ROOTFS.
- Produces: opt-in `SandboxdCheckpointRestoreContractTest` against the public server.

- [ ] **Step 1: Add a guarded real-server fixture**

```cpp
class SandboxdCheckpointRestoreContractTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        endpoint_ = RequiredEnv("CONTAINER_EP");
        rootfs_ = RequiredEnv("YR_SANDBOXD_TEST_ROOTFS");
        client_ = GrpcClient<runtime::v1::SandboxService>::CreateUdsGrpcClient(endpoint_);
    }
};
```

Register it only when `YR_RUN_SANDBOXD_CONTRACT_TEST=1`; otherwise issue one explicit GTest skip rather than silently selecting zero tests.

- [ ] **Step 2: Add the successful public-contract sequence**

```text
Start deterministic source and marker workload
→ Checkpoint(timeout=180, compress=true, leave_running=true)
→ assert empty successful response and non-empty checkpoint.img
→ List source and assert RUNNING
→ Delete source
→ Start target with checkpoint_info
→ assert restored marker and process state
→ Delete target
→ caller deletes checkpoint directory
```

- [ ] **Step 3: Complete failure and recovery coverage**

Add exact tests for:

- Checkpoint timeout/cancel with source RUNNING and a unique retry attempt;
- Checkpoint response unavailable followed by source List;
- non-empty and symlink checkpoint-directory rejection;
- Kata/runc early rejection;
- Restore Start response unavailable followed by exact List success;
- mismatched Restore fingerprint or port label rejection;
- Agent restart reconstruction from SnapshotStorage plus sandbox labels;
- Pause abort, PAUSED Delete, reusable Snapshot abort, Resume winner, and Resume loser cleanup;
- zero unexplained checkpoint image, sandbox, port reservation, and immutable object after terminal cleanup.

- [ ] **Step 4: Run the complete local quality gate**

```bash
cmake --build build --target sandboxd_test_bin pause_resume_unit_test \
  functionsystem_unit_test functionsystem_integration_test -j4
build/bin/sandboxd_test_bin --gtest_color=no
build/bin/pause_resume_unit_test --gtest_color=no
build/bin/functionsystem_unit_test --gtest_color=no
build/bin/functionsystem_integration_test --gtest_color=no
cd runtime-launcher && go test ./...
```

Expected: every command exits 0; every C++ binary reports non-zero selected and passed counts.

- [ ] **Step 5: Run against unmodified sandboxd containing 9979147**

```bash
test -n "$YR_SANDBOXD_TEST_ROOTFS"
YR_RUN_SANDBOXD_CONTRACT_TEST=1 \
CONTAINER_EP=unix:///run/sandboxd/sandboxd.sock \
build/bin/functionsystem_integration_test --gtest_color=no \
  --gtest_filter='SandboxdCheckpointRestoreContractTest.*'
```

Expected: non-zero selected tests and all pass against the real server.

- [ ] **Step 6: Correct the design and historical plan**

Document these final decisions:

- sandboxd artifacts are caller-owned;
- Restore uses Start with checkpoint_info;
- Checkpoint success response is empty;
- ports and fingerprints are YuanRong-owned labels;
- unique physical attempts replace sandboxd checkpoint-ID replay;
- runtime-launcher extensions use a private service.

Mark the old enhanced-sandboxd wire steps as superseded by this plan without deleting historical context.

- [ ] **Step 7: Commit**

```bash
git add functionsystem/tests docs/superpowers
git commit -s \
  -m "test(sandbox): verify the public checkpoint contract" \
  -m "Exercise YuanRong Checkpoint and Start-based Restore against an unmodified sandboxd and document caller-owned convergence."
```

---

### Task 10: Build the actual YuanRong artifact and pin AKernel integration

**Files:**
- Modify in YuanRong parent: `functionsystem` gitlink
- Modify in AKernel parent: `src/yuanrong` gitlink
- Modify in AKernel parent: `src/sandboxd` gitlink to a revision containing `9979147`
- Modify in AKernel parent: build inputs carrying OPEN_YR_CORE_WHEEL_URL and OPEN_YR_CORE_WHEEL_SHA256
- Modify in AKernel parent: `README.md` only after acceptance passes
- Modify in AKernel parent: `AGENTS.md` if operational assumptions change

**Interfaces:**
- Consumes: tested FunctionSystem commits from Tasks 1–9 and unmodified sandboxd public implementation.
- Produces: one AKernel image containing the matched YuanRong and sandboxd binaries.

- [ ] **Step 1: Build and package FunctionSystem**

```bash
./run.sh build -j4
./run.sh pack
```

Expected: both commands exit 0; the package contains the updated RuntimeManager and regenerated descriptors.

- [ ] **Step 2: Build the YuanRong core wheel and capture its digest**

Build top-level YuanRong in the documented DataSystem → FunctionSystem → Frontend → runtime order, then run:

```bash
sha256sum output/openyuanrong_core-*.whl | tee /tmp/openyuanrong-core.sha256
test "$(wc -l < /tmp/openyuanrong-core.sha256)" -eq 1
```

Expected: exactly one target-architecture wheel digest.

- [ ] **Step 3: Publish the wheel and build AKernel with explicit inputs**

Set `YR_WHEEL_URL` to the published wheel URL and `YR_WHEEL_SHA256` to the Step 2 digest, then run:

```bash
test -n "$YR_WHEEL_URL"
test -n "$YR_WHEEL_SHA256"
make build \
  OPEN_YR_CORE_WHEEL_URL="$YR_WHEEL_URL" \
  OPEN_YR_CORE_WHEEL_SHA256="$YR_WHEEL_SHA256"
```

Expected: build uses the explicit wheel rather than default `openyuanrong_core 0.9.9`.

- [ ] **Step 4: Verify image contents before E2E**

Inside the built image:

```bash
sandboxd --version
yr version
strings /home/yuanrong/functionsystem/bin/runtime_manager \
  | rg 'openyuanrong.restore.ports|sandboxd-checkpoint-v1'
```

Expected: sandboxd revision contains 9979147, YuanRong identifies the new artifact, and both adapter markers exist.

- [ ] **Step 5: Run the acceptance matrix**

```text
runsc: create → Pause → source delete → same-node Resume → cleanup
runsc: TCP+UDP ports → Pause → target Agent restart → cross-node Resume
runsc: Checkpoint response interruption → retry → one immutable object
runsc: concurrent Resume attempts → one CAS winner and exact loser cleanup
firecracker: create → Pause → Resume on a compatible KVM node → cleanup
kata/runc: Pause rejected before PrepareSnap
```

For every terminal case assert ETCD logical state, `sbox list`, PortManager ownership, checkpoint-root contents, and SnapshotStorage objects agree exactly.

- [ ] **Step 6: Update public status only after evidence passes**

Mark Checkpoint/Restore supported only for runtimes whose acceptance matrix passed. If Firecracker infrastructure is unavailable, document runsc support and leave Firecracker unverified.

- [ ] **Step 7: Commit the YuanRong gitlink**

```bash
git add functionsystem
git commit -s \
  -m "chore(deps): update FunctionSystem sandbox adapter" \
  -m "Pin the FunctionSystem revision that consumes sandboxd's public checkpoint and Start-based restore contract."
```

- [ ] **Step 8: Commit AKernel pins separately**

```bash
git add src/yuanrong src/sandboxd README.md AGENTS.md
git commit -s \
  -m "feat(runtime): integrate sandbox checkpoint restore" \
  -m "Pair the tested YuanRong adapter and sandboxd checkpoint implementation in the all-in-one image and document verified runtime support."
```

---

## Final Verification Checklist

- [ ] SandboxService descriptor matches sandboxd 9979147 for every public RPC and field number.
- [ ] Production code has no AsyncRestore, RestoreRequest, AsyncDeleteCheckpoint, or DeleteCheckpointRequest reference.
- [ ] Checkpoint sends non-zero timeout, compression, and leave_running=true for Pause/reusable Snapshot.
- [ ] Empty Checkpoint response reaches FunctionAgent inspection and immutable publication.
- [ ] Restore uses StartRequest checkpoint_info and deterministic sandbox ID.
- [ ] Fresh, replayed, and restart-recovered Restore reconstructs identical ports from labels.
- [ ] Kata/runc fails before runtime Prepare/Checkpoint.
- [ ] Legacy TTL cleanup cannot enumerate Pause or Restore namespaces.
- [ ] Focused, full unit, integration, Go, real runsc, and available Firecracker tests select non-zero cases and report zero failures.
- [ ] Built image contains the tested YuanRong wheel and sandboxd revision containing 9979147.
- [ ] Terminal paths leave no unexplained sandbox, port, local artifact, ETCD route, or remote object.
