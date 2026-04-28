# RUNTIME-003 — Runtime Resource Projection Proof

Date: 2026-04-28
Branch: `rust-rewrite`
Scope: Rust-only `runtime_manager` metrics/resource-reporting compatibility slice

## Goal

Close the first safe `RUNTIME-003` slice found by C++ code comparison: C++ runtime-manager `UpdateResources` builds a scheduler-facing resource view with CPU/memory capacity, actual use, allocatable resources, custom resources, and instance usage. Rust runtime-manager previously emitted only a diagnostic JSON snapshot, so downstream Rust schedulers that parse CPU/memory from resource reports could see no usable capacity.

This keeps the black-box constitution intact: only Rust `yuanrong-functionsystem` was changed. No upper-layer `yuanrong`, ST script, runtime binary, datasystem, or clean C++ control file was modified.

## C++ Reference Checked

Clean C++ 0.8 source:

- `runtime_manager/metrics/metrics_actor.cpp`
  - `StartUpdateMetrics()` sends `UpdateResources` to the function agent.
  - `BuildUpdateMetricsRequest(...)` builds `messages::UpdateResourcesRequest` from `BuildResourceUnit(...)`.
  - `BuildResourceUnitWithSystem(...)` fills `actualuse`, `capacity`, and `allocatable` for system resources.
  - `BuildResourceUnitWithInstance(...)` fills per-instance actual usage.
- `runtime_manager/metrics/collector/system_proc_cpu_collector.cpp`
  - In `proc` mode, system CPU usage is the sum of instance CPU metrics and capacity is the configured `proc_metrics_cpu`.
- `runtime_manager/metrics/collector/system_proc_memory_collector.cpp`
  - In `proc` mode, system memory usage is the sum of instance memory metrics and capacity is the configured `proc_metrics_memory`.
- `runtime_manager/config/flags.cpp`
  - `metrics_collector_type` defaults to `proc`.
  - `proc_metrics_cpu` defaults to `1000`.
  - `proc_metrics_memory` defaults to `4000`.
  - `overhead_cpu` / `overhead_memory` are used for `node` metrics mode.
- `runtime_manager/metrics/collector/node_memory_collector.cpp`
  - Node memory is reported in MB (`/proc/meminfo` kB divided by 1024).
- `runtime_manager/metrics/collector/node_disk_collector.cpp`
  - Node disk is reported in GB.
- `runtime_manager/metrics/collector/custom_resource_collector.cpp`
  - Custom resources contribute capacity/allocatable values.

## Rust Change

Changed files:

```text
functionsystem/src/runtime_manager/src/config.rs
functionsystem/src/runtime_manager/src/metrics.rs
functionsystem/src/runtime_manager/tests/metrics_resource_projection.rs
functionsystem/src/function_agent/src/config.rs
functionsystem/src/function_agent/tests/merge_process_config.rs
```

Behavioral changes:

- Added Rust runtime-manager flags aligned with C++ metrics configuration:
  - `--metrics_collector_type` (default `proc`)
  - `--proc_metrics_cpu` (default `1000.0`)
  - `--proc_metrics_memory` (default `4000.0`)
  - `--overhead_cpu` / `--overhead_memory`
- Embedded `yr-agent` now forwards those C++ flags into the embedded runtime-manager config.
- Runtime metrics JSON now preserves existing diagnostic fields and adds scheduler-compatible top-level projections:
  - `capacity`
  - `used`
  - `allocatable`
  - `resources.{name}.scalar.value`
- `proc` mode uses C++ defaults / configured capacity and derives memory used from runtime RSS in MB.
- `node` mode derives host CPU/memory capacity and applies C++ overhead flags; memory is MB.
- Disk capacity/usage is exposed in GB when root filesystem data is available.
- Valid JSON `custom_resources` contributes capacity entries.

This is intentionally a compatibility projection, not a byte-for-byte port of C++ `ResourceUnit` protobuf propagation. The Rust internal scheduler API currently uses `resource_json`, and existing Rust aggregators already parse scalar resources from a JSON `resources` object. The added projection gives that path C++-meaningful CPU/memory values without changing upper-layer protocols.

## Verification

All host commands used `CARGO_BUILD_JOBS=8`.

```bash
cargo test -p yr-runtime-manager --test metrics_resource_projection -- --nocapture
# 2 passed

cargo test -p yr-agent --test merge_process_config --test flag_compat_smoke -- --nocapture
# 9 passed

cargo check --workspace --lib --bins
# passed; only pre-existing yr-proxy warnings remain

git diff --check
# passed
```

Container black-box build/package verification in `yr-e2e-master:/workspace/rust_current_fs`:

```bash
export CARGO_BUILD_JOBS=8
cargo test -p yr-runtime-manager --test metrics_resource_projection -- --nocapture
# 2 passed

cargo test -p yr-agent --test merge_process_config --test flag_compat_smoke -- --nocapture
# 9 passed

./run.sh build -j 8
./run.sh pack
# build and pack succeeded
```

New Rust functionsystem artifact hashes copied into the proof lane:

```text
30db31343c595dd6fcf127c14bab114e7c1e9ca0a732f1d02d16b7fa63644942  output/yr-functionsystem-v0.0.0.tar.gz
a36abbca646917702b15ad3923476bc9d12b372ef5371039c2b1ec7eb187ff85  output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
e910244164a43db2410c5bd430e2a52ceb3b26b2cea267bf6109362fb490af02  output/metrics.tar.gz
```

Upper-layer `yuanrong` package was rebuilt unchanged after replacing only functionsystem artifacts:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong
bash scripts/package_yuanrong.sh -v v0.0.1
```

Aggregate hashes:

```text
d3c5f460573743a2df05c1e99977894cd131d4e5dc63176d8420bbf39d21881f  output/openyuanrong-v0.0.1.tar.gz
3a63f3471cc006f81612d0298d9023ff9406308995866fa2f8b0872eb05e9683  output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

Full accepted single-shot cpp ST after replacement:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
# Success to run cpp st
```

Deployment:

```text
/tmp/deploy/28102222
```

GTest result:

```text
[==========] Running 111 tests from 6 test cases.
[==========] 111 tests from 6 test cases ran. (232543 ms total)
[  PASSED  ] 111 tests.
```

## Evidence Files In Container

```text
/workspace/proof_source_replace_0_8/logs/runtime003_resource_projection_functionsystem_hashes.txt
/workspace/proof_source_replace_0_8/logs/runtime003_resource_projection_package_yuanrong.log
/workspace/proof_source_replace_0_8/logs/runtime003_resource_projection_openyuanrong_hashes.txt
/workspace/proof_source_replace_0_8/logs/runtime003_resource_projection_full_cpp_st.log
/workspace/proof_source_replace_0_8/logs/runtime003_resource_projection_full_cpp_st_evidence.txt
```

## Remaining RUNTIME-003 Work

This closes the scheduler-facing CPU/memory/custom-resource projection slice. Remaining code-level parity work, if release scope requires it:

- Byte-level `ResourceUnit` protobuf propagation between runtime-manager, agent, proxy/master instead of JSON projection.
- GPU/NPU, NUMA vectors, disk extensions, and resource labels with the exact C++ `Resource` shape.
- CPU usage percentage parity for per-instance collectors.
