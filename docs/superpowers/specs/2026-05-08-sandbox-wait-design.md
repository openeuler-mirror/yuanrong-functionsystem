# SandboxExecutor Wait Interface Design

## Background

The `RuntimeLauncher` gRPC service defines a `Wait(WaitRequest) returns (WaitResponse)` RPC.
`ContainerExecutor` already uses this via `DoWaitContainer()` to monitor container process exits.
`SandboxExecutor` currently has no Wait mechanism — sandbox exits are not detected or reported.

## Goal

Add Wait support to `SandboxExecutor` so that:
1. After a sandbox starts successfully, automatically send Wait RPC to containerd.
2. When Wait returns (sandbox exited), report exit status through the existing HealthCheck infrastructure.
3. After SandboxExecutor restarts, resume Wait for sandboxes confirmed via ReconcileRuntimes.

## Design

### New Methods in SandboxExecutor

```cpp
// sandbox_executor.h

// Send Wait gRPC to containerd. Called after successful Start/Restore.
litebus::Future<runtime::v1::WaitResponse> DoWait(
    const std::string &sandboxID, const std::string &runtimeID);

// Callback when Wait RPC returns. Reports exit status via HealthCheck.
litebus::Future<Status> OnWaitDone(
    const std::string &runtimeID,
    const runtime::v1::WaitResponse &response);
```

### DoWait Implementation

```cpp
litebus::Future<runtime::v1::WaitResponse> SandboxExecutor::DoWait(
    const std::string &sandboxID, const std::string &runtimeID)
{
    ASSERT_IF_NULL(containerd_);
    auto req = std::make_shared<runtime::v1::WaitRequest>();
    req->set_id(sandboxID);
    return containerd_
        ->CallAsync("Wait", *req, static_cast<runtime::v1::WaitResponse *>(nullptr),
                    &runtime::v1::RuntimeLauncher::Stub::AsyncWait)
        .Then(litebus::Defer(GetAID(), &SandboxExecutor::OnWaitDone, runtimeID,
                             std::placeholders::_1));
}
```

### OnWaitDone Implementation

```cpp
litebus::Future<Status> SandboxExecutor::OnWaitDone(
    const std::string &runtimeID, const runtime::v1::WaitResponse &response)
{
    // If runtime was already cleaned up (e.g. by Stop/Delete path), skip reporting.
    auto info = stateManager_.Find(runtimeID);
    if (!info.has_value()) {
        YRLOG_INFO("OnWaitDone: runtime({}) already unregistered, skip", runtimeID);
        return Status::OK();
    }

    const auto &instanceID = info->instanceInfo.instanceid();
    auto requestID = litebus::os::Join("update-instance-status-request", runtimeID, '-');

    YRLOG_INFO("{}|OnWaitDone: sandbox exited for runtime({}), exit_code({}), status({})",
               requestID, runtimeID, response.exit_code(), response.status());

    // Report exit status through HealthCheck.
    return healthCheckClient_->SendInstanceStatus(
        instanceID, runtimeID, response.exit_code(), requestID);
}
```

### Integration Points

#### 1. After Normal Start (OnStartDone)

In `OnStartDone`, after `guard->Commit()`, add:

```cpp
DoWait(sandboxID, runtimeID);
```

This fires the Wait RPC immediately after the sandbox is confirmed running.

#### 2. After Restore (OnRestoreDone)

In `OnRestoreDone`, after `guard->Commit()`, add:

```cpp
DoWait(sandboxID, info.runtimeid());
```

#### 3. After Reconcile (confirmedEntries)

In `OnReconcileRuntimes`, in the `confirmedEntries` loop (where stateManager_.Register is called),
add after MarkStartDone:

```cpp
DoWait(entry.containerid(), entry.runtimeid());
```

This re-establishes Wait monitoring for sandboxes that survived a SandboxExecutor restart.

### Stop and Wait Concurrency

Stop and Wait are independent:
- Stop triggers Delete RPC to containerd, which terminates the sandbox.
- Containerd terminates the container and returns the WaitResponse to the pending Wait RPC.
- OnDeleteDone unregisters the runtime from stateManager_.
- OnWaitDone finds the runtime is gone (`Find()` returns empty) and skips reporting.
- No explicit cancellation of Wait is needed.

### No Changes to Other Components

- **RuntimeStateManager** — no changes. Wait state is not tracked; it is fire-and-forget monitoring.
- **HealthCheck / HealthCheckActor** — no changes. OnWaitDone calls the existing `SendInstanceStatus` interface.
- **Proto** — no changes. `WaitRequest`/`WaitResponse` already defined.

## Files to Modify

| File | Change |
|------|--------|
| `functionsystem/src/runtime_manager/executor/sandbox/sandbox_executor.h` | Add `DoWait` and `OnWaitDone` declarations |
| `functionsystem/src/runtime_manager/executor/sandbox/sandbox_executor.cpp` | Implement `DoWait`, `OnWaitDone`; call from `OnStartDone`, `OnRestoreDone`, `OnReconcileRuntimes` |

## Test Plan

1. Start a sandbox → verify Wait RPC is sent to containerd.
2. Sandbox exits naturally → verify OnWaitDone is called and HealthCheck reports the exit.
3. Start a sandbox → Stop before exit → verify Wait returns after Delete, OnWaitDone skips reporting.
4. Restart SandboxExecutor → ReconcileRuntimes confirms sandbox → verify Wait is re-sent.
