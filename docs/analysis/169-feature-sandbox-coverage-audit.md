# feature/sandbox Coverage Audit

Date: 2026-06-06
Branch: `rust-rewrite`
Oracle: `feature/sandbox` @ `3b2de333`
Merge-base: `a1e7a64f`

## Question

Is the Rust rewrite at full functional parity with `feature/sandbox`?

## Method & caveat

`origin/feature/sandbox` has **344** commits since the merge-base (307 non-merge + 37 merges);
`rust-rewrite` has 308. These are **parallel histories** (a C++→Rust rewrite tracked against a
C++ oracle), so the commit count is not a backlog — parity is judged by behavior, not by porting
each commit.

This audit is **bucket-level and grep-grounded**, NOT a per-commit behavioral verification. It
classifies the feature/sandbox commit themes by subsystem and records whether the Rust tree has a
corresponding implementation (hit counts from `functionsystem/src`, excluding tests). It is a map
of where to look, not proof of behavioral equality. Per-subsystem depth verification (and the
broader doc-114 risk register R5–R11) remain separate work.

## Subsystem coverage (Rust src hits, tests excluded)

| Subsystem | feature/sandbox themes | Rust impl present? | Status |
| --- | --- | --- | --- |
| Build / CI / vendor / Bazel / OTel-vendor / gitcode / docs / .gitignore / claude | Bazel migration, vendor mirrors, CI fixes, PR templates | N/A (Rust uses Cargo) | **Decided N/A** — not behavior |
| Core schedule / instance / proxy / master / collective / ds / task | (baseline) | yes | **At parity** — cpp ST 107/107 (doc see evidence dir) |
| DR mode / direct routing | gap2 persistence, kill-route, proxyID, observer node-abnormal, LRU cache | partial | A/G1/C2/D1 **done** (107/107). **GAP: D2** LRU on-demand route query + drop full route watch (0 on-demand hits) |
| Quota / tenant cooldown | QuotaManagerActor, QuotaConfig, LIFO eviction, TenantQuotaExceeded, cooldown | yes (44 hits; `tenant_cooldown_active`, `ErrCreateRateLimited`, rate limiter) | **Has impl — depth unverified** |
| Snapshot / checkpoint | Checkpoint/Restore RPCs, TTL, file mgmt, group sleep/wakeup retry | yes (67 hits) | **Has impl — depth unverified** (TTL / group-wakeup retry to verify) |
| IAM | Keycloak/Casdoor, JWT HMAC, roles, dual-port SSL | yes (53 hits) | **Has impl — depth unverified**; doc-114 R8 (IAM e2e) still open |
| Sandbox / container runtime | Docker/Podman/containerd executor, rootfs, mountpoints, port-forward, cgroup stats | partial (35 hits; `container.rs` = cgroup v2 + namespace, process spawn) | **Needs deep verification** — Rust spawns processes + cgroup/namespace; full OCI/docker/podman executor parity unconfirmed |
| Traefik dynamic routing | TCP→HTTP, TLS, StripPrefix, route cleanup | yes (12 hits) | **Has impl — depth unverified** |
| exec-service | Docker exec gRPC streaming, sessions, heartbeat, PTY, epoll | yes (17 hits) | **Has impl — depth unverified** |
| Idle eviction | IdleActor, traffic reporting, idle instance eviction | yes (13 hits) | **Has impl — depth unverified** |
| GC | LocalGcActor, FATAL/orphan GC, DR-mode GC | yes (5 hits) | **Has impl — depth unverified** |
| Autoscaling | vertical scale, AutoScaleConfig, oneshot, autoscaling observability APIs | partial (8 hits) | **Needs deep verification** |
| Warmup | warmup support/retry/debug | yes (25 hits) | **Has impl — depth unverified** |
| Metadata | rootfs spec, language/BootstrapMetaData, Python 3.12/3.13 | partial | **Needs deep verification** |
| Metrics / OpenTelemetry / trace | OTLP exporter, trace propagation, dashboard, alarms, span context | **0 hits** for opentelemetry/otel/otlp/tracer | **GAP (clear)** — no OTel export / trace propagation in Rust. (Rust uses `tracing` logs; OTel observability not ported.) |
| Misc fixes | master failover InnerDomain, wakeup-to-same-node state machine, low-rel parent-child lifecycle, security header strip / loopback validation | mixed | **Needs per-item verification** |

## Confirmed gaps (high confidence)

1. **Metrics/OpenTelemetry/trace** — 0 Rust hits. No OTLP exporter, no cross-component trace
   propagation, no dashboard metric wiring. Largest clear absence. (Partly an observability-infra
   decision: Rust uses its own `tracing`; full OTel parity is a product call.)
2. **DR D2** — LRU on-demand route query read path absent; cannot drop the full instance-route
   watch in DR mode (see doc 168).
3. **DR E** — health_check dual-port (deploy script; pending lane-ownership check).

## Open behavioral risks (from doc 114, not re-verified here)

R5 SignalResponse.payload ST, R6 event stream, R7 NUMA group bind placement, R8 IAM e2e,
R9 agent deployer breadth, R10 scheduler under scale/failure, R11 multi-proxy partition recovery.

## Honest conclusion

- **Core ST-proven lane: at parity** (cpp ST 107/107 with the latest DR binaries).
- **Subsystem breadth: present but not depth-verified.** Nearly every feature/sandbox subsystem
  (quota, snapshot/checkpoint, IAM, traefik, exec-service, sandbox/container, idle, GC, warmup,
  autoscaling) has a Rust implementation with non-trivial hit counts — the prior parity effort
  ported them. What is NOT established is per-subsystem behavioral equality.
- **Clear gaps:** OpenTelemetry/trace observability (absent), DR D2 (LRU read path), DR E.
- **Therefore: not "fully齐平" in a provable sense, but much closer than commit counts suggest.**
  The remaining work is (a) close the clear gaps, and (b) depth-verify the "has impl, depth
  unverified" subsystems one at a time against the C++ oracle + targeted ST/unit tests.

## Recommended next order

1. Depth-verify the highest-risk subsystems with the smallest ST coverage: **quota cooldown**,
   **sandbox/container executor**, **autoscaling** (these gate real workloads).
2. Decide OpenTelemetry scope (port vs. document-as-divergence) — it is the one clear absence.
3. Close DR **D2** (LRU on-demand query) and **E** (health-check dual-port).
4. Burn down doc-114 R5–R11 behavioral risks with targeted tests.
