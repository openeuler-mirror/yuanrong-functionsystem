# DR Mode (Direct Routing / gap2) Parity Matrix

Date: 2026-06-06
Branch: `rust-rewrite`
Scope: Align the Rust FunctionSystem with the `feature/sandbox` DR-mode feature set introduced by the `enable_direct_routing` / `DirectRoutingConfig` commits at the branch tip.

## Goal

`feature/sandbox` added a "DR mode" (also called direct routing / gap2): a feature flag
(`--enable_direct_routing`) that activates a direct routing read path with an LRU
route cache, on-demand route query, and **single-writer persistence**. This matrix
catalogs the C++ behavior, what the Rust lane already covers, and the remaining gaps,
then records the slice closed in this session.

## C++ references inspected first

```text
functionsystem/src/function_proxy/config/direct_routing_config.h
functionsystem/src/function_proxy/common/flags/flags.cpp            (enable_direct_routing flag, default false)
functionsystem/src/function_proxy/main.cpp                          (DirectRoutingConfig::SetEnabled at startup)
functionsystem/src/function_proxy/common/state_machine/instance_state_machine.cpp  (GetPersistenceType / IsFirstPersistence)
functionsystem/src/function_proxy/local_scheduler/instance_control/instance_ctrl_actor.cpp  (SignalRoute / OnTryDispatchOnLocal / RegisterCreateCallResultCallback)
functionsystem/src/function_proxy/common/observer/observer_actor.cpp (OnNodeAbnormalEvent / abnormal scheduler watch)
scripts/deploy/function_system/health_check.sh                      (dual-port health checks)
```

Relevant C++ commits (since merge-base `a1e7a64f`):

```text
178e4368 feat(gap2): DR mode single-write persistence at RUNNING state
3b2de333 !289 feat[gap2]: DR mode single-write persistence at RUNNING state (+ health_check.sh)
6963629c fix(proxy): skip savedInfo check in DR mode OnTryDispatchOnLocal
94b02900 fix(function_proxy): populate RuntimeInfo.proxyID and require complete routing for DR kill
3001bcd9 feat(observer): add node-abnormal subscription and optimize sync for DR mode
```

## Rust references inspected

```text
functionsystem/src/function_proxy/src/config.rs                 (CLI flags)
functionsystem/src/function_proxy/src/instance_ctrl.rs          (persist_if_policy / persist)
functionsystem/src/function_proxy/src/state_machine.rs          (should_persist_state / should_update_route)
functionsystem/src/common/utils/src/types.rs                    (need_persistence_state / need_update_route_state)
functionsystem/src/function_proxy/src/busproxy/mod.rs           (route records, forward/execute kill)
functionsystem/src/function_proxy/src/busproxy/instance_view.rs (InstanceRouteRecord owner_node_id/proxy_endpoint)
```

## C++ DR behavior set (5 activation points gated on `DirectRoutingConfig::IsEnabled()`)

| ID | Behavior | C++ site | C++ commit |
| --- | --- | --- | --- |
| A | Skip etcd writes for SCHEDULING/CREATING; persist only RUNNING + crash-recovery states; first write at version 0 uses Create | `instance_state_machine.cpp` GetPersistenceType / IsFirstPersistence | 178e4368 |
| B | OnTryDispatchOnLocal skips the savedInfo check in DR mode (version stays 0, single write at RUNNING) | `instance_ctrl_actor.cpp` | 6963629c |
| C | SignalRoute direct-routes a kill only when **both** routeAddress and proxyID are present, else falls back to observer/state-machine; create-call-result sets `runtimeinfo.proxyid = nodeID` | `instance_ctrl_actor.cpp` | 94b02900 |
| D | In DR mode, drop the full instance-route watch; instead watch `/yr/abnormal/localscheduler/` and handle OnNodeAbnormalEvent | `observer_actor.cpp` | 3001bcd9 |
| E | Dual-port health checks in deploy scripts | `health_check.sh` | 3b2de333 / 4129c6c6 |

## Status matrix vs Rust

| ID | Status | Evidence / gap |
| --- | --- | --- |
| Wire surface (proto) | **Closed** | `fbfb4073`: `RuntimeInfo.proxyID`, `KillRequest.routeAddress/proxyID`, snapshot DR fields + mgmt RPCs. `proto_builder_tests::runtime_info_proxy_id_roundtrip_matches_feature_sandbox_wire_contract` |
| A (single-write persistence) | **Closed** | G1+G2 (commit `39768232`). |
| G1 DR feature flag | **Closed** | `config.rs` `enable_direct_routing` (default false, mirrors C++ flag); reachable via `InstanceController`/`BusProxyCoordinator` `Arc<Config>`. Test: `config_extended_test::enable_direct_routing_defaults_false_and_parses`. |
| B (dispatch savedInfo skip) | **N/A (closed by analysis)** | Rust local scheduling (`local_scheduler.rs schedule_local`, `group_schedule`) has no savedInfo etcd-readback / version-conflict duplicate-schedule detection — there is no `OnTryDispatchOnLocal` analogue to gate. Nothing to skip. Note: Rust `persist()` uses upsert `put`, so the C++ IsFirstPersistence Create-vs-Modify distinction is also moot. |
| C1 (runtimeinfo.proxyid on create) | **N/A (intent satisfied)** | Rust propagates instance ownership via the etcd route record JSON (`RouteJson.node_id` → `InstanceRouteRecord.owner_node_id`), not via a `RuntimeInfo` proto on the create-call result. The `RuntimeInfo.proxyID` proto field exists for wire compatibility but is not part of the Rust route mechanism, so owner-proxy id is already known to the kill router. |
| C2 (DR kill-route gating) | **Closed this session** | `merge_route_hint` now applies `KillRequest` hints only when `enable_direct_routing` AND both route_address+proxy_id present (else falls back to route-record/peer path); outgoing forwards stamp hints only in DR mode (`outgoing_kill_route_hint`). Tests: `multi_proxy_routing_test::{forward_kill_uses_request_route_hint_when_route_cache_missing (DR), forward_kill_ignores_route_hint_when_dr_disabled}`. |
| D1 (observer node-abnormal watch) | **Closed this session** | In DR mode `observer.rs run_watch_loops` watches `/yr/abnormal/localscheduler/` and calls `BusProxyCoordinator::on_node_abnormal(node)` to evict routes owned by the abnormal node (analogue of C++ `InstanceView::OnNodeAbnormal`). Test: `multi_proxy_routing_test::on_node_abnormal_evicts_only_that_nodes_routes`. |
| D2 (drop full route watch + on-demand query) | **Deferred (blocked)** | C++ also drops the full instance-route watch in DR mode and relies on an on-demand LRU route-query read path. That read path is not yet ported to Rust, so the route watch is intentionally kept; dropping it without on-demand query would break DR routing. Port the on-demand query first. |
| E (health_check dual-port) | **Open / low priority** | Shared deploy shell scripts; confirm whether the rustfs lane ships its own copy or reuses sandbox scripts before porting. |

## Closed in this session (A + G1)

1. **G1 — DR feature switch.** `config.rs` adds `enable_direct_routing` (`--enable-direct-routing`,
   default `false`), mirroring C++ `flags.cpp` `enable_direct_routing` and `main.cpp`
   `DirectRoutingConfig::SetEnabled`. Implemented as a `Config` field (idiomatic, test-safe,
   no global mutable state) rather than a global atomic; every DR consumer
   (`InstanceController`, `BusProxyCoordinator`) already holds `Arc<Config>`.

2. **A/G2 — single-write persistence at RUNNING.** `yr_common::types::dr_mode_skips_persistence`
   returns true for SCHEDULING/CREATING. `InstanceController::persist_if_policy` returns early
   (skips the etcd write) when `enable_direct_routing` is set and the state is SCHEDULING/CREATING;
   all other states keep the existing policy. Mirrors C++ GetPersistenceType DR fast-path. The
   Create-vs-Modify half is already covered because Rust `persist()` uses an upsert `put`.

Tests (arm64, all pass): `instance_state_tests::dr_mode_skips_only_scheduling_and_creating`,
`config_extended_test::enable_direct_routing_defaults_false_and_parses`; yr-common 33 /
yr-proxy lib 7 / config_extended 22, 0 failed; `cargo check` clean.

## Closed in the B→C→D session (commit pending)

- **B** — verified N/A: Rust has no savedInfo/version-conflict local-dispatch race path.
- **C2** — DR kill-route gating (`merge_route_hint` + `outgoing_kill_route_hint`); C1 verified N/A.
- **D1** — DR-mode abnormal-scheduler watch + `on_node_abnormal` route eviction.

## Remaining

1. **D2** — port the on-demand LRU route-query read path, then drop the full instance-route watch in DR mode (C++ parity for the read side). Currently the route watch is kept.
2. **E** — health_check dual-port, only if the rustfs lane owns the deploy scripts.

These remain behavior-only risks until an end-to-end DR-mode ST exists; current closure is unit/contract level.
