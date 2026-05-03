# RUNTIME-003 GPU / NPU Collector No-Hardware Proof

Date: 2026-05-03
Branch: `rust-rewrite`
Commit target: next commit after Subgoal B closure

## Goal

Close the GPU/NPU collector behavior that is testable without physical accelerator hardware, while documenting the exact C++ collector behavior that still requires GPU/NPU hosts to prove.

This slice intentionally focuses on:

- no-hardware parser / shape behavior,
- visibility-env semantics,
- existing vector / `ResourceUnit` projection plumbing,

and does **not** pretend to prove real device-driver behavior that Rust still does not implement.

## C++ evidence used

- `runtime_manager/metrics/collector/system_xpu_collector.cpp`
- `runtime_manager/metrics/collector/instance_xpu_collector.cpp`
- `runtime_manager/metrics/collector/heterogeneous_collector/gpu_probe.cpp`
- `runtime_manager/metrics/collector/heterogeneous_collector/npu_probe.cpp`
- `runtime_manager/metrics/collector/heterogeneous_collector/topo_probe.cpp`
- `runtime_manager/metrics/metrics_actor.cpp`

Key C++ behaviors referenced:

- GPU query path comes from `nvidia-smi` plus `nvidia-smi topo -m`
- NPU supports `count/hbm/sfmd/topo/all`
- `CUDA_VISIBLE_DEVICES` and `ASCEND_RT_VISIBLE_DEVICES` filter collected arrays by slot position
- NPU may collect from:
  - `npu-smi info`
  - `npu-smi info -t topo`
  - `/etc/hccn.conf`
  - `hccn_tool -i <id> -ip -g`
  - `npu_device_info_path` fallback JSON

## Audit doc

- `docs/analysis/150-gpu-npu-collector-parity-matrix.md`

That matrix separates what this slice closes from what remains hardware- or parser-bounded.

## Rust changes

### Runtime-manager collector helpers

- `functionsystem/src/runtime_manager/src/metrics.rs`
  - extracted testable GPU CSV parser:
    - `parse_gpu_query_csv(...)`
  - added visible-device-aware builder helpers:
    - `build_gpu_vectors_from_ids_visible(...)`
    - `build_npu_count_vectors_from_ids_visible(...)`
    - `build_npu_topology_vectors_from_json_visible(...)`
  - wired runtime collection to honor:
    - `CUDA_VISIBLE_DEVICES`
    - `ASCEND_RT_VISIBLE_DEVICES`
  - filtering matches the C++ `TopoProbe::FilterDevicesEnvVar` rule:
    - interpret env entries as slot positions, not device IDs
    - if env content is invalid or out of range, keep the detected result instead of hard-failing

### Regression coverage

- `functionsystem/src/runtime_manager/tests/metrics_resource_projection.rs`
  - added:
    - `gpu_query_csv_parser_skips_malformed_rows_and_keeps_first_model`
    - `gpu_visible_devices_filter_matches_cpp_slot_semantics`
    - `gpu_visible_devices_invalid_env_falls_back_to_detected_result`
    - `npu_count_visible_devices_filter_matches_cpp_slot_semantics`
    - `npu_topology_visible_devices_filter_matches_cpp_slot_semantics`

## Explicit non-claims

This slice does **not** claim:

- full `npu-smi info` regex-family parity (`910B` / `910C` / `310P3`)
- full `npu-smi info -t topo` topology parsing parity
- HCCN IP collection parity from `/etc/hccn.conf` or `hccn_tool`
- GPU topology / partition parity from `nvidia-smi topo -m`
- real hardware-backed proof on a GPU/NPU node

Those remain explicit hardware or larger-parser follow-up work under `RUNTIME-003`.

## Verification evidence

### Host

```text
cargo test -p yr-runtime-manager --test flag_compat_smoke -- --nocapture
=> 5 passed

cargo test -p yr-runtime-manager --test metrics_resource_projection -- --nocapture
=> 18 passed

cargo test -p yr-runtime-manager --test resource_unit_projection_test -- --nocapture
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
cargo test -p yr-runtime-manager --test flag_compat_smoke -- --nocapture
=> 5 passed

cargo test -p yr-runtime-manager --test metrics_resource_projection -- --nocapture
=> 18 passed

cargo test -p yr-runtime-manager --test resource_unit_projection_test -- --nocapture
=> 2 passed

./run.sh build -j 8
=> Build function-system successfully in 243.41 seconds

./run.sh pack
=> Built artifacts:
   /workspace/rust_current_fs/output/yr-functionsystem-v0.0.0.tar.gz
   /workspace/rust_current_fs/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
   /workspace/rust_current_fs/output/metrics.tar.gz
```

Container logs:

- `/workspace/proof_source_replace_0_8/logs/gpu_npu_collectors_container_flag_smoke_test.log`
- `/workspace/proof_source_replace_0_8/logs/gpu_npu_collectors_container_metrics_projection_test.log`
- `/workspace/proof_source_replace_0_8/logs/gpu_npu_collectors_container_resource_unit_test.log`
- `/workspace/proof_source_replace_0_8/logs/gpu_npu_collectors_container_build.log`
- `/workspace/proof_source_replace_0_8/logs/gpu_npu_collectors_container_pack.log`

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

- `/workspace/proof_source_replace_0_8/logs/gpu_npu_collectors_functionsystem_hashes.txt`
- `/workspace/proof_source_replace_0_8/logs/gpu_npu_collectors_openyuanrong_hashes.txt`

Current FunctionSystem artifact hashes:

```text
b956591f6d7cffe90cbcf8909cf90787e901b2f68846249bbe79693ffe54ee5b  yr-functionsystem-v0.0.0.tar.gz
c7f1899c9de7bda5cbc6d4bc32f95d7c16bbbec525586566c80ee06d757e02ab  openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
c79f9a2dd22122d338d19215d04364f00bc99fbfd59d215abd5d3980b6804ee2  metrics.tar.gz
```

### Single-shot ST

Command:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
```

Evidence:

```text
deploy=/tmp/deploy/03005116
[==========] Running 111 tests from 6 test cases.
[==========] 111 tests from 6 test cases ran. (234313 ms total)
[  PASSED  ] 111 tests.
```

Logs:

- `/workspace/proof_source_replace_0_8/logs/gpu_npu_collectors_package_yuanrong.log`
- `/workspace/proof_source_replace_0_8/logs/gpu_npu_collectors_full_cpp_st.log`
- `/workspace/proof_source_replace_0_8/logs/gpu_npu_collectors_full_cpp_st_evidence.txt`
- `/tmp/deploy/03005116/driver/cpp_output.txt`

## Remaining hardware proof blockers

To close the still-open part of `RUNTIME-003`, a hardware host must capture at least:

```bash
nvidia-smi --query-gpu=index,name,memory.total,memory.used --format=csv,noheader,nounits
nvidia-smi topo -m
npu-smi info
npu-smi info -t topo
cat /etc/hccn.conf
hccn_tool -i <device-id> -ip -g
env | grep -E 'CUDA_VISIBLE_DEVICES|ASCEND_RT_VISIBLE_DEVICES'
```

## Result

Rust now covers the no-hardware GPU/NPU collector semantics that matter on the active black-box path:

- GPU CSV query parsing is fixture-tested
- GPU/NPU visibility env filtering now matches the C++ slot-filter rule
- existing vector / `ResourceUnit` projection remains intact

What remains open is no longer vague:

- real `npu-smi` / topology / HCCN collector behavior
- real GPU topology / partition behavior
- real device-backed proof on accelerator hardware
