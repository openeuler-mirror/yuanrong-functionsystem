# RUNTIME-003 ResourceUnit Propagation Proof

Date: 2026-05-02
Branch: `rust-rewrite`
Commit target: next commit after handoff/audit slice

## Goal

Close the C++ 0.8 `ResourceUnit` / resource-view propagation gap for Rust FunctionSystem without changing upper-layer `yuanrong`, clean C++ control, datasystem, ST scripts, package layout, or acceptance commands.

This slice specifically proves that Rust now keeps C++-meaningful resource state on the authoritative scheduler path instead of collapsing everything to lossy scalar-only JSON.

## C++ evidence used

- `runtime_manager/metrics/metrics_actor.cpp`
  - `GetResourceUnit`
  - `BuildResourceUnit`
  - `BuildResourceUnitWithSystem`
  - `BuildResourceUnitWithInstance`
- `runtime_manager/manager/runtime_manager.cpp`
  - consumes metrics-side `ResourceUnit` in resource updates
- `function_agent/**`
  - register/update resource reporting path
- `common/resource_view/**`
  - `AddResourceUnit`
  - `UpdateResourceUnit`
  - `PullResource`
- `proto/posix/resource.proto`

## Audit doc

- `docs/analysis/146-resourceunit-propagation-audit.md`

That audit records the pre-fix gap:

- Rust runtime-manager already produced richer C++-shaped resource JSON.
- But Rust internal scheduler/master/domain paths still treated JSON as the active resource authority.
- Vector resources, labels, allocatable values, and per-instance actual-use were not preserved on the authoritative scheduler/resource-view path.

## Rust changes

### Contract / proto

- `proto/inner/scheduler.proto`
  - added optional `resource_unit` to:
    - `RegisterRequest`
    - `UpdateResourcesRequest`

- `functionsystem/src/common/proto/src/lib.rs`
  - fixed the generated `yr.internal` include layout so `ResourceUnit` cross-package references resolve correctly.

### Runtime-manager / embedded-agent reporting

- `functionsystem/src/runtime_manager/src/metrics.rs`
  - added `build_resource_update_json`
  - added `build_resource_unit`
  - both preserve C++-shaped scalar capacity/used/allocatable, labels, vectors, heterogeneous info, disk extensions, and per-instance actual-use

- `functionsystem/src/runtime_manager/src/agent.rs`
- `functionsystem/src/runtime_manager/src/main.rs`
- `functionsystem/src/function_agent/src/main.rs`
- `functionsystem/src/function_proxy/src/main.rs`
  - now send both `resource_json` and authoritative `resource_unit`

### Registration / topology / aggregation

- `functionsystem/src/function_agent/src/registration.rs`
- `functionsystem/src/function_proxy/src/registration.rs`
  - wired register/update requests to the new proto shape

- `functionsystem/src/function_master/src/topology.rs`
  - persists authoritative resource protobuf as `resource_unit_b64`

- `functionsystem/src/function_master/src/scheduler.rs`
  - forwards `resource_unit` into topology register/update

- `functionsystem/src/function_master/src/resource_agg.rs`
  - prefers `ResourceUnit.allocatable`
  - retains JSON fallback for compatibility

### Domain scheduler resource view

- `functionsystem/src/domain_scheduler/src/resource_view.rs`
  - preserves:
    - `capacity`
    - `used`
    - `allocatable`
    - `labels`
    - `vectors`
    - `instances`
  - `available()` now prefers `allocatable` when present
  - supports both rich JSON and authoritative protobuf ingestion

- `functionsystem/src/domain_scheduler/src/nodes.rs`
  - applies `resource_unit_b64` from topology when present

## Tests added / adjusted

New regression coverage:

- `functionsystem/src/common/proto/tests/round_trip.rs`
  - round-trip for register/update requests carrying `resource_unit`
- `functionsystem/src/runtime_manager/tests/resource_unit_projection_test.rs`
  - resource unit/json builders preserve vectors, labels, allocatable, and instance actual-use
- `functionsystem/src/domain_scheduler/tests/resource_view_contract_test.rs`
  - resource-view summary preserves allocatable, labels, vectors, and instance actual-use
- `functionsystem/src/function_master/tests/resource_agg_test.rs`
  - master aggregation prefers `ResourceUnit.allocatable`

Adjusted touched test fixtures after config/signature expansion:

- domain scheduler tests
- master topology / helper tests
- proxy integration helpers for the touched `master_proxy_flow`

## Explicit non-claims

This slice does **not** claim:

- full GPU/NPU physical hardware collector parity
- full NUMA/group placement policy parity
- full C++ resource-view delta/revision/state-machine parity beyond the active Rust scheduler path

Those remain separate follow-up work.

## Verification evidence

### Host

```text
cargo test -p yr-proto --test round_trip -- --nocapture
=> 5 passed

cargo test -p yr-runtime-manager --test resource_unit_projection_test -- --nocapture
=> 2 passed

cargo test -p yr-domain-scheduler --test service_contract_test --test resource_view_contract_test --test scheduling_test -- --nocapture
=> service_contract_test: 12 passed
=> resource_view_contract_test: 3 passed
=> scheduling_test: 9 passed

cargo test -p yr-master --test topology_test --test resource_agg_test -- --nocapture
=> topology_test: 22 passed
=> resource_agg_test: 1 passed

cargo test -p yr-agent --test resource_reporting -- --nocapture
=> 2 passed

cargo test -p yr-proxy --test functionsystem_integration master_proxy_flow -- --nocapture
=> 5 passed, 16 filtered out

cargo check --workspace --lib --bins
=> PASS

git diff --check
=> PASS
```

### Container build/package

Container: `yr-e2e-master`
Build copy: `/workspace/rust_current_fs`

```text
cargo test -p yr-proto --test round_trip -- --nocapture
=> 5 passed

cargo test -p yr-runtime-manager --test resource_unit_projection_test -- --nocapture
=> 2 passed

cargo test -p yr-domain-scheduler --test service_contract_test --test resource_view_contract_test --test scheduling_test -- --nocapture
=> 12 passed + 3 passed + 9 passed

cargo test -p yr-master --test topology_test --test resource_agg_test -- --nocapture
=> 22 passed + 1 passed

cargo test -p yr-agent --test resource_reporting -- --nocapture
=> 2 passed

cargo test -p yr-proxy --test functionsystem_integration master_proxy_flow -- --nocapture
=> 5 passed, 16 filtered out

./run.sh build -j 8
=> Build function-system successfully in 139.57 seconds

./run.sh pack
=> Built artifacts:
   /workspace/rust_current_fs/output/yr-functionsystem-v0.0.0.tar.gz
   /workspace/rust_current_fs/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
   /workspace/rust_current_fs/output/metrics.tar.gz
```

Container logs:

- `/workspace/proof_source_replace_0_8/logs/resourceunit_propagation_release_container_proto_test.log`
- `/workspace/proof_source_replace_0_8/logs/resourceunit_propagation_release_container_runtime_test.log`
- `/workspace/proof_source_replace_0_8/logs/resourceunit_propagation_release_container_domain_test.log`
- `/workspace/proof_source_replace_0_8/logs/resourceunit_propagation_release_container_master_test.log`
- `/workspace/proof_source_replace_0_8/logs/resourceunit_propagation_release_container_agent_test.log`
- `/workspace/proof_source_replace_0_8/logs/resourceunit_propagation_release_container_proxy_test.log`
- `/workspace/proof_source_replace_0_8/logs/resourceunit_propagation_release_build.log`
- `/workspace/proof_source_replace_0_8/logs/resourceunit_propagation_release_pack.log`

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

- `/workspace/proof_source_replace_0_8/logs/resourceunit_propagation_release_functionsystem_hashes.txt`
- `/workspace/proof_source_replace_0_8/logs/resourceunit_propagation_release_openyuanrong_hashes.txt`

### Single-shot ST

Command:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
```

Evidence:

```text
deploy=/tmp/deploy/02135134
[==========] Running 111 tests from 6 test cases.
[==========] 111 tests from 6 test cases ran. (218930 ms total)
[  PASSED  ] 111 tests.
```

Logs:

- `/workspace/proof_source_replace_0_8/logs/resourceunit_propagation_release_full_cpp_st.log`
- `/workspace/proof_source_replace_0_8/logs/resourceunit_propagation_release_full_cpp_st_evidence.txt`

## Result

RUNTIME-003 no longer drops C++-meaningful `ResourceUnit` data on the active Rust scheduler path. Rust now propagates authoritative resource protobuf state from runtime-manager into function-agent/proxy updates, master topology/aggregation, and domain resource-view ingestion while preserving JSON compatibility for existing surfaces.

Remaining release-scope follow-up under RUNTIME-003 is limited to:

- full hardware collector detail where physical GPU/NPU proof is needed
- full NUMA/group placement behavior
