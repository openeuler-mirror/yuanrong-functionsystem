# SandboxExecutor (CONTAINER backend) — Rust Port Design

Date: 2026-06-06
Branch: `rust-rewrite`
Goal: black-box drop-in parity for feature/sandbox's container execution backend.
C++ ref: `runtime_manager/executor/sandbox/*` (sandbox_executor 70KB, runtime_state_manager,
sandbox_request_builder, checkpoint_orchestrator), `docs/feature/` sandbox overview.

## 0. Scope & non-goals

- **In scope (Rust functionsystem):** a `CONTAINER` executor backend in `runtime_manager` that is a
  **gRPC client** of the `runtime.v1.RuntimeLauncher` service over UDS (`CONTAINER_EP`), plus the
  state/lifecycle/checkpoint/port logic. Proto contract already present in the Rust tree
  (`proto/posix/runtime_launcher_interface.proto`, package `runtime.v1`).
- **Out of scope (not functionsystem):** containerd and the Go `runtime-launcher`/sandbox-shim
  (separate Go sub-project `functionsystem/runtime-launcher/`). These are deployed alongside; the
  Rust functionsystem only talks to them. Black-box replacement does **not** require porting them.
- **Non-goal:** changing the upper-layer yuanrong / deploy contract. The CONTAINER path is selected
  per-instance, transparently, exactly as C++ does.

## 1. Component mapping (C++ → Rust)

| C++ (litebus actor) | Rust (tokio async) | Notes |
| --- | --- | --- |
| `SandboxExecutor` (actor, owns all state) | `SandboxExecutor` struct owned by one tokio task, or `Arc<SandboxExecutor>` with interior state | replace single-thread-no-lock actor with `Arc` + `DashMap`/`Mutex` (matches existing runtime_manager style) |
| `SandboxExecutorProxy` (cross-thread bridge) | not needed | Rust async methods are directly awaitable; no actor-thread bridge |
| `RuntimeStateManager` (sandboxes/inProgress/pendingDeletes/warmUpMap) | `RuntimeStateManager` with `DashMap`s | see §3 |
| `SandboxRequestBuilder` (proto assembly) | `sandbox_request_builder.rs` pure fns | rootfs/mount/env/port/resources/ckpt_dir → `StartRequest` |
| `CheckpointOrchestrator` (ckpt lifecycle) | `checkpoint_orchestrator.rs` | TakeSnapshot/Download/AddRef/ReleaseRef |
| `PortManager` (singleton host-port alloc) | reuse existing `port_manager.rs` or a sandbox-scoped allocator | host port → `tcp:HOST:CONTAINER` |
| `SandboxStartGuard` (RAII cleanup) | Rust RAII guard (`Drop`) or scopeguard pattern | cleanup on early-return/`?` |
| `RuntimeLauncher` gRPC (UDS) | tonic client over UDS (`CONTAINER_EP`) | tonic supports UDS via `tower`/`hyper-util` connector |

## 2. Integration seam

`runtime_ops.rs::start_instance_op` currently calls `executor::start_runtime_process` directly
(process backend). Add backend selection mirroring C++ `EXECUTOR_TYPE`:

```
start_instance_op(state, req):
    match select_executor(&req):           // RUNTIME (process) vs CONTAINER (sandbox)
        RUNTIME   => executor::start_runtime_process(...)   // existing
        CONTAINER => state.sandbox.start_instance(req).await // new
```

`select_executor` = the same rule C++ uses (e.g., presence of `deployOptions["rootfs"]` /
container runtime type / explicit executor type field — pin from C++ `executor.cpp`/request fields
during impl). Default stays RUNTIME so process-mode behavior is byte-for-byte unchanged.

`stop_instance_op` / snapshot ops dispatch the same way.

## 3. State model (`RuntimeStateManager`)

```rust
struct SandboxInfo { runtime_id, sandbox_id, checkpoint_id, port_mappings_json, instance_info }

struct RuntimeStateManager {
    sandboxes:        DashMap<String, SandboxInfo>,                 // active
    in_progress:      DashMap<String, Shared<StartInstanceResponse>>, // start dedup (same runtimeID → same future)
    pending_deletes:  DashSet<String>,                              // Stop during Start
    warm_up:          DashMap<String, FunctionRuntime>,             // warm pool (mutually exclusive w/ sandboxes)
}
```
- Start dedup: `in_progress` keyed by runtimeID returns a shared future (Rust: `futures::future::Shared` or a `tokio::sync::watch`/`OnceCell` per id).
- `pending_deletes`: if Stop arrives mid-Start, mark; Start completion checks and immediately deletes.

## 4. Three start paths (`start_instance`)

```
start_instance(req):
    guard = SandboxStartGuard::new(state, runtime_id)   // registers in_progress; Drop = full cleanup
    if req.warmup_type != NONE:        -> start_warmup   (Register gRPC; no port, no container)
    else if req has checkpoint_id:     -> start_by_snapshot (Download → AddRef → Build(ckpt_dir) → Start; ReleaseRef on fail)
    else:                              -> start_normal   (ParseForwardPorts → PortManager::request → Build → Start)
    guard.commit()                     // success: move in_progress → sandboxes, keep ports
```
- `SandboxStartGuard` Drop (if not committed) cleans `sandboxes`/`in_progress`/port mappings — Rust `Drop` gives this for free on `?`/early-return.
- After commit, honor any `pending_delete`.

## 5. RuntimeLauncher gRPC client (UDS)

- Endpoint: `CONTAINER_EP` env var (containerd UDS). tonic over UDS:
  `Endpoint::try_from("http://[::]:0")?.connect_with_connector(service_fn(|_| UnixStream::connect(path)))`.
- RPCs used: `Start`, `Delete`, `Wait`, `Register`/`Unregister`/`GetRegistered` (warmup), `Checkpoint`, `Stats` (metrics), `Version` (health).
- **Auto-reconnect**: a 5s connectivity check + lazy reconnect (C++ parity); do not block normal requests. Rust: a background task probing `Version`, channel rebuild on failure.

## 6. Config / env (black-box-identical inputs)

| Input | Source | Use |
| --- | --- | --- |
| `CONTAINER_EP` | env var | containerd/launcher UDS (required for CONTAINER) |
| `deployOptions["rootfs"]` | StartInstanceRequest | s3/image/local rootfs → `RootfsConfig` |
| `deployOptions["network"]` | StartInstanceRequest | `portForwardings` → PortManager → `StartRequest.ports` |
| `deployOptions["mounts"]` | StartInstanceRequest | extra mountpoints |

These already exist on the wire; the Rust request structs must expose them (verify proto coverage during impl).

## 7. Rust-idiom adaptations (vs C++ single-thread actor)

- C++ relies on single-actor-thread no-lock invariants. Rust uses `Arc<...>` + `DashMap` (already
  the runtime_manager pattern) — must reason about concurrency the C++ code got for free. Start dedup
  + pending-delete are the race-sensitive spots; cover with targeted concurrency tests.
- Futures: C++ `litebus::Future` chains → Rust `async/await`. The dedup "return same in-progress
  future" → `Shared` future or an entry-guarded `OnceCell`.

## 8. Verification plan (the hard part)

Black-box parity for CONTAINER cannot be proven without a live `containerd` + Go `runtime-launcher`.
Staged verification:
1. **Unit** (no env): `sandbox_request_builder` proto assembly, `RuntimeStateManager` transitions +
   dedup/pending-delete races, `PortManager` alloc/release, `SandboxStartGuard` cleanup, port-mapping
   JSON encode (`tcp:HOST:CONTAINER`). Achievable now.
2. **gRPC contract** (mock launcher): a tonic mock `RuntimeLauncher` server over UDS to assert the
   exact request shapes + the 3 start-path call sequences. Achievable now (no containerd).
3. **Integration** (real env, deferred): provision containerd + Go `runtime-launcher`, drive a
   container-mode StartInstance, diff observable behavior vs C++. **Env provisioning is a prerequisite
   and currently absent** — this is the gating item for declaring black-box parity.

## 9. Milestones (sequenced)

1. M1 — RuntimeLauncher tonic UDS client + `Version` health + auto-reconnect (unit + mock).
2. M2 — `RuntimeStateManager` + `SandboxStartGuard` + `PortManager` integration (unit, race tests).
3. M3 — `SandboxRequestBuilder` + `start_normal` path (mock-launcher contract test).
4. M4 — `start_warmup` (Register/Unregister) + `start_by_snapshot` (CheckpointOrchestrator) paths.
5. M5 — wire `select_executor` into `runtime_ops` (default RUNTIME unchanged; CONTAINER opt-in).
6. M6 — `Checkpoint`/snapshot path + `Stats` metrics + `Delete`/Wait lifecycle.
7. M7 — integration verification once containerd + runtime-launcher env exists (gating).

Traefik routing (doc 169/172) hangs off M3's port mappings → separate track after M5.

## 10. Risks

- Concurrency (lost the C++ single-thread invariant) — mitigate with race tests at M2.
- UDS + tonic plumbing on the target arch — validate early at M1.
- Proto field coverage: confirm `StartInstanceRequest` carries deployOptions/rootfs/network/mounts in
  the Rust structs; extend proto if missing.
- The integration env (containerd + Go launcher) is **not yet available** — without it, M1–M6 are
  unit/contract-verified only; M7 (true black-box proof) is blocked on provisioning.
