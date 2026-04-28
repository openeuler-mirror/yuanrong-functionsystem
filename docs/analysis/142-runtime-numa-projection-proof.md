# RUNTIME-003 — Runtime NUMA Projection Proof

Date: 2026-04-28
Branch: `rust-rewrite`
Scope: Rust-only `runtime_manager` / embedded `function_agent` NUMA metrics projection slice

## Goal

Close the next small `RUNTIME-003` code-level parity slice after resource labels: C++ runtime-manager can collect NUMA topology when `numa_collection_enable` is set and emits NUMA as a vector resource with node IDs and per-NUMA-node CPU capacity. Rust previously accepted the flag in the function-agent ignored-flag bucket but did not forward it or project any NUMA resource shape.

This is intentionally a small, safe Rust-only slice:

- Preserve existing scalar `capacity` / `used` / `allocatable` / `resources` JSON behavior.
- Add a separate vector-resource JSON projection for NUMA so existing scalar consumers are not broken.
- Do not alter upper-layer `yuanrong`, C++ control, ST scripts, runtime binaries, or datasystem.
- Keep build/test parallelism capped at `-j8` / `CARGO_BUILD_JOBS=8`.

## C++ Reference Checked

Clean C++ 0.8 source:

```text
runtime_manager/config/flags.cpp
runtime_manager/metrics/metrics_actor.cpp
runtime_manager/metrics/collector/numa_collector.cpp
runtime_manager/metrics/collector/numa_collector.h
common/resource_view/resource_type.h
proto/posix/resource.proto
```

Relevant C++ behavior:

- `numa_collection_enable` defaults to `false`.
- `MetricsActor::ResolveSystemResourceMetricsCollector()` adds `NUMACollector` only when `flags.GetNUMACollectionEnable()` is true.
- `NUMACollector::GetNUMACPUInfo()`:
  - returns empty metrics when NUMA is unavailable;
  - collects NUMA node IDs into `intsInfo["ids"]`;
  - converts each NUMA node CPU count into millicores and stores it in `intsInfo["CPU"]`;
  - sets metric value to the NUMA node count for limit/capacity.
- `MetricsActor::BuildNUMAResource()` treats NUMA as a vector resource and uses `TransitionToVectors(...)`, producing C++ shape equivalent to:

```text
Resource.name = "NUMA"
Resource.type = VECTORS
Resource.vectors.values["ids"].vectors[nodeID].values = [0, 1, ...]
Resource.vectors.values["CPU"].vectors[nodeID].values = [cpu_count * 1000, ...]
```

## Rust Change

Changed files:

```text
functionsystem/src/runtime_manager/src/config.rs
functionsystem/src/runtime_manager/src/metrics.rs
functionsystem/src/runtime_manager/tests/metrics_resource_projection.rs
functionsystem/src/runtime_manager/tests/flag_compat_smoke.rs
functionsystem/src/runtime_manager/tests/config_defaults_grouped.rs
functionsystem/src/function_agent/src/config.rs
functionsystem/src/function_agent/tests/merge_process_config.rs
```

Behavioral changes:

- Added runtime-manager `--numa_collection_enable` with C++ default `false`.
- Embedded `yr-agent` now forwards C++ `--numa_collection_enable` into embedded runtime-manager config.
- Added `ResourceProjection.vectors` as a separate JSON field, skipped when empty, to avoid changing existing scalar `resources` consumers.
- When `numa_collection_enable` is true and `/sys/devices/system/node/node*/cpulist` yields NUMA nodes, Rust projects:
  - scalar `capacity["NUMA"] = node_count`;
  - scalar `used["NUMA"] = 0.0`;
  - scalar `allocatable["NUMA"] = node_count` through the existing capacity clone;
  - vector `vectors["NUMA"].values["ids"].vectors[node_id].values`;
  - vector `vectors["NUMA"].values["CPU"].vectors[node_id].values` in millicores.

Boundary: this is scheduler-facing JSON projection parity. Byte-level C++ `ResourceUnit` propagation remains a separate, larger backlog item.

## Host Verification

All host commands used `CARGO_BUILD_JOBS=8`.

```bash
cargo test -p yr-runtime-manager --test metrics_resource_projection --test flag_compat_smoke --test config_defaults_grouped -- --nocapture
# 16 passed

cargo test -p yr-agent --test merge_process_config --test flag_compat_smoke -- --nocapture
# 9 passed

cargo check --workspace --lib --bins
# passed; only pre-existing yr-proxy warnings remain

git diff --check
# passed
```

## Container Build/Package Verification

Container: `yr-e2e-master`

```bash
cd /workspace/rust_current_fs
export CARGO_BUILD_JOBS=8
cargo test -p yr-runtime-manager --test metrics_resource_projection --test flag_compat_smoke --test config_defaults_grouped -- --nocapture
# 16 passed

cargo test -p yr-agent --test merge_process_config --test flag_compat_smoke -- --nocapture
# 9 passed

./run.sh build -j 8
./run.sh pack
# build and pack succeeded
```

New Rust functionsystem artifacts copied into the proof lane:

```text
30a495d8d65fae4bfd6990d9a386a3f78270bc4681acf27add79196158252c82  output/yr-functionsystem-v0.0.0.tar.gz
724a8314d377f34c041dd3ae79a1208d7025400fd235a0ae6fc169c9587c60ad  output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
0243a04372afbfdc02e6a4c774ac8da616ac43036281028dd8184710eb4a1e7a  output/metrics.tar.gz
```

Upper-layer package rebuilt unchanged after replacing only functionsystem artifacts:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong
bash scripts/package_yuanrong.sh -v v0.0.1
```

Aggregate artifacts:

```text
b7437f9926d5db56a5877f01bcc5fed154b4dbe002171cbc2465c2e3dfa8be1d  output/openyuanrong-v0.0.1.tar.gz
3a63f3471cc006f81612d0298d9023ff9406308995866fa2f8b0872eb05e9683  output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

## Accepted ST Proof

Command:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
```

Deployment:

```text
/tmp/deploy/28105616
```

Result:

```text
[==========] Running 111 tests from 6 test cases.
[==========] 111 tests from 6 test cases ran. (231846 ms total)
[  PASSED  ] 111 tests.
```

## Evidence Files In Container

```text
/workspace/proof_source_replace_0_8/logs/runtime003_numa_projection_functionsystem_hashes.txt
/workspace/proof_source_replace_0_8/logs/runtime003_numa_projection_package_yuanrong.log
/workspace/proof_source_replace_0_8/logs/runtime003_numa_projection_openyuanrong_hashes.txt
/workspace/proof_source_replace_0_8/logs/runtime003_numa_projection_full_cpp_st.log
/workspace/proof_source_replace_0_8/logs/runtime003_numa_projection_full_cpp_st_evidence.txt
```

## Remaining RUNTIME-003 Work

Closed by this proof:

- `numa_collection_enable` flag handling and embedded-agent forwarding.
- NUMA node ID / CPU millicore vector JSON projection.

Still open if release scope requires exact advanced resources:

- Byte-level C++ `ResourceUnit` protobuf propagation.
- Full NUMA placement/allocation coupling in group scheduling.
- GPU/NPU hardware probing and vector projection.
- Disk vector `extensions` parity for configured `disk_resources`.
