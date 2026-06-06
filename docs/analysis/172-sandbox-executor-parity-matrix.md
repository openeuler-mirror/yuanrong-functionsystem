# Sandbox / Container Executor Parity Matrix

Date: 2026-06-06
Branch: `rust-rewrite`
Oracle: `feature/sandbox`
C++ ref: `docs/feature/` sandbox overview; `runtime_manager/executor/sandbox/*`

## Key fact: two executor backends

C++ functionsystem has two execution backends selected by `EXECUTOR_TYPE`:

| Type | Executor | Isolation | Backend |
| --- | --- | --- | --- |
| `RUNTIME` | `RuntimeExecutor` | process-level | direct process spawn (Python/Java/Go/cpp runtimes) |
| `CONTAINER` | `SandboxExecutor` | container-level | **containerd + sandbox-shim** via `runtime.v1.RuntimeLauncher` gRPC (UDS) |

The Rust rewrite implements **only the `RUNTIME` (process) backend** — `runtime_manager/src/executor.rs`
spawns the runtime process directly (cgroup v2 + namespaces + bind-mounts + rlimits). This is the
backend the source-replacement ST exercises (cpp ST 107/107, process mode).

There is **no `SandboxExecutor` / containerd backend in Rust** (no docker/podman/containerd in
`runtime_manager/src`; the doc-169 "35 hits" were `mount`/`rootfs` metadata fields, not a container
executor).

## The sandbox feature cluster (coupled, largely un-ported)

The sandbox doc lists the SandboxExecutor capabilities — and these are exactly the doc-169
"has-impl, depth-unverified" subsystems. They are **coupled to the container backend**:

| Capability | C++ (SandboxExecutor) | Rust | Status |
| --- | --- | --- | --- |
| Container lifecycle (Start/Delete via RuntimeLauncher gRPC/UDS) | yes | none | **GAP** |
| 3 start paths: Normal / WarmUp / Restore | yes | process-Normal only | **GAP** (WarmUp pool, Restore-from-checkpoint container path) |
| Port forwarding (PortManager → Traefik) | yes | proto/plumbing only | **Mostly GAP** (no container port-forward backend) |
| Checkpoint / Restore (snapshot → remote → restore) | yes (checkpoint_orchestrator) | snapshot **RPC surface** present (my fbfb4073) but no sandbox checkpoint backend | **Surface-only / GAP** |
| WarmUp pre-warm pool | yes | warmup metadata refs only | **Mostly GAP** |
| exec-service into container (Docker exec gRPC streaming) | yes | exec plumbing refs only | **Needs verify, likely surface-only** |
| Fail-safe start (SandboxStartGuard), gRPC auto-reconnect | yes | n/a | **GAP** |

## Scope judgment

The `CONTAINER`/sandbox backend is a **new execution model** (containerd isolation), gated by
`EXECUTOR_TYPE::CONTAINER`. Process-mode (`RUNTIME`) workloads — which the ST proves at 107/107 —
are unaffected by its absence. So this is **not a regression**; it is a large **un-ported new
capability cluster**.

This is the **single biggest real functionsystem-scope gap** (vs autoscaling which was Go/k8s
out-of-scope). It is a major track, not a bounded close:
- a `RuntimeLauncher` gRPC (UDS) client + containerd/sandbox-shim integration,
- `SandboxExecutor` lifecycle + the 3 start paths,
- `PortManager` host-port allocation + Traefik route registration,
- checkpoint orchestration (snapshot/upload/download/restore) on the container path,
- WarmUp pool.

## Conclusion / synthesis (updates doc 169)

feature/sandbox's headline additions form a **container/sandbox feature cluster** (sandbox executor
+ checkpoint-via-sandbox + traefik port routing + exec-service + warmup pool + port-forward) that is
**largely un-ported** in Rust — only proto/plumbing surfaces exist. The Rust lane is at parity for
the **process-mode (`RUNTIME`) 0.8 core** (ST 107/107 + DR + quota-core), but the container-mode
feature surface is a separate, large track.

Reclassify in doc 169: traefik / exec-service / checkpoint(full) / warmup / idle(container) are
**coupled to this cluster** and should be treated as one track, not independent subsystems.

## Recommended next order

1. Treat the sandbox/container backend as a **dedicated track** with its own design before any code
   (it is the largest gap and needs the RuntimeLauncher contract + containerd assumptions pinned).
2. Meanwhile, the bounded process-mode gaps remain the higher-ROI closes: quota step 3, DR D2.
3. OpenTelemetry/trace decision still pending (doc 169).
