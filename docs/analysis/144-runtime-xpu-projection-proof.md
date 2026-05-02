# RUNTIME-003 XPU Projection Proof

Date: 2026-05-02
Branch: `rust-rewrite`
Commit target: next commit after `ad08031`

## Goal

Advance Rust black-box replacement parity for C++ 0.8.0 runtime-manager metrics by adding Rust-only GPU/NPU flag plumbing and C++-shaped XPU vector projection support, without changing upper-layer `yuanrong`, clean C++ control, ST scripts, datasystem, or runtime packages outside Rust `yuanrong-functionsystem`.

## C++ evidence used

- `runtime_manager/config/flags.cpp`
  - `npu_device_info_path` default: `/home/sn/config/topology-info.json`
  - `npu_collection_mode` default: `all`
  - `gpu_collection_enable` default: `false`
- `runtime_manager/metrics/metrics_actor.cpp`
  - GPU collector is gated by `gpu_collection_enable`.
  - NPU collector is gated by supported `npu_collection_mode` values: `count`, `hbm`, `sfmd`, `topo`, `all`.
  - GPU/NPU metrics are `VECTORS` resources and are named `GPU/<product_model>` / `NPU/<product_model>` when a product model exists.
- `runtime_manager/metrics/collector/system_xpu_collector.cpp`
  - Heterogeneous vectors include `ids`, `HBM`, `usedHBM`, `memory`, `usedMemory`, `stream`, `latency`, `health` when values are available.
  - Heterogeneous metadata includes `vendor`, `product_model`, `partition`, and `dev_cluster_ips` when values are available.
- `runtime_manager/metrics/collector/heterogeneous_collector/npu_probe.cpp`
  - `count` mode can derive NPU ids from `/dev/davinci*` and uses C++ defaults: product model `Ascend`, HBM `1000`, memory/used/health `0`.
  - When `npu-smi` is unavailable, C++ can fall back to `npu_device_info_path` topology JSON, which supplies ids and partition information.

## Rust changes

- `runtime_manager/src/config.rs`
  - Added C++ flags:
    - `--npu_device_info_path` / `--npu-device-info-path`
    - `--npu_collection_mode` / `--npu-collection-mode`
    - `--gpu_collection_enable` / `--gpu-collection-enable`
- `function_agent/src/config.rs`
  - Accepted and forwarded `npu_device_info_path`, `npu_collection_mode`, and `gpu_collection_enable` into embedded runtime-manager config.
- `runtime_manager/src/metrics.rs`
  - Extended `VectorResource` with C++-named `heterogeneousInfo` metadata.
  - Added C++-shaped GPU/NPU vector builders.
  - Added GPU projection from `nvidia-smi --query-gpu=index,name,memory.total,memory.used --format=csv,noheader,nounits` when `gpu_collection_enable=true`.
  - Added NPU `count`-mode projection from `/dev/davinci*` ids.
  - Added NPU topology JSON fallback from `npu_device_info_path` for supported non-count modes.

## Explicit non-claims

This slice does **not** claim full C++ hardware collector parity. These remain open unless later implemented and verified:

- Full `nvidia-smi topo -m` partition parsing.
- Full `npu-smi info`, `npu-smi info -t topo`, HCCN IP collection, health refresh threads.
- Byte-level protobuf `ResourceUnit` propagation.
- Scheduler/group placement use of these vector resources.

The goal of this slice is to eliminate no-op flag gaps and add C++-shaped projection surfaces that are deterministic and testable without requiring physical GPU/NPU hardware in the ST container.

## Verification evidence

### Host

```text
cargo test -p yr-runtime-manager --test metrics_resource_projection --test flag_compat_smoke --test config_defaults_grouped -- --nocapture
=> config_defaults_grouped: 5 passed
=> flag_compat_smoke: 5 passed
=> metrics_resource_projection: 13 passed

cargo test -p yr-agent --test merge_process_config --test flag_compat_smoke -- --nocapture
=> flag_compat_smoke: 5 passed
=> merge_process_config: 4 passed

cargo check --workspace --lib --bins
=> PASS, with pre-existing yr-proxy warnings only

git diff --check
=> PASS
```

### Container build/package

Container: `yr-e2e-master`
Build copy: `/workspace/rust_current_fs`

```text
cargo test -p yr-runtime-manager --test metrics_resource_projection --test flag_compat_smoke --test config_defaults_grouped -- --nocapture
=> 5 + 5 + 13 passed

cargo test -p yr-agent --test merge_process_config --test flag_compat_smoke -- --nocapture
=> 5 + 4 passed

./run.sh build -j 8
=> Build function-system successfully

./run.sh pack
=> Built artifacts:
   /workspace/rust_current_fs/output/yr-functionsystem-v0.0.0.tar.gz
   /workspace/rust_current_fs/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
   /workspace/rust_current_fs/output/metrics.tar.gz
```

### Upper-layer proof lane

Proof lane: `/workspace/proof_source_replace_0_8`

Replaced only functionsystem artifacts in `src/yuanrong/output`, then ran unchanged upper-layer package script:

```text
bash scripts/package_yuanrong.sh -v v0.0.1
=> output/openyuanrong-v0.0.1.tar.gz
=> output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

Artifact hashes recorded in container:

- `/workspace/proof_source_replace_0_8/logs/runtime003_xpu_projection_functionsystem_hashes.txt`
- `/workspace/proof_source_replace_0_8/logs/runtime003_xpu_projection_openyuanrong_hashes.txt`

### Single-shot ST

Command:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
```

Evidence:

```text
deploy=/tmp/deploy/02104356
[==========] Running 111 tests from 6 test cases.
[==========] 111 tests from 6 test cases ran. (229601 ms total)
[  PASSED  ] 111 tests.
```

Full log:

- `/workspace/proof_source_replace_0_8/logs/runtime003_xpu_projection_full_cpp_st.log`
- `/workspace/proof_source_replace_0_8/logs/runtime003_xpu_projection_full_cpp_st_evidence.txt`

## Result

RUNTIME-003 now covers CPU/memory/disk/custom scalar projection, labels, NUMA vectors, disk vector extensions, and basic GPU/NPU flag + vector projection surfaces. Remaining RUNTIME-003 gaps are full hardware collector detail, full NUMA placement semantics, and byte-level ResourceUnit propagation.
