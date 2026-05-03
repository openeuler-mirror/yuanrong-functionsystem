# COMMON-004 / PROXY-004 / PROXY-005 Group-NUMA Placement Proof

Date: 2026-05-03
Branch: `rust-rewrite`
Commit target: next commit after Subgoal A closure

## Goal

Close the release-scope Rust-only part of the remaining C++ 0.8 group / NUMA / placement gap without modifying upper-layer `yuanrong`, clean C++ control, datasystem, ST scripts, package layout, or acceptance commands.

This slice proves only the part that is both:

- directly observable on the current black-box request path, and
- safely closable in Rust without inventing missing C++ state machines.

## C++ evidence used

- `function_proxy/local_scheduler/local_group_ctrl/local_group_ctrl_actor.cpp`
  - `TransGroupRequest`
  - range defaulting / validation
  - group create request-shape rejection rules
- `common/utils/struct_transfer.h`
  - `GroupBinPackAffinity`
- `common/schedule_decision/performer/group_schedule_performer.cpp`
  - strict-pack performer split from generic affinity mapping
- `common/utils/numa_binding.cpp`
  - real CPU/memory NUMA bind and verification behavior

## Audit doc

- `docs/analysis/148-group-numa-placement-parity-matrix.md`

That matrix records the important boundary for this slice:

- Rust scheduler-side affinity primitives exist.
- Rust group-create request admission was still too permissive compared with C++.
- Rust does **not** currently implement the full C++ local-group persistence / recovery / duplicate / partial-failure machine.
- Rust does **not** currently enforce real runtime CPU/memory NUMA bind like C++ `NUMABinding`.

## Rust changes

### Proxy request validation

- `functionsystem/src/function_proxy/src/busproxy/invocation_handler.rs`
  - added C++-style group create validation before any scheduling fanout
  - rejects:
    - empty group request lists
    - detached lifecycle inside group create
    - mixed request priorities
    - more than one ranged request
    - invalid range bounds / step
    - `StrictPack` groups whose requests carry different `schedule_affinity`
  - mirrors C++ range normalization defaults:
    - `min` `0/-1` -> `1`
    - `max` `0/-1` -> `256`
    - `step` `0/-1` -> `2`
  - returns early `ErrParamInvalid` instead of falling through to later generic scheduling errors

### Regression coverage

- `functionsystem/src/function_proxy/tests/invocation_handler_test.rs`
  - added request-shape regression tests for:
    - empty group invalid
    - detached lifecycle invalid
    - mixed priorities invalid
    - multiple ranged creates invalid
    - invalid range bounds invalid
    - strict-pack with mismatched affinity invalid

## Explicit non-claims

This slice does **not** claim:

- full C++ `LocalGroupCtrlActor` persistence / sync / recover / clear parity
- full group duplicate / restart / partial-failure behavior parity
- actual placement enforcement for spread / pack / strict-spread / strict-pack on the current Rust create path
- real runtime CPU/memory NUMA bind or bind verification
- `CreateResourceGroup` side-effect parity

Those remain explicitly bounded gaps rather than silent gaps.

## Verification evidence

### Host

```text
cargo test -p yr-proxy --test invocation_handler_test -- --nocapture
=> 43 passed

cargo test -p yr-proxy --test group_create_test -- --nocapture
=> 5 passed

cargo check --workspace --lib --bins
=> PASS

git diff --check
=> PASS
```

### Container build/package

Container: `yr-e2e-master`
Build copy: `/workspace/rust_current_fs`

```text
cargo test -p yr-proxy --test invocation_handler_test -- --nocapture
=> 43 passed

cargo test -p yr-proxy --test group_create_test -- --nocapture
=> 5 passed

./run.sh build -j 8
=> Build function-system successfully in 258.28 seconds

./run.sh pack
=> Built artifacts:
   /workspace/rust_current_fs/output/yr-functionsystem-v0.0.0.tar.gz
   /workspace/rust_current_fs/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
   /workspace/rust_current_fs/output/metrics.tar.gz
```

Container log note:

- the first container build attempt failed because the synced build copy contained host-generated `CMakeCache.txt` directories under `vendor/` and `common/*/build`.
- after moving those synced cache directories aside inside `/workspace/rust_current_fs`, the required unchanged `./run.sh build -j 8` and `./run.sh pack` succeeded.
- this was build-copy hygiene in the proof environment, not a repository source change.

Container logs:

- `/workspace/proof_source_replace_0_8/logs/group_numa_placement_container_invocation_handler_test.log`
- `/workspace/proof_source_replace_0_8/logs/group_numa_placement_container_group_create_test.log`
- `/workspace/proof_source_replace_0_8/logs/group_numa_placement_container_build.log`
- `/workspace/proof_source_replace_0_8/logs/group_numa_placement_container_pack.log`

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

- `/workspace/proof_source_replace_0_8/logs/group_numa_placement_functionsystem_hashes.txt`
- `/workspace/proof_source_replace_0_8/logs/group_numa_placement_openyuanrong_hashes.txt`

Current FunctionSystem artifact hashes:

```text
339be74f5891f5306c6d39ab2c9bc9951aaa5adb804cf5c97e03a3fe59bad354  yr-functionsystem-v0.0.0.tar.gz
4193be7eb2230dfb2caca52ce828a105f1d14d2825ac8086d4e73d8021e361d4  openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
b92c46f00f658d1f8991486c076e8fdc09942e587c36dcb7b1c489d07421767f  metrics.tar.gz
```

### Single-shot ST

Command:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
```

Evidence:

```text
deploy=/tmp/deploy/03002226
[==========] Running 111 tests from 6 test cases.
[==========] 111 tests from 6 test cases ran. (232673 ms total)
[  PASSED  ] 111 tests.
```

Logs:

- `/workspace/proof_source_replace_0_8/logs/group_numa_placement_package_yuanrong.log`
- `/workspace/proof_source_replace_0_8/logs/group_numa_placement_full_cpp_st.log`
- `/workspace/proof_source_replace_0_8/logs/group_numa_placement_full_cpp_st_evidence.txt`
- `/tmp/deploy/03002226/driver/cpp_output.txt`

## Result

Rust now matches the C++ 0.8 group-create request-shape admission rules that matter on the active black-box path, so invalid grouped create requests fail early with C++-compatible parameter errors instead of leaking into later generic scheduling failures.

The release-scope remaining risk for this area is now explicit and bounded:

- true placement enforcement is still missing from the current Rust group create path
- true NUMA CPU/memory runtime binding still needs runtime-side implementation plus hardware-backed proof
- the broader C++ local-group persistence / recovery machine remains a separate follow-up if release scope requires it
