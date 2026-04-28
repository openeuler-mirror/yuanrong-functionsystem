# Function Master / Domain Scheduler Code Parity Audit

Date: 2026-04-28
Branch: `rust-rewrite`
Scope: Rust `functionsystem/src/function_master/**` and `functionsystem/src/domain_scheduler/**` against C++ 0.8 `function_master/**` and `domain_scheduler/**`.

## Summary

Rust master/scheduler implements the endpoints and scheduling behavior required by the source-replacement ST lane: local/domain scheduler registration, resources, named instance query, basic scheduling, topology, election modes, and resource views. C++ master/domain scheduler includes broader subsystems: instance manager actors, group manager, resource group manager, scaler, snapshot manager, system function loader, taint/migration/upgrade watchers, domain group control, and underlayer scheduler management. These are not fully covered by current ST.

## Source evidence

| Area | C++ evidence | Rust evidence | Finding |
| --- | --- | --- | --- |
| Master HTTP API | C++ `function_master/instance_manager/**`, resource group/snapshot drivers register HTTP routes | Rust `function_master/src/http.rs` implements healthy, queryagents, resources, scheduling_queue, named-ins, queryinstances, metastore explore | Current ST APIs exist; snapshot/resource-group routes need broader comparison. |
| Instance manager | C++ has `instance_manager_actor`, group manager, instance family caches | Rust has `instances.rs`, `schedule_manager.rs`, `snapshot.rs`, `scheduler.rs` | Main query/schedule path exists, but family/group manager breadth not proven. |
| Resource group manager | C++ `resource_group_manager/**` persists/query/migrates resource groups and translates bundles to group requests | Rust has function_proxy resource group request handler and master e2e resource-group tests, but source breadth needs deeper check | Resource group production semantics need A/B. |
| Snapshot manager | C++ `snap_manager/**` supports snapshot metadata, list/query/delete, watch/sync, restore scheduling | Rust has `function_master/src/snapshot.rs` and tests, but API parity not fully audited | Snapshot behavior is not proven by ST. |
| System function loader | C++ `system_function_loader/**` bootstraps system functions | Rust `system_func_loader.rs` exists | Needs behavior comparison. |
| Taint/migration/upgrade | C++ flags include `migrate_enable`, taint tolerance/exclude labels, upgrade watch keys | Rust config accepts many flags; behavior not fully traced in this pass | Likely parse-compatible or partial. |
| Domain scheduling | C++ `domain_scheduler/domain_scheduler_service/**`, `domain_group_control/**`, `underlayer_scheduler_manager/**` | Rust `domain_scheduler/src/scheduler.rs`, `resource_view.rs`, `group.rs`, `abnormal_processor.rs`, framework plugins | Basic scheduling exists; full group/underlayer manager semantics not closed. |
| Preemption/quota | C++ flags and scheduler actors include preemption/resource controls | Rust has `PreemptionController`, `ScheduleQueue`, resource view, config `enable_preemption` | Needs A/B for non-trivial cases. |

## Findings

### MASTER-001: snapshot manager API and restore semantics are not ST-proven

C++ has a dedicated snapshot manager with HTTP routes, metadata cache, etcd watch, delete, list, and restore scheduling. Rust has snapshot-related code/tests, but this audit has not proven route shape and restore behavior are equivalent.

Classification: `Needs test` / `P2`, `P1` if snapshots are release scope.

### MASTER-002: resource group manager breadth is not fully closed

C++ resource group manager persists resource group info, syncs from metastore, migrates resource groups, and translates bundles into group requests. Rust has resource-group handling sufficient for current paths, but the full actor behavior is broader.

Classification: `Needs test` / `P1` for resource-group production parity.

### MASTER-003: taint/migration/system-upgrade watchers are likely parse-compatible or partial

C++ master flags include taint tolerance, migrate enable/prefix, evicted taint key, system upgrade watch key/address, and related behavior. Rust accepts many launch flags, but the full behavior was not found in the first source pass.

Classification: `Parse-compatible` / `P2`.

### MASTER-004: HTTP route parity needs an endpoint matrix

Rust `http.rs` intentionally implements many compatibility routes, including `/named-ins` and `/queryinstances`. C++ exposes additional resource group/snapshot routes and protobuf/JSON variants. The route set needs a generated A/B matrix.

Classification: `Needs test` / `P1` for routes used by upper-layer management.

### SCHED-001: domain scheduler underlayer/group control is not fully proven

C++ has `domain_group_control` and `underlayer_scheduler_manager` actors. Rust has a scheduler engine/framework, group module, and resource view, but full underlayer group control behavior is not closed by ST.

Classification: `Needs test` / `P1` if multi-domain/group scheduling is release scope.

### SCHED-002: scheduling plugin/resource policy parity is partial

Rust scheduling uses a framework and resource view with healthy-node selection, priorities, pending queue, and preemption warning. C++ has broader scheduler decisions, taints, group policy, migration, and resource-group transformations.

Classification: `Needs implementation` or `Needs test` / `P1/P2` depending on policy scope.

## Strong areas

- Named instance and current ST query paths have passed accepted source-replacement ST.
- Rust has meaningful unit/e2e tests for scheduler, topology, election, eviction, scaling, snapshot, and resource group.
- Basic domain scheduler node/resource selection is implemented and testable.

## Next checks

1. Generate C++ vs Rust HTTP route matrix for function_master and response formats.
2. A/B snapshot list/query/delete/restore with clean C++ control if runtime support exists.
3. A/B resource group create/query/migrate/sync scenarios.
4. Build taint/migration/upgrade watcher behavior inventory before implementation.
