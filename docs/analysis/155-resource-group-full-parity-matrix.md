# Resource Group Full Parity Matrix

Date: 2026-05-03
Branch: `rust-rewrite`
Scope: Subgoal A from `docs/analysis/161-production-control-plane-parity-ai-task.md`

## Goal

Audit the remaining C++ 0.8 resource-group state-machine surface against the current Rust FunctionSystem so this slice closes only what is both:

1. release-scope relevant,
2. testable inside the Rust repo,
3. and safe to claim without pretending the whole C++ resource-group manager already exists.

## C++ references inspected first

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_master/resource_group_manager/resource_group_manager_driver.h
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_master/resource_group_manager/resource_group_manager_driver.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_master/resource_group_manager/resource_group_manager_actor.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_proxy/local_scheduler/local_group_ctrl/local_group_ctrl_actor.cpp
```

## Rust references inspected

```text
functionsystem/src/function_master/src/http.rs
functionsystem/src/function_master/src/scheduler.rs
functionsystem/src/function_master/src/schedule_manager.rs
functionsystem/src/function_master/tests/e2e_resource_group.rs
functionsystem/src/function_master/tests/http_compat_test.rs
functionsystem/src/function_master/tests/scheduler_test.rs
functionsystem/src/function_proxy/src/busproxy/invocation_handler.rs
functionsystem/src/function_proxy/tests/invocation_handler_test.rs
functionsystem/src/function_proxy/tests/group_create_test.rs
functionsystem/src/function_proxy/src/instance_recover.rs
proto/posix/core_service.proto
proto/posix/message.proto
```

## High-level finding

Current Rust does **not** have a real counterpart to the C++ `resource_group_manager`.

- C++ creates a persisted `ResourceGroupInfo`, initializes every bundle as `PENDING`, translates the resource group into a `GroupInfo`, schedules it through the global scheduler, updates bundle/node ownership on success, persists status transitions, deletes bundles on cleanup, and reschedules after proxy abnormal events.
- Rust currently has only two thin surfaces:
  1. proxy `CreateResourceGroup` (`RGroupReq`) now fails fast with an explicit internal error instead of falsely claiming success, but still has no scheduling or persistence side effect;
  2. master `/rgroup` returns an empty response shape with no backing resource-group store.
- Rust **does** have group-scheduling machinery for ordinary grouped creates (`CreateReqs`) and partial restart cleanup for stale in-flight instances, but that is not the same state machine as C++ resource-group bundle management.

That means the current accepted ST lane does **not** prove resource-group parity. It only proves the active upper-layer lane does not currently depend on these missing semantics.

## Matrix

| Behavior | C++ 0.8 behavior | Current Rust state | Evidence | Slice decision |
| --- | --- | --- | --- | --- |
| Create resource group | `HandleForwardCreateResourceGroup` builds `ResourceGroupInfo`, sets group/bundle status `PENDING`, persists to metastore, schedules via global scheduler, then transitions to `CREATED` or `FAILED` | Proxy `handle_create_resource_group` now returns explicit `ERR_INNER_SYSTEM_ERROR` with no side effects; Rust still does not contact master or persist anything | C++ `resource_group_manager_actor.cpp:391-533`; Rust `function_proxy/src/busproxy/invocation_handler.rs:922-949` | **Still the highest-priority gap**; this slice removes fake success, full state machine remains larger follow-up |
| Query resource group | `/rgroup` POST returns `QueryResourceGroupResponse` populated from cached resource groups; slave forwards query to master | Rust `/rgroup` always returns empty `groups/rGroup` | C++ `resource_group_manager_driver.h:34-77`, `resource_group_manager_actor.cpp:982-1072`; Rust `function_master/src/http.rs:814-849` | **Open**; do not claim parity until a real store exists |
| Delete resource group | delete pre-check waits for pending create, removes bundles from proxies, deletes metastore entry, then removes in-memory cache | no Rust resource-group delete path found | C++ `resource_group_manager_actor.cpp:535-729` | **Open**; large state-machine work |
| Duplicate group name | duplicate name returns `ERR_PARAM_INVALID`, message `resource group name exists` | no Rust duplicate-name handling because create is stubbed | C++ `resource_group_manager_actor.cpp:402-409` | **Open**; could only close with a real RG store |
| Duplicate create request id | repeated create request is deduped instead of rescheduled | no Rust request-id dedupe for resource groups | C++ `resource_group_manager_actor.cpp:394-399`; `local_group_ctrl_actor.cpp:188-202` | **Open**; stateful follow-up |
| Pending / created / failed transitions | resource group and bundles start `PENDING`, then move to `CREATED` or `FAILED` with persisted message | no Rust RG/bundle status model beyond empty HTTP/proto shapes | C++ `resource_group_manager_actor.cpp:58-95`, `467-533`; proto `message.proto:809-868` | **Open**; requires real store + scheduler integration |
| Detached vs non-detached cleanup | non-detached RG freed on driver exit or job kill; detached parent instance deletion also frees RG | no Rust RG lifecycle handling | C++ `resource_group_manager_actor.cpp:562-591` | **Open**; stateful follow-up |
| Driver exit cleanup | RG manager driver stops actor/http server; exited driver/job can trigger RG cleanup | no Rust RG manager/driver exists | C++ `resource_group_manager_driver.cpp:22-59`, `resource_group_manager_actor.cpp:562-569` | **Open** |
| Job kill cleanup | non-detached RGs freed on kill job | no Rust equivalent | C++ `resource_group_manager_actor.cpp:581-589` | **Open** |
| Bundle creation / deletion | bundle IDs are synthesized, labels/resources copied, function-proxy ownership filled after scheduling, then removed on delete | no Rust bundle model | C++ `resource_group_manager_actor.cpp:51-122`, `489-507`, `622-729`, `1153-1177` | **Open**; large |
| Partial failure behavior | scheduling failure marks RG `FAILED`; range/group failures roll back unscheduled members and clean reserves/binds | Rust group-create has limited cleanup for ordinary grouped instance creates, but not RG bundles | C++ `resource_group_manager_actor.cpp:467-487`; `local_group_ctrl_actor.cpp:573-625`, `1190-1291`; Rust `function_proxy/src/busproxy/invocation_handler.rs:820-887` | **Partially analogous only**; do not overclaim RG parity |
| Sync / recover / migrate | RG manager syncs from metastore; local group control compares synced state and clears stale cache; abnormal proxy triggers reschedule | Rust only has instance rehydrate / stale in-flight failure, no RG sync/recover path | C++ `resource_group_manager_actor.cpp:198-262`, `731-834`; `local_group_ctrl_actor.cpp:1307-1354`; Rust `function_proxy/src/instance_recover.rs:21-62` | **Open** |
| Response code / message contract | create/delete failures return mapped C++ error codes/messages; query returns actual `QueryResourceGroupResponse` data | Rust create no longer lies with success, but still returns a Rust-specific fail-fast internal error instead of real C++ create semantics; query still returns unconditional empty success shape | C++ `resource_group_manager_actor.cpp:30-49`, `405-407`, `463-487`, `555-557`, `695-717`; Rust `invocation_handler.rs:937-946`, `http.rs:834-848` | **Partially bounded**; fake success is closed, real parity remains open |

## What current Rust tests already prove

### Covered today

- ordinary grouped scheduling rejects empty `group_id` and empty request lists:
  - `function_master/tests/e2e_resource_group.rs:14-43`
  - `function_master/tests/scheduler_test.rs:12-24`
- ordinary grouped scheduling can batch sub-requests once topology exists:
  - `function_master/tests/e2e_resource_group.rs:45-77`
- group-instance query over instance metadata works:
  - `function_master/tests/e2e_resource_group.rs:79-197`
- `/rgroup` currently returns a stable empty JSON shape:
  - `function_master/tests/e2e_resource_group.rs:118-141`
  - `function_master/tests/http_compat_test.rs:712-738`
- proxy `CreateResourceGroup` now fails fast instead of returning a stub success:
  - `function_proxy/tests/invocation_handler_test.rs:741-763`

### Not covered today

- any real resource-group creation side effect
- any resource-group duplication logic
- any bundle lifecycle or proxy ownership updates
- any resource-group deletion / cleanup behavior
- any metastore-backed resource-group query result
- any reschedule / abnormal-proxy recovery behavior

## What current ST actually proves

The accepted single-shot ST still proves only the active replacement lane:

1. upper-layer build / pack / install / single-shot test still works with the current Rust package,
2. existing grouped-create and management APIs used by that lane do not crash,
3. but resource-group state-machine breadth is still outside what ST exercises.

It does **not** prove:

- `CreateResourceGroup`
- `/rgroup` returning real resource groups
- delete / cleanup of resource groups
- bundle reschedule after proxy abnormal
- metastore sync / recover / migrate for resource groups

## Smallest release-scope closure candidates

### Worth closing in this slice

1. **Remove the false-success `CreateResourceGroup` reply in proxy.**
   - This slice now does that: Rust returns an explicit internal error instead of claiming a reservation succeeded.
   - The full resource-group manager, store, scheduler bridge, and bundle state are still absent.
2. **Document the master `/rgroup` surface as query-shape only, not real parity.**
   - Until a resource-group store exists, Rust must not be described as supporting real RG query semantics.

### Explicitly too large for this slice

1. a real Rust `resource_group_manager` equivalent on master,
2. proxy-to-master resource-group creation bridge plus persistence,
3. bundle ownership / deletion / abnormal reschedule,
4. RG sync / recover / migrate from metastore,
5. full delete / detached / job-kill lifecycle parity.

## Recommended slice outcome

This slice should **not** pretend to finish `MASTER-002`.

The honest goal is narrower:

1. record the full state-machine gap in docs,
2. stop Rust from silently claiming RG creation succeeded when it did nothing,
3. keep the release claim explicit that resource-group state-machine parity remains outside the currently accepted black-box lane.
