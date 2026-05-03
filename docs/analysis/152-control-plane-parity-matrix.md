# Control-Plane Parity Matrix

Date: 2026-05-03
Branch: `rust-rewrite`
Scope: Subgoal C from `docs/analysis/148-remaining-blackbox-parity-ai-task.md`

## Goal

Build a release-scope matrix for the master / scheduler / meta / IAM control plane so the remaining parity work is driven by evidence instead of broad source-size anxiety.

This matrix separates:

1. rows that need endpoint or behavior audit first,
2. rows that look closable with Rust-only P1 work,
3. rows that are probably documentation / explicit-boundary items for this release,
4. rows that are too broad to honestly claim in one small slice.

## Prior audits reused

- `docs/analysis/126-master-scheduler-domain-parity-audit.md`
- `docs/analysis/127-metastore-iam-parity-audit.md`
- `docs/analysis/133-cpp-rust-flag-behavior-inventory.md`

## Current Rust surface re-read for this slice

- `functionsystem/src/function_master/src/http.rs`
- `functionsystem/src/function_master/src/snapshot.rs`
- `functionsystem/src/meta_store/src/server.rs`
- `functionsystem/src/iam_server/src/routes.rs`

## Matrix

| Row | C++ source starting points | Current Rust surface | Current finding | Slice class |
| --- | --- | --- | --- | --- |
| MASTER-001 snapshot metadata HTTP/watch/sync/delete/list/restore | `function_master/snap_manager/**`, `snapshot_scheduler.cpp` | `function_master/src/snapshot.rs`, master HTTP compatibility routes | Rust now has route-level evidence for `query/list` plus the C++-style empty-body `400` on `/query-snapshot`, but the broader C++ snapshot manager HTTP/watch/delete/restore breadth is still not endpoint-audited | **Partially closed; remaining watch/delete/restore is still P1/P2** |
| MASTER-002 resource group persist/query/migrate/bundles | `function_master/resource_group_manager/**`, `instance_manager/group_manager.cpp`, `global_scheduler/scheduler_manager/domain_sched_mgr.cpp` | master scheduler/group/resource files plus proxy resource-group request path | Existing audits already say Rust breadth is narrower; Subgoal A also confirmed proxy `CreateResourceGroup` is still a stub success reply | **Likely P1, but needs exact behavior matrix first** |
| MASTER-003 taint/migration/upgrade flags | `function_master/common/flags/flags.cpp`, `main.cpp` | `function_master/src/config.rs`, `main.rs` | Looks more like flag inventory / parse-compatible surface than an immediately testable black-box delta | **Document/boundary first** |
| MASTER-004 HTTP route/status/body/protobuf matrix | master HTTP drivers / instance manager / snapshot / resource-group routes | `function_master/src/http.rs` | Matrix now exists; this slice restored `/masterinfo` + `/global-scheduler/masterinfo` and fixed empty-body `/query-snapshot` status parity, but the full route/protobuf A/B matrix is still not finished | **First P1 slice closed; broader HTTP audit remains** |
| SCHED-001 domain group control and underlayer scheduler manager | `domain_scheduler/domain_group_control/**`, `underlayer_scheduler_manager/**` | Rust scheduler engine/resource view/group modules | Old audit still stands: basic scheduling exists, full underlayer/group-control breadth is not proven | **Likely P1 after master routes/snapshot/resource-group** |
| SCHED-002 taints/group policy/migration/preemption/quota policies | scheduler framework / policy actors across C++ domain scheduler | Rust scheduling framework and preemption controls | Too broad for an honest one-slice claim without deeper scheduler architecture work | **Large / likely later** |
| META-001 KV/watch/lease/revision behavior | `meta_store/server/src/kv_service_actor.cpp`, `watch_service_actor.cpp`, `lease_service_actor.cpp` | `meta_store/src/server.rs`, `kv_store.rs`, `watch_service.rs`, `lease_service.rs` | Rust exposes etcd-compatible gRPC surface and revision-bearing headers, but current proof is still “needed subset for ST”, not explicit compatibility probes | **Likely closable P1 with focused probe matrix** |
| META-002 backup/persistence flush/restart behavior | `meta_store/server/src/backup_actor.cpp`, persistence-related driver/config | `meta_store/src/server.rs`, `backup.rs`, `snapshot_file.rs` | Rust does local snapshot save/recover and optional etcd backup, but crash/restart durability policy is not proven against C++ | **Likely P1 if persistent mode is release scope** |
| IAM-001 token/AKSK/internal IAM control-plane parity | `iam_server/iam_actor.cpp`, `internal_iam.cpp`, token/aksk actors | `iam_server/src/routes.rs`, `token.rs`, `aksk.rs` | Rust routes are broad, but several endpoints are placeholder-compatible rather than fully integrated; exact C++ route/status/body A/B is still missing | **Audit first; probably boundary/P2 unless IAM-enabled release is required** |

## Concrete evidence behind the matrix

### Master HTTP and snapshot

- `function_master/src/http.rs`
  - already serves compatibility routes such as:
    - `/healthy`
    - `/queryagents`
    - `/resources`
    - `/scheduling_queue`
    - `/named-ins`
    - `/queryinstances`
  - and duplicates several of them under `/global-scheduler/*` and `/instance-manager/*`
- `function_master/src/snapshot.rs`
  - implements:
    - in-memory `SnapshotManager`
    - snapshot capture for terminal transitions
    - list/filter by function + tenant
  - but there is no evidence yet of C++-equivalent watch/sync/restore scheduler behavior

### MetaStore

- `meta_store/src/server.rs`
  - exposes etcd-compatible:
    - KV
    - Watch
    - Lease
    - Maintenance
  - has:
    - revision-bearing headers
    - optional local snapshot load/save
    - optional backup stream to etcd
    - slave watcher replay path
- this is a strong base, but not yet a parity proof for revision/watch/lease edge cases

### IAM

- `iam_server/src/routes.rs`
  - has broad route coverage for:
    - token / AKSK issue/auth/abandon
    - token exchange / code exchange / login
    - auth URL
    - tenant quota
    - REST-style `/v1/tokens`, `/v1/aksk`, `/v1/users`, `/v1/tenants`, `/v1/roles`
- several routes are explicitly placeholder-compatible:
  - token exchange uses `id_token` as tenant placeholder
  - code exchange uses `code` as placeholder tenant
  - `auth_url` returns a placeholder URL
  - tenant quota returns default unlimited quotas
- that means IAM route breadth exists, but not all semantics are C++-parity claims yet

## Recommended Subgoal C execution order

1. **MASTER-004** — generate the master HTTP route/status/body/protobuf matrix first.
2. **MASTER-001** — audit/close snapshot list/query/delete/restore semantics that are actually reachable from upper-layer control flows.
3. **MASTER-002** — resource group persist/query/migrate/bundles, especially because proxy-side `CreateResourceGroup` is still bounded as not closed.
4. **META-001** — focused KV/watch/lease/revision probe matrix.
5. **META-002** — persistence/backup/restart proof if persistent mode is still considered release scope.
6. **SCHED-001** — only after master/resource-group behavior is clearer.
7. **MASTER-003 / IAM-001 / SCHED-002** — keep as boundary/audit rows unless a concrete P1 black-box gap drops out.

## Update after the current MASTER-004 slice

The first control-plane P1 closure from this matrix is now complete:

1. Rust restores the C++-reachable `masterinfo` surface at both:
   - `/masterinfo`
   - `/global-scheduler/masterinfo`
2. Rust now matches C++ `query-snapshot` empty-body status handling:
   - empty body -> `400 BAD_REQUEST`
   - missing snapshot id -> `404 NOT_FOUND`
3. This matters on real upper-layer control flows because official Python CLI code fetches service-discovery info from `/global-scheduler/masterinfo`.

That narrows the remaining Subgoal C work to the parts that are still not honestly closed:

1. **MASTER-001** broader snapshot manager behavior:
   - watch/sync/delete/restore breadth
   - protobuf path exactness
2. **MASTER-002** resource-group persistence/query/migrate behavior
3. **META-001 / META-002** edge compatibility beyond the currently sufficient ST subset
