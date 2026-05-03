# Snapshot Manager Parity Matrix

Date: 2026-05-03
Branch: `rust-rewrite`
Scope: Subgoal B from `docs/analysis/161-production-control-plane-parity-ai-task.md`

## Goal

Compare the C++ 0.8 snapshot manager against the current Rust master snapshot surface, then separate:

1. external HTTP/API mismatches that are small and safe to close now,
2. deeper actor / etcd / restore semantics that still need either larger implementation work or explicit release-scope boundaries.

## C++ references inspected first

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_master/snap_manager/snap_manager_driver.h
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_master/snap_manager/snap_manager_actor.cpp
0.8.0/src/yuanrong/src/libruntime/invokeadaptor/invoke_adaptor.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_proxy/local_scheduler/local_scheduler_service/local_sched_srv_actor.cpp
```

## Rust references inspected

```text
functionsystem/src/function_master/src/http.rs
functionsystem/src/function_master/src/snapshot.rs
functionsystem/src/function_master/tests/http_compat_test.rs
functionsystem/src/function_master/tests/e2e_snapshot.rs
functionsystem/src/function_master/tests/instances_test.rs
```

## High-level finding

Current Rust has a workable **in-memory snapshot cache** and the basic query/list JSON routes, but it still covers far less than the C++ snapshot manager:

- C++ has:
  - query/list HTTP for both JSON and protobuf,
  - delete from etcd,
  - restore (`SnapStartCheckpoint`) through the scheduler,
  - `GetAndWatch` sync from etcd,
  - cleanup/quota expiration behavior.
- Rust currently has:
  - in-memory snapshot CRUD,
  - terminal-state capture from instance-manager transitions,
  - JSON query/list,
  - but no real protobuf payloads for query/list,
  - no delete/restore/watch/sync control-plane path.

The best small release-scope closure in this slice is the **query/list protobuf mismatch**. The deeper delete/restore/watch/sync semantics should be bounded explicitly unless a larger implementation is justified.

Important boundary: even after this slice, Rust can only project the fields it actually retains in `InstanceSnapshot` (for example: instance ID, function, tenant, proxy, state/exit reason, create time, CPU, memory, snapshot ID). It still does **not** reconstruct the full C++ etcd-backed `SnapshotMetadata` object with all original `InstanceInfo` fields.

Two more boundaries stay open in this slice:

1. Rust `snapshotInfo.createTime` is inferred from current master-side instance JSON (`created_at_ms` / `createTime`) rather than copied from a C++-style runtime-produced `SnapshotInfo`.
2. Rust list order remains the current `exit_time`-descending in-memory sort, while C++ `ListSnapshotsByFunction` simply serializes the actor-returned vector without an explicit API ordering guarantee.

## Matrix

| Behavior | C++ 0.8 behavior | Current Rust state | Evidence | Slice decision |
| --- | --- | --- | --- | --- |
| Query snapshot JSON | GET `/query-snapshot`; body=`snapshotID`; empty body => 400; missing snapshot => 404; JSON body is `SnapshotMetadata` JSON | Implemented and previously proved | C++ `snap_manager_driver.h:38-73`; Rust `http.rs:647-678`, `http_compat_test.rs:526-604` | **Closed already** |
| Query snapshot protobuf | Same route, `Type != json` returns serialized `SnapshotMetadata` bytes | Rust returns `200` with empty protobuf body when snapshot exists | C++ `snap_manager_driver.h:57-70`; Rust `http.rs:668-675` | **Close empty-body mismatch in this slice**; keep full stored-metadata parity bounded |
| List snapshots JSON | GET `/list-snapshots`; body=`functionID`; empty body => 400; JSON array of snapshot metadata | Implemented and previously proved | C++ `snap_manager_driver.h:75-119`; Rust `http.rs:765-803`, `http_compat_test.rs:606-708` | **Closed for route/body/status only**; ordering semantics remain bounded |
| List snapshots protobuf | Same route, protobuf body is concatenated serialized `SnapshotMetadata` records | Rust returns `200` with empty protobuf body | C++ `snap_manager_driver.h:94-103`; Rust `http.rs:796-803` | **Close empty-body mismatch in this slice**; keep full stored-metadata parity and ordering semantics bounded |
| Delete snapshot | `DeleteSnapshot` removes metadata from etcd and watch path removes cache row | No Rust route/actor path found | C++ `snap_manager_actor.cpp:143-147`, `468-480` | **Explicit boundary for now** |
| Restore snapshot | `SnapStartCheckpoint` validates snapshot, builds schedule request, schedules restore, returns `RestoreSnapshotResponse` | No Rust restore path found | C++ `snap_manager_actor.cpp:307-318`, `406-449`; upper-layer runtime calls restore/checkpoint APIs in `invoke_adaptor.cpp` | **Important but larger**; keep open unless a dedicated restore slice is started |
| Watch / sync | `GetAndWatch` + syncer populate/update local cache from etcd | Rust snapshot manager is in-memory only | C++ `snap_manager_actor.cpp:153-220`; Rust `snapshot.rs:41-85` | **Boundary for now** |
| Cleanup / quota expiration | C++ periodically validates TTL and deletes expired/over-quota snapshots | No Rust equivalent | C++ `snap_manager_actor.cpp:240-255`, `326-497` | **Boundary for now** |
| Persistence interaction | C++ writes metadata to etcd and serves query/list off watched cache | Rust query/list observe only current in-memory cache | C++ `snap_manager_actor.cpp:452-480`; Rust `snapshot.rs:41-85` | **Boundary for now** |

## What current Rust tests already prove

- query-snapshot empty body -> `400`:
  - `function_master/tests/http_compat_test.rs:526-543`
- query-snapshot existing snapshot JSON:
  - `function_master/tests/http_compat_test.rs:545-586`
- query-snapshot missing snapshot -> `404`:
  - `function_master/tests/http_compat_test.rs:588-604`
- list-snapshots empty body -> `400`:
  - `function_master/tests/http_compat_test.rs:606-623`
- list-snapshots JSON array and tenant filtering:
  - `function_master/tests/http_compat_test.rs:625-708`
- terminal-state transitions record snapshots in memory:
  - `function_master/tests/instances_test.rs:28-48`
- end-to-end JSON query/list still works through the router:
  - `function_master/tests/e2e_snapshot.rs:14-109`

## What current Rust does **not** prove

- any protobuf payload exactness for query/list
- delete-snapshot parity
- restore-snapshot parity
- etcd-backed watch/sync
- TTL or quota cleanup

## Smallest release-scope closure candidates

### Worth closing in this slice

1. **Return real protobuf bytes for `query-snapshot`.**
   - Use existing in-memory snapshot data to build `messages::SnapshotMetadata`.
   - Do not overclaim it as a full etcd metadata replay.
2. **Return real protobuf bytes for `list-snapshots`.**
   - Match the C++ concatenation behavior over the current list output.
   - Keep absent `InstanceInfo` fields, create-time provenance, and ordering semantics explicit in the proof doc.

### Explicitly larger follow-up work

1. delete-snapshot control plane,
2. restore-snapshot scheduling path,
3. etcd watch/sync and persistence semantics,
4. TTL/quota cleanup parity.

## Recommended slice outcome

This slice should close the **query/list protobuf empty-body contract gap** and keep the deeper snapshot manager actor semantics, plus full stored-`SnapshotMetadata` parity, create-time provenance, and list ordering semantics, explicitly bounded in the proof doc and backlog.
