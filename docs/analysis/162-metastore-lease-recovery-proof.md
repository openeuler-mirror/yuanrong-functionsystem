# MetaStore Lease Recovery Proof

Date: 2026-05-03
Branch: `rust-rewrite`
Scope: bounded MetaStore lease recovery / slave watch closure from `docs/analysis/159-metastore-lease-recovery-parity-matrix.md`

## Goal

Close the release-scope MetaStore lease gap that was still outside the accepted ST lane:

1. recover persisted lease rows from backup etcd on startup,
2. keep slave lease state in sync with `/metastore/lease/` updates,
3. avoid silently dropping backup queue / lease-delete failures,
4. keep broader etcd parity explicitly bounded.

## C++ behavior used as oracle

Inspected first:

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/meta_store/server/src/meta_store_driver.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/meta_store/server/src/lease_service_actor.cpp
```

Relevant C++ behavior:

- `LeaseServiceActor::Start()` loads `/metastore/lease/` from backup storage before the lease actor begins serving.
- `Sync(...)` rebuilds expiry from current time and seeds the in-memory lease table.
- In explore/slave mode, the lease actor starts a watch from the snapshot revision and applies PUT/DELETE updates.
- The backup path does not silently treat lease delete failures as success.

## Exact closure in Rust

Closed behavior in this slice:

```text
Rust MetaStore now restores lease backup state on startup, anchors slave lease watch to the recovered backup revision, stays idle on the watch path without 1-second resnapshot churn, and surfaces lease-backup queue/delete failures instead of silently succeeding.
```

What changed:

- `functionsystem/src/meta_store/src/backup.rs`
  - added lease backup snapshot recovery with revision capture,
  - marks backup health unhealthy when the backup queue is full/closed,
  - propagates lease delete RPC failures instead of swallowing them.
- `functionsystem/src/meta_store/src/lease_service.rs`
  - added bounded backup snapshot sync that refreshes deadlines and advances `next_id`,
  - retains the existing PUT/DELETE apply helpers for live watch events.
- `functionsystem/src/meta_store/src/server.rs`
  - startup now restores lease snapshot state before serving,
  - slave lease watch now starts from the recovered revision,
  - healthy idle watch now stays on the watch stream with progress notifications,
  - and on prolonged silence / stream failure / reconnect, Rust resyncs from backup snapshot and re-establishes the watch.
- `functionsystem/src/meta_store/tests/e2e_metastore.rs`
  - added restart-recovery, backup-restart resync, and empty-backup convergence coverage.
- `functionsystem/src/meta_store/src/backup.rs` tests
  - added queue-full and forced-delete-error coverage.
- `functionsystem/src/meta_store/src/server.rs` tests
  - added idle-timeout threshold coverage so healthy idle watch no longer snapshot-resyncs every second.

## Rust test evidence

Targeted metastore package tests:

```text
cargo test -p yr-metastore-server -- --nocapture
=> unit tests:
   - backup_handle_marks_health_unhealthy_when_channel_is_full
   - recover_leases_snapshot_returns_revision_and_leases
   - lease_delete_apply_op_propagates_delete_errors
   - idle_timeout_requires_long_silence_before_resync
=> e2e tests:
   - lease_recovered_from_backup_after_restart
   - slave_syncs_leases_from_backup_watch
   - slave_resyncs_leases_after_backup_restart
   - slave_clears_stale_leases_after_empty_backup_resync
   - plus existing KV/watch/txn/lease round-trip coverage
=> PASS (4 unit + 10 e2e)
```

Host preflight:

```text
cargo check --workspace --lib --bins
=> PASS

git diff --check
=> PASS
```

## Container build/package evidence

Container: `yr-e2e-master`
Build copy: `/workspace/rust_current_fs`

```text
cargo test -p yr-metastore-server -- --nocapture
=> PASS (4 unit + 10 e2e)

./run.sh build -j 8
=> Finished `release` profile [optimized] target(s) in 24.11s

./run.sh pack
=> built:
   /workspace/rust_current_fs/output/yr-functionsystem-v0.0.0.tar.gz
   /workspace/rust_current_fs/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
   /workspace/rust_current_fs/output/metrics.tar.gz
```

Artifact hashes:

```text
3e49afe5c5f5e1a8e2ab0334da0a9774f022fc9e28c0bbbbca05ea2f826d9ff8  output/yr-functionsystem-v0.0.0.tar.gz
97d750ec113649f26065155635c2473deab77b236994494a2bf65d8dc08fe625  output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
d72bc1255d731e26eb2d34e559f2ea4c0b552118250054d4fd120d65c07622d9  output/metrics.tar.gz
```

## Upper-layer proof lane

Proof lane: `/workspace/proof_source_replace_0_8`

Replaced only these FunctionSystem artifacts in `src/yuanrong/output`:

- `yr-functionsystem-v0.0.0.tar.gz`
- `openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl`
- `metrics.tar.gz`

Repackaged with unchanged upper-layer script:

```text
cd /workspace/proof_source_replace_0_8/src/yuanrong
bash scripts/package_yuanrong.sh -v v0.0.1
=> output/openyuanrong-v0.0.1.tar.gz
=> output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

Repacked hashes:

```text
48a612ef189b9b539b355efb59eead6bd44e215bb3186d67aefd0178aea6b596  output/openyuanrong-v0.0.1.tar.gz
3a63f3471cc006f81612d0298d9023ff9406308995866fa2f8b0872eb05e9683  output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

## Single-shot ST

Command:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
```

Deploy path:

```text
/tmp/deploy/03102628
```

Evidence file:

```text
/tmp/deploy/03102628/driver/cpp_output.txt
```

Observed evidence:

```text
[==========] Running 111 tests from 6 test cases.
[  PASSED  ] 111 tests.
```

## Backlog impact

- `docs/analysis/129-rust-gap-backlog.md`
  - update `META-001` to reflect the bounded lease recovery/watch closure,
  - update `META-002` to reflect the lease-backup persistence hardening that now surfaces queue/delete failures.

## Explicit non-claims / remaining boundaries

This proof does **not** claim:

1. full etcd parity for all KV/watch/revision edge semantics,
2. custom `GrantLease` requested-ID parity on the MetaStore gRPC route,
3. keepalive stream parity,
4. exact C++ watch event coalescing / revision shaping,
5. full file-snapshot/local-persistence crash recovery parity outside the backup-etcd lease lane.

The accepted claim is narrower:

```text
For the production-control-plane MetaStore lease lane, Rust now closes the startup recovery + slave watch + backup-restart resync gap while preserving the accepted upper-layer build/pack/install/ST black-box path.
```
