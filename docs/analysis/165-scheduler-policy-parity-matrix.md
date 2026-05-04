# Scheduler Policy Parity Matrix

Date: 2026-05-03
Branch: `rust-rewrite`
Scope: bounded Subgoal E from `docs/analysis/161-production-control-plane-parity-ai-task.md`

## Goal

Compare the C++ 0.8 domain scheduler policy/filter surface against the current Rust scheduler framework, then choose the smallest production-meaningful closure for this slice.

This matrix intentionally separates:

1. **already implemented but not yet well-proved policy paths** that can be closed with bounded Rust-side work,
2. **deeper scheduler-control-plane gaps** that should remain explicit boundaries for the final release decision.

## C++ references inspected first

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/scheduler_framework/framework/policy.h
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/scheduler_framework/framework/framework_impl.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/scheduler_framework/utils/label_affinity_utils.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/domain_scheduler/domain_group_control/domain_group_ctrl.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/domain_scheduler/underlayer_scheduler_manager/underlayer_sched_mgr.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/resource_view/resource_poller.cpp
```

## Rust references inspected

```text
functionsystem/src/domain_scheduler/src/scheduler_framework/policy.rs
functionsystem/src/domain_scheduler/src/scheduler_framework/framework.rs
functionsystem/src/domain_scheduler/src/function_meta.rs
functionsystem/src/domain_scheduler/src/nodes.rs
functionsystem/src/domain_scheduler/src/resource_view.rs
functionsystem/src/domain_scheduler/src/group.rs
functionsystem/src/domain_scheduler/src/scheduler.rs
functionsystem/src/domain_scheduler/tests/scheduling_test.rs
functionsystem/src/domain_scheduler/tests/resource_view_contract_test.rs
```

## High-level finding

Rust already has a real scheduler framework with filter/scorer plugins, resource-view projection, label-aware scheduling, disk/heterogeneous scoring, failure-domain hints, and bounded gang/group reservation behavior.

What is still much broader in C++ is not a single missing filter, but the surrounding control plane:

- underlayer scheduler manager actor orchestration,
- domain-group control actor semantics,
- taint/toleration breadth,
- migration/preemption behavior breadth,
- full affinity selector semantics and priorities.

The best small closure in this slice is therefore **not** to promise all scheduler-policy parity. It is to harden and prove a narrower path that already matters to placement correctness:

1. selector expression completeness for the currently implemented Rust JSON selector path, using the C++ label-affinity operator set as the behavioral reference for supported operators,
2. failure-domain filtering proof for the current `labels.zone` fallback path in the Rust resource-view/node projection chain.

That keeps the slice meaningful without overclaiming underlayer/group/preemption parity.

## Matrix

| Behavior | C++ 0.8 behavior | Current Rust state | Evidence | Slice decision |
| --- | --- | --- | --- | --- |
| Base filter/scorer framework | C++ has prefilter/filter/score plugins and weighted score plugins | Rust has matching prefilter/filter/scorer framework shape with default, disk, label-affinity, selector, heterogeneous, and failure-domain plugins | C++ `policy.h`, `framework_impl.cpp`; Rust `scheduler_framework/policy.rs`, `framework.rs` | Already closed enough for this slice |
| Rust JSON selector operators | C++ label-affinity expression matching supports `In`, `NotIn`, `Exists`, `NotExist` | **Pre-slice gap:** Rust selector JSON path supported `matchLabels`, `In`, `NotIn`, `Exists`, but did **not** yet support `NotExist`; this slice closes that Rust selector-completeness gap using the C++ label-expression operator set as precedent, not as a claim about C++ `ResourceSelectorFilter` shape | C++ `label_affinity_utils.cpp:47-68`; Rust `scheduler_framework/policy.rs:241-324`; tests now cover `NotExist` | **Closed in this slice** |
| Failure-domain filtering | C++ scheduler/topology logic can constrain placement by topology/label domain hints | Rust already had a `FailureDomainFilter` and node manager parsing from `failure_domain` / `zone`; this slice adds explicit proof only for the `labels.zone` fallback path | C++ scheduler topology/resource-view tree + label-affinity utilities; Rust `nodes.rs`, `scheduler_framework/policy.rs` | **Closed for `labels.zone` fallback in this slice** |
| Label affinity priority/weight semantics | C++ supports required/preferred affinity + anti-affinity with priority ordering/weights | Rust has simpler equality-map affinity and selector JSON; full weighted selector parity is not implemented | C++ `label_affinity_utils.cpp`; Rust `function_meta.rs`, `policy.rs` | Explicit boundary for now |
| Taints / tolerations | C++ broader scheduler policy includes taint/toleration paths | Rust config/tests do not prove equivalent taint semantics | Earlier audit `docs/analysis/126-master-scheduler-domain-parity-audit.md`; current Rust search | Explicit boundary for now |
| Preemption | C++ broader preemption/resource pressure handling | Rust has `enable_preemption` plumbing and `PreemptionController`, but behavior breadth is not closed | Rust `scheduler.rs`, `schedule_decision.rs`; existing smoke tests | Explicit boundary for now |
| Migration | C++ broader migration/watch behavior | Rust abnormal processor records migration tasks, but end-to-end policy semantics remain much shallower | Rust `abnormal_processor.rs`; earlier audit `126` | Explicit boundary for now |
| Domain-group control | C++ `DomainGroupCtrl` actor owns group scheduling control | Rust has bounded gang/group reservation + metastore phase recording, but not full actor parity | C++ `domain_group_ctrl.cpp`; Rust `group.rs` | Explicit boundary for now |
| Underlayer scheduler manager | C++ `UnderlayerSchedMgr` actor dispatches schedule/reserve/bind/unbind to underlayers | Rust local-node manager forwards schedule/evict gRPC, but not the full underlayer actor contract | C++ `underlayer_sched_mgr.cpp`; Rust `nodes.rs`, service layer | Explicit boundary for now |

## What current Rust tests already prove

- least-loaded node selection on aggregate free score:
  - `scheduling_engine_select_node_least_loaded_free_score`
- request-label filtering against projected labels:
  - `label_on_request_filters_nodes`
- authoritative ResourceUnit labels override lossy JSON:
  - `label_on_request_uses_authoritative_resource_unit_labels_when_json_is_lossy`
- resource selector `matchLabels` path:
  - `scheduler_framework_resource_selector_match_labels`
- resource-view summaries preserve vectors/labels/instance usage:
  - `resource_view_contract_test.rs`

## What current Rust does **not** yet prove

- selector `NotExist` operator behavior,
- explicit failure-domain filter behavior for the `labels.zone` fallback path,
- full weighted affinity/anti-affinity parity,
- taints/tolerations parity,
- full migration/preemption/underlayer/domain-group control semantics.

## Smallest release-scope closure candidates

### Worth closing in this slice

1. **Add `NotExist` support to the Rust selector expression path.**
   - This is a direct, bounded Rust selector-completeness gap, with the C++ label-expression matcher used as the operator reference.
2. **Add an explicit test for failure-domain filtering via `labels.zone` fallback.**
   - Rust already carries the broader parsing paths, but this bounded proof reduces uncertainty without reopening the full scheduler design.

### Explicitly larger follow-up work

1. weighted/priority affinity parity,
2. taint/toleration parity,
3. preemption/migration behavior parity,
4. underlayer scheduler manager and domain-group control actor parity.

## Recommended slice outcome

This slice should close the **Rust selector operator completeness + failure-domain policy** gap in bounded form, while keeping underlayer/group, taint, migration, preemption, and full weighted affinity semantics explicitly bounded for the final release decision.
