# MASTER-001 / MASTER-004 Control-Plane HTTP Proof

Date: 2026-05-03
Branch: `rust-rewrite`
Commit target: next commit after the Subgoal C HTTP slice

## Goal

Close the first release-scope control-plane HTTP gap proved by actual C++ source and upper-layer usage:

1. restore the missing `masterinfo` route surface, and
2. match the C++ empty-body contract for `/query-snapshot`.

This slice is intentionally narrow. It closes the first clean P1 management-API gap without pretending to finish the entire master/snapshot/resource-group/meta/IAM parity backlog in one patch.

## C++ evidence used

- `function_master/global_scheduler/global_sched_driver.cpp`
  - `/masterinfo` is `GET`-only
  - `Type` must be absent/`json`; unsupported values return `400`
  - body shape is:
    - `master_address`
    - `meta_store_address`
    - `schedule_topo`
- `common/scheduler_topology/sched_node.cpp`
  - `GetTopologyView()` fills:
    - `leader` from the current node's parent if present
    - `members` from the current node's children
  - for the root domain node, `leader` is omitted and only `members` are emitted
- `function_master/global_scheduler/global_sched_actor.cpp`
  - `FindRootDomainSched()` returns the topology-tree root node
- `function_master/snap_manager/snap_manager_driver.h`
  - `/query-snapshot` expects body=`snapshotID`
  - empty body returns `400 BAD_REQUEST`

## Upper-layer usage evidence

Official upper-layer Python CLI code uses the prefixed management route:

- `0.8.0/src/yuanrong/api/python/yr/cli/utils.py`
  - fetches `"{function_master_addr}/global-scheduler/masterinfo"`

That makes the missing prefixed Rust route a real black-box management-surface gap, not just a theoretical C++/Rust diff.

## Audit doc

- `docs/analysis/152-control-plane-parity-matrix.md`

## Rust changes

### Master HTTP compat

- `functionsystem/src/function_master/src/http.rs`
  - added:
    - `/masterinfo`
    - `/global-scheduler/masterinfo`
  - response now mirrors the C++ JSON shape:
    - `master_address`
    - `meta_store_address`
    - `schedule_topo`
  - `masterinfo` stays JSON-only like C++
  - `/query-snapshot` now returns `400 BAD_REQUEST` for an empty body instead of `404`
  - empty topology now keeps `schedule_topo.members = []` so upper-layer CLI consumers do not fail on missing array keys
- `functionsystem/src/function_master/src/main.rs`
  - the C++-compatible combined global-scheduler listener on `config.port` now mounts the same compat router surface, so `/global-scheduler/masterinfo` is reachable on the port upper-layer CLI code actually uses

### Regression coverage

- `functionsystem/src/function_master/tests/http_compat_test.rs`
  - updated:
    - `query_snapshot_requires_body`
  - added:
    - `masterinfo_returns_cpp_shape_on_root_and_prefixed_routes`
    - `masterinfo_rejects_non_json_type`
    - `masterinfo_empty_topology_keeps_members_array`

## Explicit non-claims

This slice does **not** claim:

- full snapshot manager parity for watch/sync/delete/restore
- full snapshot protobuf-body parity
- full resource-group actor parity
- full MetaStore / IAM endpoint parity
- full master HTTP route/protobuf A/B coverage beyond the routes touched here

Those remain explicit follow-up rows in Subgoal C rather than silent assumptions.

## Verification evidence

### Host

```text
cargo test -p yr-master --test http_compat_test
=> 39 passed

cargo test -p yr-master --test e2e_snapshot
=> 2 passed

cargo check --workspace --lib --bins
=> PASS

git diff --check
=> PASS
```

### Container build/package

Container: `yr-e2e-master`
Build copy: `/workspace/rust_current_fs`

```text
cargo test -p yr-master --test http_compat_test -- --nocapture
=> 39 passed

cargo test -p yr-master --test e2e_snapshot -- --nocapture
=> 2 passed

./run.sh build -j 8
=> Build function-system successfully in 120.36 seconds

./run.sh pack
=> Built artifacts:
   /workspace/rust_current_fs/output/yr-functionsystem-v0.0.0.tar.gz
   /workspace/rust_current_fs/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
   /workspace/rust_current_fs/output/metrics.tar.gz
```

Container logs:

- `/workspace/proof_source_replace_0_8/logs/control_plane_container_http_compat_test.log`
- `/workspace/proof_source_replace_0_8/logs/control_plane_container_e2e_snapshot.log`
- `/workspace/proof_source_replace_0_8/logs/control_plane_container_build.log`
- `/workspace/proof_source_replace_0_8/logs/control_plane_container_pack.log`

### Upper-layer proof lane

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

Artifact hashes recorded in container:

- `/workspace/proof_source_replace_0_8/logs/control_plane_functionsystem_hashes.txt`
- `/workspace/proof_source_replace_0_8/logs/control_plane_openyuanrong_hashes.txt`

Current FunctionSystem artifact hashes:

```text
1a88c792beaa298ddfb5299689e5646b1df22e5346cf1ceadcc266f626e01ad1  yr-functionsystem-v0.0.0.tar.gz
7d30666d8f887a1785dcc6f5586500d1cdcafa3fe9a8cd1ec84e4aa9bb5a28ed  openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
a89c34dac1377d9e57e130fb8469996130389a95493e554a07a3832068e0551d  metrics.tar.gz
```

### Single-shot ST

Command:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
```

Evidence:

```text
deploy=/tmp/deploy/03020415
[==========] Running 111 tests from 6 test cases.
[==========] 111 tests from 6 test cases ran. (216834 ms total)
[  PASSED  ] 111 tests.
```

Logs:

- `/workspace/proof_source_replace_0_8/logs/control_plane_package_yuanrong.log`
- `/workspace/proof_source_replace_0_8/logs/control_plane_full_cpp_st.log`
- `/workspace/proof_source_replace_0_8/logs/control_plane_full_cpp_st_evidence.txt`
- `/tmp/deploy/03020415/driver/cpp_output.txt`

## Result

Rust now covers the first concrete control-plane black-box gap identified in Subgoal C:

1. upper-layer callers can fetch `masterinfo` from the same root and prefixed routes they use against C++, including the combined global-scheduler listener on `config.port`
2. empty `/query-snapshot` requests now fail with the same `400 BAD_REQUEST` contract as C++

What remains open is now narrower and explicit:

- broader snapshot manager parity
- broader resource-group parity
- meta/lease/watch edge compatibility
- IAM byte-level route/body parity
