# RUNTIME-003 — Runtime Disk Resources Projection Proof

Date: 2026-04-29
Branch: `rust-rewrite`
Scope: Rust-only `runtime_manager` / embedded `function_agent` `disk_resources` metrics projection slice

## Goal

Close the next `RUNTIME-003` code-level parity slice after NUMA projection: C++ runtime-manager accepts `--disk_resources`, parses configured named disk resources, and emits them as a vector resource named `disk` with disk extensions. Rust previously reported scalar root disk capacity/used only and had no C++ `disk_resources` flag or vector extension projection.

This slice stays inside the black-box replacement constitution:

- Modify only Rust `yuanrong-functionsystem` code and repo docs.
- Do not modify upper-layer `yuanrong`, clean C++ control, runtime binaries, datasystem, ST scripts, or deployment commands.
- Preserve existing scalar root disk projection so current Rust consumers are not broken.
- Add C++-shaped disk vectors as an additive `vectors["disk"]` projection.
- Keep build/test parallelism capped at `-j8` / `CARGO_BUILD_JOBS=8`.

## C++ Reference Checked

Clean C++ 0.8 source:

```text
runtime_manager/config/flags.cpp
runtime_manager/metrics/collector/disk_collector.cpp
runtime_manager/metrics/collector/disk_collector.h
runtime_manager/metrics/metrics_actor.cpp
proto/posix/resource.proto
```

Relevant C++ behavior:

- `disk_resources` defaults to an empty string.
- `MetricsActor::ResolveDiskResourceMetricsCollector(...)` creates `DiskCollector` only when the config string is non-empty.
- The config is parsed as a JSON array of disk objects.
- Each valid disk object requires:
  - `name`
  - `size`
  - `mountPoints`
- `size` must match `^\d+G$`; the numeric part is stored as GB integer.
- `mountPoints` must:
  - have length <= 8192;
  - start and end with `/`;
  - match allowed characters `[a-zA-Z0-9_\-/\.]`;
  - not contain `..`.
- Invalid disk entries are skipped rather than failing the whole collector.
- `DiskCollector::GetUsage()` emits `devClusterMetrics.intsInfo["disk"] = diskSizes_` and `extensionInfo` entries containing `ResourceExtension.disk { name, size, mountPoints }`.
- `MetricsActor::BuildDiskDevClusterResource()` sets `Resource.name = "disk"`, converts ints info into vectors, and copies extensions.

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

- Added runtime-manager `--disk_resources` / `--disk-resources` with C++ default empty string.
- Embedded `yr-agent` now forwards C++ `--disk_resources` into embedded runtime-manager config.
- Extended additive `VectorResource` JSON with optional `extensions`.
- Added disk extension JSON shape:

```json
{
  "disk": {
    "name": "fast",
    "size": 40,
    "mountPoints": "/mnt/fast/"
  }
}
```

- Added `build_disk_vectors(node_id, disk_resources)` with C++-aligned parsing:
  - JSON array only;
  - required `name`, `size`, `mountPoints` fields;
  - `size` accepts only digits plus `G`;
  - mount path checks mirror C++ security rules;
  - invalid entries are skipped;
  - empty/invalid config produces no vector projection.
- `build_resource_projection(...)` now inserts `vectors["disk"]` only when `disk_resources` yields at least one valid disk.
- Existing scalar root disk projection remains unchanged:
  - `capacity["disk"]` from root filesystem total GB;
  - `used["disk"]` from root filesystem used GB;
  - scalar `resources["disk"]` remains based on capacity.

Boundary: this is scheduler-facing JSON projection parity. Byte-level C++ `ResourceUnit` protobuf propagation and hardware GPU/NPU vector probing remain separate backlog items.

## TDD Evidence

RED was verified first:

```text
error[E0609]: no field `disk_resources` on type `Config`
error[E0432]: unresolved import `yr_runtime_manager::metrics::build_disk_vectors`
error[E0609]: no field `extensions` on type `VectorResource`
```

Then the Rust implementation was added and the same tests passed.

## Host Verification

All host commands used `CARGO_BUILD_JOBS=8`.

```bash
cargo test -p yr-runtime-manager --test metrics_resource_projection --test flag_compat_smoke --test config_defaults_grouped -- --nocapture
# 19 passed

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
# 19 passed

cargo test -p yr-agent --test merge_process_config --test flag_compat_smoke -- --nocapture
# 9 passed

./run.sh build -j 8
./run.sh pack
# build and pack succeeded
```

New Rust functionsystem artifacts copied into the proof lane:

```text
b43038a9169df8c0d06666d4cea74ba8cd5b8bb0f2dafde50a33273153b8f3e4  output/yr-functionsystem-v0.0.0.tar.gz
e6da42fa9ff155488eafdf6f58fea3cb08f225025ac0df570bfff8bf0d9451c5  output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
fd8471a8507100e2b1be39973439d75f60e67db5415052687937c70e584a18d8  output/metrics.tar.gz
```

Upper-layer package rebuilt unchanged after replacing only functionsystem artifacts:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong
bash scripts/package_yuanrong.sh -v v0.0.1
```

Aggregate artifacts:

```text
690e631faa0f7e866edce60423005d9fdc4ad46d79faf1bef90e85c6601f9adf  output/openyuanrong-v0.0.1.tar.gz
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
/tmp/deploy/29011937
```

Result:

```text
[==========] Running 111 tests from 6 test cases.
[==========] 111 tests from 6 test cases ran. (205268 ms total)
[  PASSED  ] 111 tests.
```

## Evidence Files In Container

```text
/workspace/proof_source_replace_0_8/logs/runtime003_disk_resources_functionsystem_hashes.txt
/workspace/proof_source_replace_0_8/logs/runtime003_disk_resources_package_yuanrong.log
/workspace/proof_source_replace_0_8/logs/runtime003_disk_resources_openyuanrong_hashes.txt
/workspace/proof_source_replace_0_8/logs/runtime003_disk_resources_full_cpp_st_final.log
/workspace/proof_source_replace_0_8/logs/runtime003_disk_resources_full_cpp_st_evidence.txt
```

## Remaining RUNTIME-003 Work

Closed by this proof:

- `disk_resources` runtime-manager flag and embedded-agent forwarding.
- C++-aligned disk JSON parser for `name` / `size` / `mountPoints`.
- Additive `vectors["disk"]` projection with disk sizes and disk extensions.
- Preservation of existing scalar root disk projection.

Still open if release scope requires exact advanced resources:

- GPU/NPU hardware probing and vector projection.
- Byte-level C++ `ResourceUnit` protobuf propagation.
- Full NUMA placement/allocation coupling in group scheduling.
