# SandboxExecutor Wait Interface Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add automatic Wait RPC support to SandboxExecutor so sandbox exits are detected and reported through HealthCheck.

**Architecture:** After a sandbox starts successfully (normal start or restore), SandboxExecutor sends a Wait gRPC to containerd. When Wait returns, the exit status is reported via the existing HealthCheck `SendInstanceStatus` interface. After restart, reconciliation re-establishes Wait for confirmed sandboxes.

**Tech Stack:** C++, gRPC (runtime::v1::RuntimeLauncher), litebus actor framework, HealthCheck actor

---

### Task 1: Add DoWait and OnWaitDone declarations to sandbox_executor.h

**Files:**
- Modify: `functionsystem/src/runtime_manager/executor/sandbox/sandbox_executor.h:224-238`

- [ ] **Step 1: Add DoWait and OnWaitDone method declarations**

In `sandbox_executor.h`, in the `// ── gRPC call wrappers` section (after `DoUnregisterWarmUp`, around line 240), add two new method declarations:

```cpp
    litebus::Future<runtime::v1::WaitResponse> DoWait(
        const std::string &sandboxID, const std::string &runtimeID);

    litebus::Future<Status> OnWaitDone(
        const std::string &runtimeID, const runtime::v1::WaitResponse &response);
```

- [ ] **Step 2: Verify header compiles**

Run: `cd /home/yuanrong/wyc/yuanrong-functionsystem && bash run.sh build`
Expected: Build succeeds (methods are declared but not yet defined — linker will fail, but header syntax should be valid). If the build system requires definitions, proceed to Task 2 first.

- [ ] **Step 3: Commit**

```bash
git add functionsystem/src/runtime_manager/executor/sandbox/sandbox_executor.h
git commit -m "feat(sandbox): declare DoWait and OnWaitDone methods"
```

---

### Task 2: Implement DoWait and OnWaitDone in sandbox_executor.cpp

**Files:**
- Modify: `functionsystem/src/runtime_manager/executor/sandbox/sandbox_executor.cpp`

- [ ] **Step 1: Implement DoWait**

Add `DoWait` implementation in the `// ── gRPC wrappers` section of `sandbox_executor.cpp`, after the `DoUnregisterWarmUp` method (after line 908):

```cpp
litebus::Future<runtime::v1::WaitResponse> SandboxExecutor::DoWait(
    const std::string &sandboxID, const std::string &runtimeID)
{
    ASSERT_IF_NULL(containerd_);
    auto req = std::make_shared<runtime::v1::WaitRequest>();
    req->set_id(sandboxID);
    YRLOG_INFO("DoWait: sandbox({}) runtime({})", sandboxID, runtimeID);
    return containerd_
        ->CallAsync("Wait", *req, static_cast<runtime::v1::WaitResponse *>(nullptr),
                    &runtime::v1::RuntimeLauncher::Stub::AsyncWait)
        .Then([req, runtimeID](litebus::Try<runtime::v1::WaitResponse> rsp) -> litebus::Future<runtime::v1::WaitResponse> {
            if (rsp.IsOK()) {
                return rsp.Get();
            }
            auto msg = fmt::format("failed to wait sandbox {}, grpc err: {}", req->id(), rsp.GetErrorCode());
            YRLOG_ERROR("{}", msg);
            runtime::v1::WaitResponse wait{};
            wait.set_status(static_cast<int32_t>(StatusCode::ERR_INNER_COMMUNICATION));
            wait.set_message(msg);
            return wait;
        })
        .Then(litebus::Defer(GetAID(), &SandboxExecutor::OnWaitDone, runtimeID, std::placeholders::_1));
}
```

- [ ] **Step 2: Implement OnWaitDone**

Add `OnWaitDone` immediately after `DoWait`:

```cpp
litebus::Future<Status> SandboxExecutor::OnWaitDone(
    const std::string &runtimeID, const runtime::v1::WaitResponse &response)
{
    auto info = stateManager_.Find(runtimeID);
    if (!info.has_value()) {
        YRLOG_INFO("OnWaitDone: runtime({}) already unregistered, skip", runtimeID);
        return Status::OK();
    }

    const auto &instanceID = info->instanceInfo.instanceid();
    auto requestID = litebus::os::Join("update-instance-status-request", runtimeID, '-');

    YRLOG_INFO("{}|OnWaitDone: sandbox exited for runtime({}), exit_code({}), status({})",
               requestID, runtimeID, response.exit_code(), response.status());

    return healthCheckClient_->SendInstanceStatus(
        instanceID, runtimeID, response.exit_code(), requestID);
}
```

- [ ] **Step 3: Verify build**

Run: `cd /home/yuanrong/wyc/yuanrong-functionsystem && bash run.sh build`
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add functionsystem/src/runtime_manager/executor/sandbox/sandbox_executor.cpp
git commit -m "feat(sandbox): implement DoWait and OnWaitDone"
```

---

### Task 3: Call DoWait from OnStartDone and OnRestoreDone

**Files:**
- Modify: `functionsystem/src/runtime_manager/executor/sandbox/sandbox_executor.cpp`

- [ ] **Step 1: Add DoWait call in OnStartDone**

In `OnStartDone` (around line 234-240), add `DoWait(sandboxID, runtimeID)` after `guard->Commit()` and before the return statement. Change:

```cpp
    guard->Commit();

    ReportMetrics(info.instanceid(), runtimeID, sandboxID,
                  {"yr_app_instance_start_time", " start timestamp", "ms"});
    YRLOG_INFO("{}|{}|StartNormal success: instance({}) runtime({}) sandbox({})", info.traceid(), info.requestid(),
               info.instanceid(), runtimeID, sandboxID);
    return MakeSuccessStartResponse(request, sandboxID);
```

to:

```cpp
    guard->Commit();

    DoWait(sandboxID, runtimeID);

    ReportMetrics(info.instanceid(), runtimeID, sandboxID,
                  {"yr_app_instance_start_time", " start timestamp", "ms"});
    YRLOG_INFO("{}|{}|StartNormal success: instance({}) runtime({}) sandbox({})", info.traceid(), info.requestid(),
               info.instanceid(), runtimeID, sandboxID);
    return MakeSuccessStartResponse(request, sandboxID);
```

- [ ] **Step 2: Add DoWait call in OnRestoreDone**

In `OnRestoreDone` (around line 390-393), add `DoWait(sandboxID, info.runtimeid())` after `guard->Commit()` and before the return statement. Change:

```cpp
    guard->Commit();
    YRLOG_INFO("{}|{}|restore success: instance({}) runtime({}) sandbox({})", info.traceid(), info.requestid(),
               info.instanceid(), info.runtimeid(), sandboxID);
    return MakeSuccessStartResponse(request, sandboxID);
```

to:

```cpp
    guard->Commit();

    DoWait(sandboxID, info.runtimeid());

    YRLOG_INFO("{}|{}|restore success: instance({}) runtime({}) sandbox({})", info.traceid(), info.requestid(),
               info.instanceid(), info.runtimeid(), sandboxID);
    return MakeSuccessStartResponse(request, sandboxID);
```

- [ ] **Step 3: Verify build**

Run: `cd /home/yuanrong/wyc/yuanrong-functionsystem && bash run.sh build`
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add functionsystem/src/runtime_manager/executor/sandbox/sandbox_executor.cpp
git commit -m "feat(sandbox): call DoWait after successful start and restore"
```

---

### Task 4: Call DoWait from OnReconcileRuntimes confirmed entries

**Files:**
- Modify: `functionsystem/src/runtime_manager/executor/sandbox/sandbox_executor.cpp`

- [ ] **Step 1: Add DoWait call in confirmed entries loop**

In `OnReconcileRuntimes`, in the confirmed entries loop (around line 748-753), add `DoWait` after `MarkStartDone`. Change:

```cpp
            if (!stateManager_.IsActive(entry.runtimeid())) {
                stateManager_.Register({entry.runtimeid(), entry.containerid(), {}, {}, {}});
                stateManager_.MarkStartDone(entry.runtimeid());
            }
```

to:

```cpp
            if (!stateManager_.IsActive(entry.runtimeid())) {
                stateManager_.Register({entry.runtimeid(), entry.containerid(), {}, {}, {}});
                stateManager_.MarkStartDone(entry.runtimeid());
                DoWait(entry.containerid(), entry.runtimeid());
            }
```

- [ ] **Step 2: Verify build**

Run: `cd /home/yuanrong/wyc/yuanrong-functionsystem && bash run.sh build`
Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add functionsystem/src/runtime_manager/executor/sandbox/sandbox_executor.cpp
git commit -m "feat(sandbox): resume Wait for reconciled sandboxes after restart"
```

---

### Task 5: Build verification

**Files:** None (verification only)

- [ ] **Step 1: Full build**

Run: `cd /home/yuanrong/wyc/yuanrong-functionsystem && bash run.sh build`
Expected: Clean build with no errors or warnings related to the new code.

- [ ] **Step 2: Run existing unit tests**

Run: `cd /home/yuanrong/wyc/yuanrong-functionsystem && bash run.sh test`
Expected: All existing tests pass. The new methods are exercised only through integration paths, so unit tests are not added in this change.
