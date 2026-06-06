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
| C (kill-route data plane) | **Partial** | `fbfb4073`: busproxy merges route_address/proxy_id hints from `KillRequest` into route records and populates them on forward/execute kill. Missing: DR-gated "require both, else fallback" decision; `runtimeinfo.proxyid = node_id` is never set on the producing/create path (Rust derives owner from `instance_view.owner_node()` instead — parity unconfirmed). |
| A (single-write persistence) | **Closed this session** | G1+G2 below. |
| G1 DR feature flag | **Closed this session** | `config.rs` `enable_direct_routing` (default false, mirrors C++ flag); reachable via `InstanceController`/`BusProxyCoordinator` `Arc<Config>`. Test: `config_extended_test::enable_direct_routing_defaults_false_and_parses`. |
| B (dispatch savedInfo skip) | **Open** | Rust dispatch path is not DR-aware. Note: Rust `persist()` uses `put` (upsert), so the C++ IsFirstPersistence Create-vs-Modify distinction is already satisfied; only the dispatch-side savedInfo skip remains. |
| D (observer node-abnormal) | **Open** | Rust has `domain_scheduler/abnormal_processor` (node_abnormal) but not the DR-conditional observer rewiring to `/yr/abnormal/localscheduler/`. |
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

## Recommended next order

1. **B** — make the local-dispatch path DR-aware (skip savedInfo / proceed with single write at RUNNING).
2. **C remainder** — set `runtime_info.proxy_id = node_id` on the create-call-result path; gate kill
   direct-routing on DR + both-fields-present with explicit observer fallback.
3. **D** — observer node-abnormal subscription under DR mode.
4. **E** — health_check dual-port, only if the rustfs lane owns the deploy scripts.

These remain behavior-only risks until an end-to-end DR-mode ST exists; current closure is unit/contract level.
