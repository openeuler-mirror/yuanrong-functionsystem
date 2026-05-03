# MASTER-002 Resource-Group Fail-Fast Proof

Date: 2026-05-03
Branch: `rust-rewrite`
Scope: Subgoal A from `docs/analysis/161-production-control-plane-parity-ai-task.md`

## Goal

This slice does **not** claim full C++ resource-group parity.

Its goal is narrower and production-safety focused:

1. audit the real C++ resource-group state machine,
2. prove Rust still lacks that state machine,
3. remove the most dangerous false claim in the current Rust lane: proxy `CreateResourceGroup` returning immediate success even though nothing was scheduled or persisted.

## Audit doc

- `docs/analysis/155-resource-group-full-parity-matrix.md`

## C++ evidence used

- `function_master/resource_group_manager/resource_group_manager_driver.h`
  - `/rgroup` is a real query entrypoint backed by `ResourceGroupManagerActor`.
- `function_master/resource_group_manager/resource_group_manager_actor.cpp`
  - `HandleForwardCreateResourceGroup(...)`
    - builds `ResourceGroupInfo`
    - initializes resource-group and bundle status as `PENDING`
    - persists to metastore
    - schedules via global scheduler
    - transitions to `CREATED` or `FAILED`
  - `MasterBusiness::QueryResourceGroup(...)`
    - returns real `ResourceGroupInfo` rows from cache
  - `HandleForwardDeleteResourceGroup(...)`
    - removes bundles and metastore rows
  - `HandleDeleteInstance(...)` / `HandleKillJob(...)`
    - free non-detached resource groups on driver exit / job kill
- `function_proxy/local_scheduler/local_group_ctrl/local_group_ctrl_actor.cpp`
  - group scheduling in C++ is stateful and failure-aware; Rust ordinary group-create cleanup is only a partial analogy, not resource-group parity

The important conclusion from the C++ side is simple:

```text
CreateResourceGroup success in C++ means real control-plane side effects happened.
```

That makes Rust's old unconditional success reply a real black-box lie.

## Rust pre-fix state

### Proxy create path

- `functionsystem/src/function_proxy/src/busproxy/invocation_handler.rs`
  - `handle_create_resource_group(...)` returned:
    - `code = ERR_NONE`
    - `message = ""`
    - original `request_id`
  - no scheduling
  - no metastore persistence
  - no master-side resource-group state

### Master query path

- `functionsystem/src/function_master/src/http.rs`
  - `/rgroup` returned a stable empty response shape:
    - JSON: `{ "requestID": "...", "groups": [], "count": 0 }`
    - protobuf: empty `QueryResourceGroupResponse`
  - but there is still no backing resource-group store in Rust `MasterState`

## Exact behavior closed in this slice

Closed behavior:

```text
Rust proxy CreateResourceGroup no longer claims success when no resource-group manager exists.
```

Current Rust behavior after this slice:

- proxy `CreateResourceGroup` returns:
  - `code = ERR_INNER_SYSTEM_ERROR`
  - non-empty explanatory message mentioning `resource group`
  - original `request_id`

This is intentionally a **bounded fail-fast**, not a fake parity claim.

## Rust changes

### Code

- `functionsystem/src/function_proxy/src/busproxy/invocation_handler.rs`
  - changed `handle_create_resource_group(...)`
  - replaced stub success with explicit `ERR_INNER_SYSTEM_ERROR`

### Tests / probes

- `functionsystem/src/function_proxy/tests/invocation_handler_test.rs`
  - replaced the old stub-success assertion with:
    - `r_group_req_fails_fast_until_resource_group_manager_exists`
  - coverage now checks:
    - response is `RGroupRsp`
    - `request_id` is preserved
    - error code is `ERR_INNER_SYSTEM_ERROR`
    - message mentions `resource group`

### Docs

- `docs/analysis/155-resource-group-full-parity-matrix.md`
  - records the larger state-machine gap honestly
- `docs/analysis/129-rust-gap-backlog.md`
  - updates `MASTER-002` to reflect the new fail-fast boundary

## Host preflight evidence

```text
cargo test -p yr-proxy --test invocation_handler_test
=> 43 passed

cargo test -p yr-proxy --test group_create_test
=> 5 passed

cargo check --workspace --lib --bins
=> PASS

git diff --check
=> PASS
```

## Container build/package evidence

Container: `yr-e2e-master`
Build copy: `/workspace/rust_current_fs`

```text
cargo test -p yr-proxy --test invocation_handler_test -- --nocapture
=> 43 passed

cargo test -p yr-proxy --test group_create_test -- --nocapture
=> 5 passed

./run.sh build -j 8
=> Build function-system successfully in 161.18 seconds

./run.sh pack
=> built:
   /workspace/rust_current_fs/output/yr-functionsystem-v0.0.0.tar.gz
   /workspace/rust_current_fs/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
   /workspace/rust_current_fs/output/metrics.tar.gz
```

Container logs:

- `/workspace/proof_source_replace_0_8/logs/resource_group_failfast_container_invocation_handler_test.log`
- `/workspace/proof_source_replace_0_8/logs/resource_group_failfast_container_group_create_test.log`
- `/workspace/proof_source_replace_0_8/logs/resource_group_failfast_container_build.log`
- `/workspace/proof_source_replace_0_8/logs/resource_group_failfast_container_pack.log`

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

Artifact hashes:

```text
ed2a7efbb0c824f0b6ef7ca6456211583e2ef99384758c97749c82081de13ade  output/yr-functionsystem-v0.0.0.tar.gz
8c0a35425e769ae8cccf641ebbc847c290128d1c8c6591a78b53e20e7da15d2b  output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
2864808db9a60d3f4ca329ff1fcffbac92593767690345f44eaca4b39d5f8ca7  output/metrics.tar.gz
41eae90c0dc84d57acf6b5a8e263d271a9821eb12303b2181707ebec375459a3  output/openyuanrong-v0.0.1.tar.gz
3a63f3471cc006f81612d0298d9023ff9406308995866fa2f8b0872eb05e9683  output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

Hash logs:

- `/workspace/proof_source_replace_0_8/logs/resource_group_failfast_functionsystem_hashes.txt`
- `/workspace/proof_source_replace_0_8/logs/resource_group_failfast_openyuanrong_hashes.txt`

## Single-shot ST

Command:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
```

Evidence:

```text
deploy=/tmp/deploy/03081941
[==========] Running 111 tests from 6 test cases.
[==========] 111 tests from 6 test cases ran. (217646 ms total)
[  PASSED  ] 111 tests.
```

Logs:

- `/workspace/proof_source_replace_0_8/logs/resource_group_failfast_package_yuanrong.log`
- `/workspace/proof_source_replace_0_8/logs/resource_group_failfast_full_cpp_st.log`
- `/workspace/proof_source_replace_0_8/logs/resource_group_failfast_full_cpp_st_evidence.txt`
- `/tmp/deploy/03081941/driver/cpp_output.txt`

## Explicit non-claims

This slice does **not** claim:

- real Rust `resource_group_manager` parity
- master `/rgroup` returning real resource groups
- bundle create/delete ownership tracking
- resource-group delete / detached / driver-exit / job-kill cleanup parity
- proxy abnormal reschedule parity
- metastore-backed resource-group sync / recover / migrate parity

Those remain explicit `MASTER-002` follow-up work.

## Result

Rust is still **not** a full C++-parity implementation for resource groups.

What changed is narrower and important:

```text
Rust no longer lies about CreateResourceGroup succeeding.
```

That keeps the current black-box claim honest while preserving the accepted build/pack/install/single-shot-ST lane.
