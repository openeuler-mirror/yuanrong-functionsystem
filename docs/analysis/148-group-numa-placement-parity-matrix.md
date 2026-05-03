# Group / NUMA / Placement Parity Matrix

Date: 2026-05-03
Branch: `rust-rewrite`
Scope: Subgoal A from `docs/analysis/148-remaining-blackbox-parity-ai-task.md`

## Goal

Compare the official C++ 0.8 group-control / placement / NUMA path against the current Rust implementation, identify which release-scope gaps are still open, and separate:

1. Rust-only logic gaps that can be closed with tests in this slice.
2. Broader state-machine / control-plane gaps that belong to later slices.
3. NUMA runtime-binding behavior that needs hardware or host NUMA capability to prove fully.

## C++ references inspected first

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/utils/numa_binding.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/utils/struct_transfer.h
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_proxy/local_scheduler/local_group_ctrl/local_group_ctrl_actor.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/schedule_decision/performer/group_schedule_performer.cpp
```

## Rust references inspected

```text
functionsystem/src/function_proxy/src/busproxy/invocation_handler.rs
functionsystem/src/function_proxy/src/busproxy/mod.rs
functionsystem/src/function_proxy/src/instance_ctrl.rs
functionsystem/src/function_proxy/tests/invocation_handler_test.rs
functionsystem/src/function_proxy/tests/group_create_test.rs
functionsystem/src/common/utils/src/schedule_plugin/affinity_utils.rs
functionsystem/src/common/utils/src/schedule_plugin/label_affinity.rs
functionsystem/src/domain_scheduler/src/scheduler_framework/policy.rs
functionsystem/src/domain_scheduler/tests/scheduling_test.rs
```

## High-level finding

Current Rust is **parse-and-fanout**, while C++ is **stateful group scheduling**.

- C++ has a real local-group state machine: validate → persist → schedule → reserve/bind → recover/clear.
- Rust currently accepts `CreateReqs`, fans them out to repeated single-instance start requests, and adds only two extension keys (`bind_resource`, `bind_strategy`).
- The actual Rust launch path (`InvocationHandler::handle_group_create` → `BusProxyCoordinator::schedule_instance_via_agent` → `InstanceController::start_instance`) drops scheduling semantics before placement: it passes default clamped resources and `"default"` resource-group to start, and does not propagate `scheduling_ops`, `group_policy`, or `GroupSpec`-style group state into scheduling.

That means current Rust can pass ST while still missing important C++ release-scope group/placement behavior.

## Matrix

| Behavior | C++ 0.8 behavior | Current Rust state | ST coverage | Slice decision |
| --- | --- | --- | --- | --- |
| Group create request count validation | `TransGroupRequest(...)` rejects `requests_size <= 0` and `> 256` | `group_create_empty_requests_succeeds` currently proves Rust returns success for empty groups; no max-size guard found | No | **Close in this slice** with tests + validation |
| Range defaults and validation | `MutatingInstanceRangeParam` fills `min/max/step`; `ValidInstanceRangeParam` enforces `min>0`, `max>=min`, `max<=256`, `step>0`; only one ranged create allowed | Rust only uses `range.min.max(1)` for initial fanout and does not enforce C++ validation/default rules | No | **Close in this slice** for request validation and range-shape behavior |
| Detached instances inside group create | C++ rejects `createoptions["lifecycle"] == "detached"` for grouped scheduling | No matching Rust validation found in `handle_group_create` | No | **Close in this slice** |
| Mixed priority in one group | C++ requires all requests in the group to share the same priority | No matching Rust validation found | No | **Close in this slice** |
| StrictPack affinity consistency check | C++ rejects `StrictPack` groups when member affinity differs | No matching Rust validation found | No | **Close in this slice** |
| Duplicate group request by request ID | `IsDuplicateGroup` returns existing future/result instead of rescheduling | Rust has no persisted group context keyed by group request ID; duplicate `CreateReqs` is not modeled | No | **Document as later control/state-machine work** unless a smaller safe parity subset emerges |
| Group persistence and recovery | C++ `Sync()` / `Recover()` reload and continue `SCHEDULING` groups; failed groups are re-notified | Rust has no equivalent persisted group context / recovery path for `CreateReqs` | No | **Document as later state-machine work** |
| Partial failure cleanup | C++ rolls back failed binds/schedules and reports group failure; `ClearGroup` removes cached state | Rust does basic cleanup of already-started instances on partial scheduling failure, cancellation, or timeout, but without C++ group state persistence | Partial | **Keep current cleanup, strengthen validation first; full state-machine parity remains later work** |
| Kill / clear group | C++ `ClearGroup` deletes cached group state and scheduling instances; response carries `groupid` | Rust `execute_kill` cleans up instances for `grp-*` IDs, but there is no equivalent persisted `GroupContext` lifecycle | Partial | Collective/ST only | **Document current partial behavior; do not overclaim full C++ parity** |
| Suspend / resume group | Not found in the inspected `local_group_ctrl_actor.cpp` path; only fake suspend/resume flag inventory exists elsewhere | Rust accepts related flags in inventory only | Not applicable | **Track under later flag/control-plane matrix, not this slice** |
| Group policy = Spread / Pack / StrictSpread / StrictPack | C++ `GroupBinPackAffinity(...)` converts policy into `grouplb` affinity/anti-affinity; `GroupSchedulePerformer::DoStrictPackSchedule` special-cases `StrictPack` as one virtual aggregated item | Rust currently stores `group_policy` in proto shapes but does not translate it into scheduler-visible `grouplb` affinity in the actual group-create path; `schedule_instance_via_agent` drops group scheduling semantics before placement | No | **Do not overclaim closure here**; keep only the request-shape guards that are safe to close now and document real placement wiring as later work |
| Group bind option parsing | C++ copies bind resource + bind strategy into schedule-option extension fields | Rust already maps `BindOptions` to `bind_resource` / `bind_strategy` extension keys | Unit only | **Preserve and extend with real scheduling consumption where feasible** |
| NUMA CPU/memory bind behavior | C++ `NUMABinding` binds CPU and memory to one or more NUMA nodes and verifies binding | No Rust implementation found for runtime NUMA CPU/memory binding; no consumer of `bind_resource=NUMA` / `bind_strategy=*` found outside tests | No | **Document as environment/hardware/runtime-binding blocker** unless a small no-hardware projection/shape closure is possible |
| Placement/bin-pack/spread enforcement | C++ uses group policy and scheduler framework / resource-view state to influence or enforce placement | Rust scheduler framework already understands label/resource selectors and inner affinity structures, but group-create path does not currently populate those structures or route through a C++-equivalent group scheduler | No | **Leave open in this slice**; document as a real remaining gap rather than pretending request parsing closes scheduler behavior |
| Resource group create (`CreateResourceGroup`) | C++ has real resource-group manager / bundle behavior | Rust `handle_create_resource_group` is still a stub success reply with no side effect | No | Not covered by current ST | **Defer to Subgoal C / MASTER-002**, but keep this matrix explicit that Subgoal A does not close resource-group bundles |

## What current ST actually proves

The accepted single-shot cpp ST (`*-CollectiveTest.InvalidGroupNameTest`) proves only a narrow active lane:

- upper-layer package/build/install/test surfaces still work against the Rust replacement,
- grouped create flows do not crash the currently exercised path,
- the current invalid-group-name scenario still passes.

It does **not** prove:

- duplicate group request replay,
- recovery after restart,
- strict pack / spread / strict spread placement,
- resource-group bundle behavior,
- NUMA CPU/memory binding.

## Release-scope gap split for this slice

### Close now with Rust-only tests + code

These are contained, high-value, and do not require changing upper-layer modules:

1. Group-create validation parity:
   - empty request list rejected
   - oversized group rejected
   - detached lifecycle rejected
   - mixed priority rejected
   - more than one ranged request rejected
   - invalid range parameters rejected
   - `StrictPack` with inconsistent affinity rejected
2. Preserve bind option parsing and keep the C++ parity matrix explicit about where Rust still stops at parse-only metadata.

### Explicitly **not** claimed as closed by this slice

These should be documented honestly even if some smaller guards are fixed:

1. Full C++ `LocalGroupCtrlActor` state-machine parity:
   - persisted `GroupContext`
   - duplicate replay futures
   - `Sync()` / `Recover()`
   - all bind/unbind rollback semantics
2. Full `GroupSchedulePerformer::DoStrictPackSchedule` parity:
   - aggregate-as-one virtual item
   - batch reservation / preemption / rollback interplay
3. Resource-group bundle / migrate / query behavior:
   - belongs to `MASTER-002` / later control-plane work
4. Runtime NUMA CPU/memory binding proof:
   - requires a host/container environment with NUMA support and a Rust implementation that actually consumes NUMA bind requests

## Tests this slice should add first

### Rust proxy tests

- `group_create_empty_requests_is_invalid`
- `group_create_rejects_oversized_batch`
- `group_create_rejects_detached_lifecycle`
- `group_create_rejects_mixed_priorities`
- `group_create_rejects_multiple_range_requests`
- `group_create_rejects_invalid_range_bounds`
- `group_create_rejects_strict_pack_with_different_affinity`
- `group_policy_pack_populates_grouplb_preferred_affinity`
- `group_policy_spread_populates_grouplb_preferred_anti_affinity`
- `group_policy_strict_spread_populates_grouplb_required_anti_affinity`

### Rust scheduler tests

- a focused scheduling fixture proving the injected `grouplb` affinity affects node selection in the expected direction for pack/spread/strict-spread, without overclaiming full C++ state-machine parity

## Slice success criteria

This subgoal can be considered closed only if:

1. The validation and scheduler-visible policy gaps above are covered by failing tests first, then fixed.
2. `docs/analysis/129-rust-gap-backlog.md` is updated for `COMMON-004`, `PROXY-004`, `PROXY-005`, and the placement part of `RUNTIME-003`.
3. A proof doc records host preflight, container build/pack, artifact hashes, proof deploy path, and single-shot ST evidence.
4. Remaining non-closed group state-machine / NUMA binding gaps are written explicitly as exclusions or blockers, not silently implied away.
