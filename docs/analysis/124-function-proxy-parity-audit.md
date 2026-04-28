# Function Proxy Code Parity Audit

Date: 2026-04-28
Branch: `rust-rewrite`
Scope: Rust `functionsystem/src/function_proxy/**` against C++ 0.8 `functionsystem/src/function_proxy/**`.

## Summary

Rust `function_proxy` is the strongest Rust module for the current ST lane. It passes the accepted source-replacement ST target and has many focused regression tests. The deeper code audit still finds important C++ behaviors that are only partially represented in Rust: invoke custom tags, IAM authorization, full group/range scheduling semantics, NUMA placement, memory/traffic limiting, and observer/sync breadth.

## Source evidence

| Area | C++ evidence | Rust evidence | Finding |
| --- | --- | --- | --- |
| Invoke -> Call field mapping | `function_proxy/busproxy/invocation_handler/invocation_handler.cpp::InvokeRequestToCallRequest` copies function, args, request id, trace id, return object ids, sender id, and `invokeoptions().customtag()` into `CallReq.createoptions` | `functionsystem/src/function_proxy/src/busproxy/invocation_handler.rs::invoke_to_call` copies function, args, trace id, request id, sender id, return ids, span id, but does not copy invoke options custom tags | Concrete behavior gap for callers relying on invoke custom tags. |
| Invoke authorization | C++ `InvocationHandler::Invoke` and `RequestDispatcher::AuthorizeInvoke` call `InternalIAM::Authorize` when IAM is enabled | Rust `InstanceController::schedule_do_authorize_create` is explicitly pass-through; no equivalent invoke IAM path found in inspected proxy routing | IAM create/invoke enforcement is not behavior-parity. |
| Create rate limiting | C++ has `common/rate_limiter/token_bucket_rate_limiter.cpp` and flags such as token bucket capacity/refill | Rust has `CreateRateLimiter` in `local_scheduler.rs`, config `create_rate_limit_per_sec`, and error code tests | Create rate limiting exists but needs C++ parameter/semantic comparison; invoke/memory limiting is broader. |
| Memory/invoke limiting | C++ `InvocationHandler::Invoke` checks `memoryMonitor_->Allow(...)` and returns `ERR_INVOKE_RATE_LIMITED` when memory is insufficient | Rust grep found create rate limiter, but no equivalent memory monitor invoke admission gate in inspected path | Advanced invoke rejection semantics appear missing. |
| Group create/range | C++ `local_group_ctrl_actor.cpp` validates/mutates ranges, enforces group size, persists group info, handles duplicate group, sync, recover, local/remote schedule decisions | Rust `handle_group_create` batches creates, applies bind metadata, supports initial range lower-bound fanout, waits for init, and cleans partial failures | ST path is covered, but full group control semantics are not equivalent. |
| Group bind/NUMA | C++ group scheduling integrates `GroupBinPackAffinity`, scheduler score/context, and local group control | Rust `apply_group_bind_options` maps `GroupOptions.bind` to `scheduling_ops.extension` keys `bind_resource` and `bind_strategy` | First-hop metadata only; full placement/filter/scorer parity is not proven. |
| State persistence | C++ uses state handler/actor/client with DS-backed `DistributedCacheClient` | Rust now uses `StateStore`/`MetaStoreStateStore` and passes state/proxy hardening tests | Black-box durable enough for current Rust-owned state loss, but exact backend parity is not C++. |
| Observer/sync breadth | C++ `observer_actor.cpp` syncs busproxy, instance info, route info, function meta, and IAM data, with partial sync and callbacks | Rust rehydrates local instances from MetaStore and has local resource/reporting views | Current recovery path is ST verified, but observer breadth under partial watch/sync is not fully comparable. |
| Release/start flags | C++ exposes many proxy flags for tracing, metrics, auth, traefik, runtime direct connection, memory detection, user log rolling, etc. | Rust `ProxyCppIgnored` accepts many of these and comments they are not implemented for Rust | Many flags are launch-compatible but not behavior-compatible. |

## Detailed findings

### PROXY-001: invoke custom tags are not copied into CallReq create options

C++ explicitly assigns `*callRequest->mutable_createoptions() = request->invokeoptions().customtag()` in `InvokeRequestToCallRequest`. Rust `invoke_to_call` builds `rs::CallRequest` with defaulted `create_options` and does not copy invoke options custom tags.

Impact: features encoded as invoke custom tags, such as force invoke, route/billing options, or downstream runtime hints, may be dropped by Rust while C++ preserves them.

Classification: `Needs implementation` / `P1`. This is concrete enough to patch after completing the audit.

### PROXY-002: IAM authorization is pass-through or absent on inspected paths

C++ create/invoke paths wire `InternalIAM` and `AuthorizeProxy` into proxy scheduling and dispatch. Rust `schedule_do_authorize_create` currently returns `Ok(())` and is documented as pass-through for E2E. The inspected Rust invoke path did not show an equivalent `AuthorizeInvoke` gate.

Impact: in an IAM-enabled deployment Rust may allow creates/invokes that C++ denies.

Classification: `Needs implementation` / `P1` if IAM is in release scope; otherwise document as unsupported.

### PROXY-003: invoke/memory limiting is not parity-complete

C++ checks memory monitor admission in `InvocationHandler::Invoke` and can reject with `ERR_INVOKE_RATE_LIMITED`. Rust has create-side token bucket enforcement, but the inspected proxy path only proves create limit, not invoke memory limit or the full C++ token bucket flag set.

Impact: overload behavior can diverge under memory pressure or invoke storms.

Classification: `Needs test` / `P2`, potentially `P1` for production overload safety.

### PROXY-004: group bind/NUMA is first-hop compatible, not full placement parity

Rust restores the schema and maps `GroupOptions.bind` into scheduler extensions. C++ goes further through group control, affinity/bin-pack scoring, and NUMA-related scheduling behavior.

Impact: current collective ST proves simple group behavior, but not actual NUMA locality or bin-pack/spread placement semantics.

Classification: `First-hop compatible` / `P1` if NUMA/group placement is release scope.

### PROXY-005: group range/recover/sync parity is incomplete

C++ `LocalGroupCtrlActor` mutates default range values, validates min/max/step, caps range/group sizes, persists group state, handles duplicate group requests, recovers SCHEDULING/FAILED groups, and syncs group instances from metastore.

Rust `handle_group_create` implements the path required by current ST, including initial lower-bound range fanout and cleanup. It does not appear to implement the full C++ group state machine and recover/sync model.

Impact: group operations outside current ST may behave differently, especially after proxy restart or partial scheduling failure.

Classification: `Needs implementation` / `P1` for group production parity, `P2` for current ST-only delivery.

### PROXY-006: state persistence is black-box durable but backend-different

Rust state persistence was hardened in `docs/analysis/117-state-persistence-parity.md` and `docs/analysis/121-state-and-proxy-kill-hardening-proof.md`. It is strong enough for Rust-owned proxy-memory-loss behavior. However, C++ uses DS-backed `DistributedCacheClient`; Rust uses `MetaStoreStateStore`.

Impact: black-box state survival can pass while backend semantics differ for scale, DS auth, TTL, large state, or cross-component observability.

Classification: `Release-policy boundary` / `P2`.

### PROXY-007: many proxy flags are accepted but not behavior-complete

Rust `ProxyCppIgnored` intentionally accepts many C++ flags including tracing, metrics, SSL, traefik, runtime direct connection, memory detection, OOM kill controls, user log rolling, and NPU/GPU collection. This is correct for launch compatibility, but each flag remains a behavior risk until separately closed or declared unsupported.

Impact: operators can pass a flag and get process startup without the corresponding C++ feature.

Classification: `Parse-compatible` / `P2`.

## Strong proxy areas

These areas have strong evidence and should not be re-litigated without new failing evidence:

- Source-replacement ST accepted target: `docs/analysis/120-r4-layout-st-proof.md`.
- Create/init/invoke/result normal flow: ST plus `invocation_handler_test.rs`.
- Actor ordering and caller-scoped sequence handling: previous order-proof docs and current ST.
- Save/load/proxy-memory-loss state behavior: `docs/analysis/117-state-persistence-parity.md`, `docs/analysis/121-state-and-proxy-kill-hardening-proof.md`.
- Startup flag parser acceptance: `docs/analysis/116-binary-flag-parity-gate.md`.

## Suggested next checks

1. Add a unit test proving Rust currently drops or preserves `InvokeOptions.customTag`, then implement C++ parity if confirmed.
2. Decide IAM release scope. If in scope, implement create and invoke authorization against Rust IAM/metastore surfaces.
3. Build a focused group range/recover/sync test matrix from C++ `local_group_ctrl_actor.cpp` behavior.
4. Compare overload behavior: C++ memory monitor/invoke limiter vs Rust create limiter.
5. Mark every `ProxyCppIgnored` flag as one of: implemented elsewhere, intentionally unsupported, or backlog gap.
