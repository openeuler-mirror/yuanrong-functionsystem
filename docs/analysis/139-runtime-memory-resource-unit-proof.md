# RUNTIME-004 — Runtime Memory Resource Unit Proof

Date: 2026-04-28
Branch: `rust-rewrite`
Scope: Rust-only `runtime_manager` memory resource unit parity with C++ 0.8

## Goal

Close a code-level black-box parity gap found after the accepted ST suite was already green: Rust treated `resources["memory"]` as GiB in cgroup/OOM/RLIMIT paths, while C++ 0.8 treats the same SDK option as MB.

This keeps the black-box constitution intact: only Rust `yuanrong-functionsystem` was changed. No upper-layer `yuanrong`, ST script, runtime binary, datasystem, or clean C++ control file was modified.

## C++ Reference Checked

Clean C++ 0.8 source and ST fixtures show MB semantics consistently:

- `function_proxy/local_scheduler/instance_control/instance_ctrl_actor.cpp`
  - Invalid resource errors format memory as `Required memory resource size <N> MB ... [min,max] MB`.
- `runtime_manager/metrics/metrics_actor.cpp`
  - Runtime memory monitor reads instance usage and limit in MB and logs `memory usage: <usage> MB, limit: <limit> MB`.
- `runtime_manager/metrics/metrics_client.cpp`
  - Runtime memory limit is read from the deployed instance resource field and sent to the monitor path.
- ST fixtures:
  - `test/st/cpp/src/base/task_test.cpp` uses `option.memory = 1.0` and `500.0` as valid resource requests.
  - `test/st/cpp/src/base/actor_test.cpp` expects `127 MB` to be invalid and `128 MB` to be valid.

Therefore Rust must treat the user-facing memory resource value as MiB/MB-equivalent for runtime-manager resource enforcement and OOM monitoring. Treating `500.0` as GiB is a black-box semantic mismatch even if the current ST suite does not catch it directly.

## Rust Change

Changed files:

```text
functionsystem/src/runtime_manager/src/container.rs
functionsystem/src/runtime_manager/src/executor.rs
functionsystem/src/runtime_manager/src/oom/cgroup.rs
functionsystem/src/runtime_manager/src/oom/monitor.rs
functionsystem/src/runtime_manager/src/oom/oom_handler.rs
functionsystem/src/runtime_manager/src/state.rs
functionsystem/src/runtime_manager/tests/oom_lifecycle.rs
functionsystem/src/runtime_manager/tests/memory_resource_units.rs
```

Behavioral changes:

- `CgroupIsolate` now converts `resources["memory"]` from MiB to bytes for cgroup v2 `memory.max`.
- OOM monitor compares process usage in MB against `resources["memory"]` in MB plus the configured slack.
- OOM cgroup refresh uses the same MB conversion as spawn-time cgroup setup.
- Runtime state documentation now records memory resource units as MiB.
- Rust no longer sets `RLIMIT_AS` from `resources["memory"]` in the child process.
  - C++ resource enforcement flows through metrics/OOM callback behavior, not a hard address-space limit.
  - Keeping RLIMIT_AS would make small but valid ST values like `memory = 1.0` behave unlike C++.

New regression tests:

- `memory_resource_units.rs`
  - `memory_max_bytes_for_resource(Some(500.0)) == 500 * 1024 * 1024`
  - cgroup refresh writes `256 * 1024 * 1024` to `memory.max`.
- `oom_lifecycle.rs`
  - `memory_limit_mb_for_resource(500.0) == Some(500)` and non-positive values disable the limit.

## Verification

All Rust host commands used `CARGO_BUILD_JOBS=8`.

```bash
cargo test -p yr-runtime-manager \
  --test oom_lifecycle \
  --test memory_resource_units \
  --test command_env_snapshot_test \
  --test health_exit_classification \
  -- --nocapture
# 19 passed

cargo check --workspace --lib --bins
# passed; only pre-existing yr-proxy warnings remain

git diff --check
# passed
```

Container black-box build/package source in `yr-e2e-master:/workspace/rust_current_fs`:

```bash
export CARGO_BUILD_JOBS=8
cargo test -p yr-runtime-manager --test oom_lifecycle --test memory_resource_units -- --nocapture
./run.sh build -j 8
./run.sh pack
# build and pack succeeded
```

New Rust functionsystem artifact hashes copied into the proof lane:

```text
9ac8719ab09bcd78ac9fd8004f75e76bb83ce9d10d8c4ea75d4ef4bacd37aaff  output/yr-functionsystem-v0.0.0.tar.gz
b0a1f2941f4de7b391c21b6e382e634603ba0f3d272a438d27ebe36945a99163  output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
a1c1c67853447bf187d005705c26434e87ec9587e517f91e225ae839942ba34e  output/metrics.tar.gz
```

Upper-layer `yuanrong` package was rebuilt unchanged after replacing only functionsystem artifacts:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong
bash scripts/package_yuanrong.sh -v v0.0.1
```

Aggregate hashes:

```text
035d09ce85d2ee9a861499a1b47b0976643575eccd08e742d6bd3036042394a9  output/openyuanrong-v0.0.1.tar.gz
3a63f3471cc006f81612d0298d9023ff9406308995866fa2f8b0872eb05e9683  output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

Targeted resource ST after replacement:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
FILTER="TaskTest.TestResource:TaskTest.InvokeSuccessfullyWithDifferentResource:ActorTest.InvalidResource:ActorTest.ZeroGPU"
bash test.sh -b -l cpp -f "$FILTER"
# Success to run cpp st
```

Deployment:

```text
/tmp/deploy/28100232
```

GTest result:

```text
[==========] Running 4 tests from 2 test cases.
[  PASSED  ] 4 tests.
```

Full accepted single-shot cpp ST after replacement:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
# Success to run cpp st
```

Deployment:

```text
/tmp/deploy/28100337
```

GTest result:

```text
[==========] Running 111 tests from 6 test cases.
[==========] 111 tests from 6 test cases ran. (225848 ms total)
[  PASSED  ] 111 tests.
```

## Evidence Files In Container

```text
/workspace/proof_source_replace_0_8/logs/runtime004_memory_units_functionsystem_hashes.txt
/workspace/proof_source_replace_0_8/logs/runtime004_memory_units_package_yuanrong.log
/workspace/proof_source_replace_0_8/logs/runtime004_memory_units_openyuanrong_hashes.txt
/workspace/proof_source_replace_0_8/logs/runtime004_memory_units_resource_st.log
/workspace/proof_source_replace_0_8/logs/runtime004_memory_units_full_cpp_st.log
/workspace/proof_source_replace_0_8/logs/runtime004_memory_units_full_cpp_st_evidence.txt
```

## Status

The memory-resource unit mismatch is closed for Rust runtime-manager cgroup/OOM enforcement paths. Accepted black-box ST remains green after the change. Remaining `RUNTIME-004` work, if release scope requires it, is an actual over-limit deployed OOM callback proof, not the user-facing memory-unit conversion.
