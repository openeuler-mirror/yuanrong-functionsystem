# MetaStore Lease Recovery Parity Matrix

Date: 2026-05-03
Branch: `rust-rewrite`
Scope: Subgoal C from `docs/analysis/161-production-control-plane-parity-ai-task.md`

## Goal

Compare the C++ 0.8 MetaStore lease path against the current Rust embedded MetaStore, then pick the smallest production-meaningful closure for this slice.

This matrix is intentionally narrower than full etcd API parity. It focuses on the lease path because:

1. lease state participates in leader election / service registry behavior,
2. C++ explicitly persists and restores lease state from backup,
3. current Rust startup restores KV but not lease state.

## C++ references inspected first

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/meta_store/server/src/meta_store_driver.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/meta_store/server/src/lease_service_actor.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/tests/unit/function_master/meta_store/meta_store_lease_test.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/tests/unit/function_master/meta_store/meta_store_kv_test.cpp
```

## Rust references inspected

```text
functionsystem/src/meta_store/src/server.rs
functionsystem/src/meta_store/src/backup.rs
functionsystem/src/meta_store/src/lease_service.rs
functionsystem/src/meta_store/src/meta_store_grpc.rs
functionsystem/src/meta_store/tests/e2e_metastore.rs
functionsystem/src/common/meta_store_client/src/client.rs
```

## High-level finding

Rust MetaStore already had usable KV / watch / txn / basic lease grant/revoke behavior for the current ST lane, but its lease recovery path was shallower than C++.

This slice is now closed in a bounded way:

- Rust restores persisted lease rows from `lease_backup_prefix` on startup,
- rewrites deadlines from current time like the C++ lease actor sync path,
- starts slave lease watch from the backup snapshot revision,
- keeps healthy idle watch on the stream path via progress notify and only falls back to snapshot resync after prolonged silence / stream failure / backup restart,
- and stops silently dropping backup queue / lease-delete failures.

The remaining release-scope question is no longer "does Rust recover leases at all?", but the broader etcd/control-plane semantics that stay outside this bounded lease closure.

## Matrix

| Behavior | C++ 0.8 behavior | Current Rust state | Evidence | Slice decision |
| --- | --- | --- | --- | --- |
| KV backup recovery | Restores KV cache from backup storage | Implemented for Rust startup | C++ `meta_store_driver.cpp`, `kv_service_actor`; Rust `backup.rs:82-118`, `server.rs:56-71` | Already closed enough for this slice |
| Lease backup recovery on startup | `LeaseServiceActor::Start()` `Get(prefix=/metastore/lease/)` then `Sync(...)` lease table before running | Closed in bounded form: Rust now snapshots `lease_backup_prefix`, rebuilds TTL-based deadlines, and advances local lease ID allocation before serving | C++ `lease_service_actor.cpp:109-175`; Rust `backup.rs`, `lease_service.rs`, `server.rs`; proof `docs/analysis/162-metastore-lease-recovery-proof.md` | Closed in this slice |
| Lease backup watch in slave/explore mode | After initial sync, slave watches `/metastore/lease/` and applies PUT/DELETE events | Closed in bounded form: Rust slave now starts watch from the recovered revision, applies PUT/DELETE updates, uses progress notifications during healthy idle periods, and falls back to snapshot resync only after prolonged silence / reconnect | C++ `lease_service_actor.cpp:155-173`, `213-260`; Rust `server.rs:181-256`; proof `docs/analysis/162-metastore-lease-recovery-proof.md` | Closed in this slice |
| Lease grant requested ID | C++ accepts explicit lease IDs in etcd lease request path | Rust etcd `Lease::lease_grant` keeps `r.id`, but custom `MetaStoreService::grant_lease` drops it; common Rust client currently doesn't expose requested ID | Rust `server.rs:433-444`; Rust `meta_store_grpc.rs:365-375`; Rust client `common/meta_store_client/src/client.rs:791-799` | Keep as bounded follow-up unless touched naturally |
| Lease keepalive stream | C++ has continuous keepalive handling | Rust custom keepalive stream returns `UNIMPLEMENTED` | C++ `lease_service_actor.cpp` keepalive handlers; Rust `meta_store_grpc.rs:410-417` | Explicit boundary for now |
| Lease revoke deletes bound keys | C++ lease revoke/expiry deletes lease-bound KV | Implemented in Rust | C++ lease actor + KV integration; Rust `lease_service.rs:106-129`, `199-235` | Already closed enough for this slice |
| Watch event shaping | C++ async push coalesces stale same-key events and updates response revision carefully | Rust publishes direct events without coalescing | C++ `watch_service_async_push_actor.cpp`; Rust `server.rs:207-212`, `335-426` | Boundary for now |
| Persistence payload format | C++ persists serialized `messages::Lease` under `/metastore/lease/` | Rust persists JSON `{id, ttl}` under `lease_backup_prefix` | C++ `lease_service_actor.cpp`; Rust `backup.rs:140-147` | Rust-private detail for now if recovery logic understands both its own persisted format and current release lane needs |

## What current Rust tests already prove

- KV put/get/delete round trip:
  - `functionsystem/src/meta_store/tests/e2e_metastore.rs:29-43`
- watch delivers put event:
  - `functionsystem/src/meta_store/tests/e2e_metastore.rs:71-119`
- lease grant -> put-with-lease -> revoke deletes key:
  - `functionsystem/src/meta_store/tests/e2e_metastore.rs:121-147`
- txn compare on mod revision:
  - `functionsystem/src/meta_store/tests/e2e_metastore.rs:149-200`
- MetaStore client routes through the same server:
  - `functionsystem/src/meta_store/tests/e2e_metastore.rs:202-215`

## What current Rust tests now prove

- backup queue saturation is no longer silent:
  - `backup_handle_marks_health_unhealthy_when_channel_is_full`
- persisted lease backup snapshot returns both rows and backup revision:
  - `recover_leases_snapshot_returns_revision_and_leases`
- lease backup delete errors propagate instead of being swallowed:
  - `lease_delete_apply_op_propagates_delete_errors`
- idle watch timeout now requires prolonged silence before snapshot resync:
  - `idle_timeout_requires_long_silence_before_resync`
- restart recovery of leases from backup etcd:
  - `lease_recovered_from_backup_after_restart`
- slave-side lease watch sync from backup etcd:
  - `slave_syncs_leases_from_backup_watch`
- slave resync after backup restart:
  - `slave_resyncs_leases_after_backup_restart`
- empty backup snapshot resync clears stale slave lease state:
  - `slave_clears_stale_leases_after_empty_backup_resync`

## What current Rust still does **not** prove

- keepalive stream parity
- requested-ID parity on the custom `MetaStoreService::GrantLease` RPC
- exact watch coalescing / revision shaping parity
- file-snapshot/local-persistence crash recovery parity outside the backup-etcd lease lane

## Smallest release-scope closure candidates

### Best candidate for this slice

1. **Recover lease table from backup etcd on startup.**
   - Read `lease_backup_prefix` alongside KV backup recovery.
   - Rebuild in-memory deadlines from current time and persisted TTL.
2. **Sync lease updates in slave mode from backup etcd watch.**
   - Mirror the current KV slave watcher shape for the lease prefix.
   - Apply PUT/DELETE updates into `LeaseService`.

This is the closest Rust-side analogue to the C++ lease actor's `Start()` + `Sync()` + watch flow, and it closes a real restart/failover gap without promising full etcd parity.

### Explicitly larger follow-up work

1. custom `GrantLease` requested-ID parity,
2. keepalive streaming API,
3. exact watch coalescing / revision shaping parity,
4. crash/restart proof for local snapshot mode if release scope requires file-based persistence too.

## Recommended slice outcome

This slice closes the **lease backup recovery + slave watch gap** and keeps keepalive stream, requested-ID custom RPC parity, broader watch shaping, and file-snapshot persistence semantics explicitly bounded.
