# MASTER-001 Snapshot Protobuf Closure Proof

Date: 2026-05-03
Branch: `rust-rewrite`
Scope: Subgoal B from `docs/analysis/161-production-control-plane-parity-ai-task.md`

## Goal

This slice does **not** claim full C++ snapshot-manager parity.

It closes one concrete production control-plane mismatch:

```text
Rust `/query-snapshot` and `/list-snapshots` protobuf paths no longer return empty success bodies.
```

The slice keeps broader snapshot-manager depth explicitly bounded:

- delete-snapshot control plane,
- restore-snapshot scheduling path,
- etcd-backed watch/sync/persistence,
- full stored `SnapshotMetadata` replay,
- exact `createTime` provenance,
- list ordering semantics.

## Audit doc

- `docs/analysis/157-snapshot-manager-parity-matrix.md`

## C++ evidence used

- `function_master/snap_manager/snap_manager_driver.h`
  - `/query-snapshot`
    - GET only
    - empty body => `400`
    - missing snapshot => `404`
    - protobuf path returns serialized `messages::SnapshotMetadata`
  - `/list-snapshots`
    - GET only
    - empty body => `400`
    - protobuf path concatenates serialized `messages::SnapshotMetadata` records
- `function_master/snap_manager/snap_manager_actor.cpp`
  - `HandleRecordSnapshot(...)`
    - builds `SnapshotMetadata` from request `snapshotInfo` + `instanceInfo`
  - `ListSnapshotsByFunction(...)`
    - returns the actor/cache vector that the HTTP driver serializes
- `function_proxy/local_scheduler/snap_ctrl/snap_ctrl_actor.cpp`
  - snapshot record request copies runtime `snapshotinfo` and current `instanceinfo` into `RecordSnapshotRequest`
- `runtime_manager/executor/container_executor.cpp`
  - runtime snapshot response fills checkpoint/storage/ttl before proxy -> master record

The important C++ conclusion is:

```text
snapshot protobuf success should serialize real SnapshotMetadata content when matches exist, not an unconditional empty success placeholder.
```

## Rust pre-fix state

- `functionsystem/src/function_master/src/http.rs`
  - `/query-snapshot` protobuf success returned:
    - `200 OK`
    - `content-type: application/x-protobuf`
    - empty body
  - `/list-snapshots` protobuf success returned:
    - `200 OK`
    - `content-type: application/x-protobuf`
    - empty body
- `functionsystem/src/function_master/src/snapshot.rs`
  - had only JSON-facing `InstanceSnapshot` data, with no protobuf projection helper

That meant Rust advertised protobuf compatibility on the route surface but produced no usable payload.

## Exact behavior closed in this slice

Closed behavior:

```text
Rust snapshot query/list protobuf now return real SnapshotMetadata bytes projected from the active in-memory snapshot rows.
```

Current Rust behavior after this slice:

- `/query-snapshot`
  - protobuf path returns decodable `messages::SnapshotMetadata`
  - includes the Rust-resident snapshot fields currently available:
    - instance ID
    - function name
    - tenant ID
    - function proxy ID
    - state / exit reason
    - snapshot ID
    - create time
    - CPU / memory resources
- `/list-snapshots`
  - protobuf path returns concatenated query-equivalent snapshot payloads for the current list output
  - zero-match success still legitimately returns an empty body, matching the C++ concatenation behavior

## Rust changes

### Code

- `functionsystem/src/function_master/src/snapshot.rs`
  - added `snapshot_to_proto(...)`
  - added `snapshots_to_proto_bytes(...)`
  - projects CPU/memory resources into protobuf `instance_info.resources`
- `functionsystem/src/function_master/src/http.rs`
  - `/query-snapshot` protobuf path now serializes one projected `SnapshotMetadata`
  - `/list-snapshots` protobuf path now serializes concatenated projected `SnapshotMetadata` bytes

### Tests / probes

- `functionsystem/src/function_master/tests/http_compat_test.rs`
  - added `query_snapshot_returns_protobuf_metadata`
    - drives a real instance -> snapshot capture path
    - decodes protobuf
    - asserts instance fields, status, snapshot ID, create time, and non-zero CPU/memory
  - added `list_snapshots_returns_protobuf_metadata_bytes`
    - proves list protobuf body equals the ordered concatenation of per-snapshot query protobuf bodies for the current list output
  - added `list_snapshots_protobuf_zero_match_returns_empty_body`
    - locks the C++ zero-match success behavior for protobuf list
- Existing JSON snapshot tests remain green:
  - `query_snapshot_returns_json`
  - `list_snapshots_returns_matching_snapshots`
  - `list_snapshots_json_body_filters_by_tenant`
  - `e2e_snapshot.rs`
  - `instances_test.rs`

### Docs

- `docs/analysis/157-snapshot-manager-parity-matrix.md`
  - records the route-level protobuf closure and the remaining bounded gaps
- `docs/analysis/129-rust-gap-backlog.md`
  - updates `MASTER-001` to reflect the narrowed closure and remaining boundaries

## Host preflight evidence

```text
cargo test -p yr-master --test http_compat_test -- --nocapture
=> 41 passed

cargo test -p yr-master --test e2e_snapshot -- --nocapture
=> 2 passed

cargo test -p yr-master --test instances_test -- --nocapture
=> 18 passed

cargo check --workspace --lib --bins
=> PASS

git diff --check
=> PASS
```

## Container build/package evidence

Container: `yr-e2e-master`
Build copy: `/workspace/rust_current_fs`

```text
cargo test -p yr-master --test http_compat_test -- --nocapture
=> 41 passed

cargo test -p yr-master --test e2e_snapshot -- --nocapture
=> 2 passed

cargo test -p yr-master --test instances_test -- --nocapture
=> 18 passed

./run.sh build -j 8
=> Finished `release` profile [optimized] target(s) in 44.25s

./run.sh pack
=> built:
   /workspace/rust_current_fs/output/yr-functionsystem-v0.0.0.tar.gz
   /workspace/rust_current_fs/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
   /workspace/rust_current_fs/output/metrics.tar.gz
```

Artifact hashes:

```text
777851682ce17ca2efedd16decf0662a8433e134cb30707e612378220b78eae3  output/yr-functionsystem-v0.0.0.tar.gz
b04c06462c74b0207f22f6ada81cbd54ce700984511df65ee69aadaa122db843  output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
882553c04a364ea81264823c31978efa7feda0f632f7c9129bac56b98c8dc360  output/metrics.tar.gz
```

## Upper-layer proof lane

Proof lane: `/workspace/proof_source_replace_0_8`

Replaced only these FunctionSystem artifacts in `src/yuanrong/output`:

- `yr-functionsystem-v0.0.0.tar.gz`
- `openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl`
- `metrics.tar.gz`

Repackaged with unchanged upper-layer script:

```text
bash scripts/package_yuanrong.sh -v v0.0.1
=> output/openyuanrong-v0.0.1.tar.gz
=> output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

Repacked hashes:

```text
3aa958eec481f2ca5f97e3c22120725bc9e5458facc60489b27c71a39f5f1ccc  output/openyuanrong-v0.0.1.tar.gz
3a63f3471cc006f81612d0298d9023ff9406308995866fa2f8b0872eb05e9683  output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

## Single-shot ST

Command:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
```

Evidence:

```text
deploy=/tmp/deploy/03090357
/tmp/deploy/03090357/driver/cpp_output.txt
Note: Google Test filter = *-CollectiveTest.InvalidGroupNameTest
[  PASSED  ] 111 tests.
```

Duplicated ST evidence path created by the harness:

- `/workspace/proof_source_replace_0_8/src/yuanrong/test/st/deploy/driver/cpp_output.txt`

## Explicit non-claims

This slice does **not** claim:

- delete-snapshot parity
- restore-snapshot scheduling parity
- etcd watch/sync/persistence parity
- full C++ stored `SnapshotMetadata` byte-for-byte parity
- exact `snapshotInfo.createTime` provenance parity
- exact list ordering parity beyond the current Rust route output

## Release-scope conclusion for this slice

This slice is accepted as a **bounded external protobuf closure**:

- Rust no longer returns fake-empty protobuf success bodies for snapshot query/list.
- The accepted black-box lane still keeps deeper snapshot manager semantics outside the proven scope until a later dedicated slice closes them.
