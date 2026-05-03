# GPU / NPU Collector Parity Matrix

Date: 2026-05-03
Branch: `rust-rewrite`
Scope: Subgoal B from `docs/analysis/148-remaining-blackbox-parity-ai-task.md`

## Goal

Compare the official C++ 0.8 GPU/NPU heterogeneous collector path against the current Rust runtime-manager metrics path, then split the remaining work into:

1. parser / shape / projection behavior that is testable without hardware,
2. flag and environment semantics that are testable without hardware, and
3. true hardware-only behavior that must stay explicitly blocked until real GPU/NPU hosts are available.

## C++ references inspected first

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/runtime_manager/metrics/collector/system_xpu_collector.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/runtime_manager/metrics/collector/instance_xpu_collector.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/runtime_manager/metrics/collector/heterogeneous_collector/gpu_probe.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/runtime_manager/metrics/collector/heterogeneous_collector/npu_probe.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/runtime_manager/metrics/collector/heterogeneous_collector/topo_probe.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/runtime_manager/metrics/metrics_actor.cpp
```

## Rust references inspected

```text
functionsystem/src/runtime_manager/src/metrics.rs
functionsystem/src/runtime_manager/src/config.rs
functionsystem/src/runtime_manager/src/container.rs
functionsystem/src/runtime_manager/tests/metrics_resource_projection.rs
functionsystem/src/runtime_manager/tests/resource_unit_projection_test.rs
functionsystem/src/runtime_manager/tests/flag_compat_smoke.rs
```

## High-level finding

Current Rust already covers part of the C++ heterogeneous resource surface, but it covers the **projection shape** better than the **collector behavior**.

- Rust already emits C++-shaped vector resources into JSON / `ResourceUnit`.
- Rust already honors the main flags at CLI/config level:
  - `gpu_collection_enable`
  - `npu_collection_mode`
  - `npu_device_info_path`
- Rust already has a real GPU command path:
  - `nvidia-smi --query-gpu=index,name,memory.total,memory.used --format=csv,noheader,nounits`
- Rust already has a real NPU no-hardware fallback path:
  - `/dev/davinci*` count mode
  - `topology-info.json` fallback for non-`count` modes

But Rust still misses several C++ collector details:

- no `npu-smi info` parser
- no `npu-smi info -t topo` parser
- no HCCN IP extraction
- no visible-device env filtering for GPU/NPU vectors
- no GPU topology / partition collection

## Matrix

| Behavior | C++ 0.8 behavior | Current Rust state | No-hardware testability | Slice decision |
| --- | --- | --- | --- | --- |
| GPU enable flag | `MetricsActor::AddGpuNumaNpuCollectors` only installs GPU collector when `gpu_collection_enable` is on | Implemented; flag is parsed and GPU vectors are only collected when enabled | Yes | **Keep covered; add focused regression only if needed** |
| GPU command query shape | `GpuProbe` shells out to `nvidia-smi`, collects device IDs, product model, HBM total/used | Rust shells out to `nvidia-smi --query-gpu=index,name,memory.total,memory.used --format=csv,noheader,nounits` and builds vectors | Yes | **Close with parser fixture tests** for multi-row / malformed / zero-device behavior |
| GPU topology / partition | C++ also runs `nvidia-smi topo -m`, computes topology matrix and partition slots | No Rust equivalent | Not meaningfully without reproducing topology parser/fixtures | **Document as still open** |
| GPU visible-device env | C++ `TopoProbe::ExtractVisibleDevicesFromEnvVar` + `FilterDevicesEnvVar` honor `CUDA_VISIBLE_DEVICES` by filtering collected vectors by slot position | No Rust filtering found | Yes | **Close in this slice** |
| GPU health / memory / stream / latency default surface | C++ emits health plus stream/latency defaults through `SystemXPUCollector` dev-cluster metrics | Rust GPU vectors already emit HBM, usedHBM, stream=110, latency=0, health=0 when command output is available | Yes | **Keep and regression-test through parser fixtures** |
| NPU count mode | C++ `count` mode enumerates `/dev/davinci*`, defaults HBM=1000, stream=110, memory=0, health=0 | Implemented via `/dev` scan and `build_npu_count_vectors_from_ids` | Yes | **Keep covered; extend with env filtering** |
| NPU topology-info JSON fallback | C++ falls back to `npu_device_info_path` JSON when `npu-smi` path is unavailable | Implemented via `build_npu_topology_vectors_from_json` | Yes | **Keep covered; add invalid-shape regressions if needed** |
| NPU visible-device env | C++ honors `ASCEND_RT_VISIBLE_DEVICES` by filtering IDs / partition / HBM / IP / health arrays by slot position | No Rust filtering found | Yes | **Close in this slice** |
| NPU `npu-smi info` parser | C++ parses multiple output families (`910B`, `910C`, `310P3`) with regex, collecting ids, product_model, health, used/total memory, HBM | No Rust parser; non-`count` modes currently rely only on topology-info JSON | Yes, with text fixtures | **Good candidate, but larger than env-filter closure**; only do if time remains after env/parser regressions |
| NPU topology command | C++ parses `npu-smi info -t topo`, computes partition data, validates matrix | No Rust parser | Yes, with text fixtures, but broader | **Document as open unless explicitly implemented** |
| NPU device IP / SFMD path | C++ reads `/etc/hccn.conf` or falls back to `hccn_tool -i <id> -ip -g` | No Rust equivalent | Partly, with fixtures for config text | **Document as open hardware/control-network follow-up** |
| ResourceUnit/vector projection | C++ pushes vector resources and heterogeneous info through metrics actor/resource view | Implemented in Rust JSON + `ResourceUnit` projection and already unit-tested | Yes | **Already closed; keep referenced as prior baseline** |

## What current Rust tests already prove

- GPU vectors can be shaped into a C++-like heterogeneous resource:
  - `gpu_vectors_are_available_for_flag_gated_projection_inputs`
- NPU count mode can produce C++-like default vectors:
  - `xpu_vectors_follow_cpp_heterogeneous_resource_shape`
- NPU topology-info JSON fallback filters by `nodeName` and preserves partition strings:
  - `npu_topology_json_fallback_matches_cpp_node_filter_and_partition_shape`
- NPU topology fallback is wired into `build_resource_projection`:
  - `npu_topology_fallback_is_wired_into_resource_projection_for_supported_modes`
- Vector resources survive JSON / `ResourceUnit` projection:
  - `resource_update_payload_and_proto_unit_preserve_vectors_labels_and_instance_usage`

## Biggest no-hardware gaps left

### Worth closing now

1. Visible-device environment semantics:
   - `CUDA_VISIBLE_DEVICES`
   - `ASCEND_RT_VISIBLE_DEVICES`
   - same C++ fallback rule when env content is invalid: keep detected result instead of hard-failing
2. Explicit parser fixture coverage for Rust GPU CSV parsing:
   - valid multi-row output
   - malformed rows skipped
   - empty / zero-device output returns no vectors
3. Optional invalid-topology JSON regressions:
   - `number` mismatch
   - non-array IDs / partitions

### Still hardware- or larger-parser-bound

1. NPU `npu-smi info` regex family parity:
   - 910B
   - 910C
   - 310P3
2. NPU topology-command parity:
   - `npu-smi info -t topo`
   - topology matrix validation
   - partition generation from topology
3. NPU SFMD/HCCN IP parity:
   - `/etc/hccn.conf`
   - `hccn_tool -i <id> -ip -g`
4. Real GPU topology / partition parity:
   - `nvidia-smi topo -m`

## Hardware-only proof blockers to document honestly

To fully close the remaining C++ heterogeneous collector surface, a real hardware host must capture at least:

```bash
# GPU
nvidia-smi --query-gpu=index,name,memory.total,memory.used --format=csv,noheader,nounits
nvidia-smi topo -m

# NPU
npu-smi info
npu-smi info -t topo
cat /etc/hccn.conf
hccn_tool -i <device-id> -ip -g
env | grep -E 'CUDA_VISIBLE_DEVICES|ASCEND_RT_VISIBLE_DEVICES'
```

And the proof host must preserve:

- the exact raw command output,
- the active visibility env vars,
- the resulting Rust `resource_json` / `ResourceUnit`,
- and the corresponding C++ collector output for the same node.

## Slice plan

1. Add no-hardware Rust tests first for:
   - GPU CSV parser behavior
   - GPU visible-device env filtering
   - NPU visible-device env filtering
2. Implement only the minimal Rust collector/projection helpers needed for those tests.
3. Keep `npu-smi` / HCCN / topology-command parity explicitly open unless the parser work proves small enough to close safely in this slice.
