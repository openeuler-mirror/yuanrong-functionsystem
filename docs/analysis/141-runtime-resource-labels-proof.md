# RUNTIME-003 — Runtime Resource Labels Projection Proof

Date: 2026-04-28
Branch: `rust-rewrite`
Scope: Rust-only `runtime_manager` / embedded `function_agent` metrics-label compatibility slice

## Goal

Close the `RUNTIME-003` resource-label slice found during C++ 0.8 code comparison. C++ runtime-manager collects node labels from startup environment and a Kubernetes-style label file, then exposes them through the resource-reporting path. Rust runtime-manager already had scheduler-facing CPU/memory/disk/custom-resource JSON projection, but did not include top-level `labels`, so Rust scheduler policy code that parses `resource_json.labels` could not see runtime-manager node labels.

The black-box constitution is preserved:

- Only Rust `yuanrong-functionsystem` changed.
- No upper-layer `yuanrong`, C++ control, ST script, runtime binary, or datasystem file changed.
- Build and verification used `-j8` / `CARGO_BUILD_JOBS=8`.
- Accepted ST proof used single-shot `bash test.sh -b -l cpp ...`, not the two-step debug flow.

## C++ Reference Checked

Clean C++ 0.8 source:

```text
runtime_manager/config/flags.cpp
runtime_manager/metrics/collector/resource_labels_collector.cpp
runtime_manager/metrics/collector/resource_labels_collector.h
```

Relevant C++ behavior:

- `resource_label_path` flag defaults to `/home/sn/podInfo/labels`.
- `ResourceLabelsCollector` reads these sources at construction:
  - `INIT_LABELS` env JSON.
  - `NODE_ID` env.
  - `HOST_IP` env.
  - `resource_label_path` file.
- Label file format is Kubernetes/downward-API style:

```text
key="value"
```

- Invalid label file lines are skipped.
- `NODE_ID` and `HOST_IP` are inserted only when the env value is non-empty.
- `INIT_LABELS` is expected to contain string labels; Rust keeps string-valued labels and does not widen numeric JSON labels into scheduler labels.

Rust scheduler reference checked:

```text
domain_scheduler/src/nodes.rs
```

`parse_node_labels_and_domain(...)` and `merge_node_labels_from_worker_json(...)` already consume top-level `labels` from worker `resource_json`, so projecting labels there is the smallest Rust-only compatibility path.

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

- Added runtime-manager flag `--resource_label_path` with C++ default `/home/sn/podInfo/labels`.
- Added embedded `yr-agent` forwarding for C++ `--resource_label_path` into embedded runtime-manager config.
- Added top-level `labels` to `ResourceProjection` JSON.
- Implemented label collection from:
  - `INIT_LABELS`
  - `NODE_ID`
  - `HOST_IP`
  - `resource_label_path`
- Kept parser semantics intentionally close to C++:
  - label file values must be quoted as `key="value"`;
  - invalid lines are skipped;
  - no broad whitespace normalization is applied;
  - non-string `INIT_LABELS` values are not converted into labels.

This remains a JSON projection compatibility slice, not a byte-for-byte C++ `ResourceUnit.nodelabels` protobuf port.

## Host Verification

All host commands used `CARGO_BUILD_JOBS=8`.

```bash
cargo test -p yr-runtime-manager --test metrics_resource_projection -- --nocapture
# 3 passed

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
cargo test -p yr-runtime-manager --test metrics_resource_projection -- --nocapture
# 3 passed

cargo test -p yr-agent --test merge_process_config --test flag_compat_smoke -- --nocapture
# 9 passed

./run.sh build -j 8
./run.sh pack
# build and pack succeeded
```

New Rust functionsystem artifacts copied into the proof lane:

```text
8844a9bf572edf363836e90fe4d80c92f1a00b27d5726f009debb9ad8fe8764b  output/yr-functionsystem-v0.0.0.tar.gz
2d644d060d5ab1ce2178628de6ac4b9bd44ac1bb74030fa3089f1d5c3caed909  output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
88e8fe94eead75d42f0465b0c83cd8289562c6c9645a19d85b5fd8eaaa54b978  output/metrics.tar.gz
```

Upper-layer package rebuilt unchanged after replacing only functionsystem artifacts:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong
bash scripts/package_yuanrong.sh -v v0.0.1
```

Aggregate artifacts:

```text
6532ea046ae571933ebf9cb0b74bb9f4f98521c4004389c2acb2d50ff12c22cc  output/openyuanrong-v0.0.1.tar.gz
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
/tmp/deploy/28103649
```

Result:

```text
[==========] Running 111 tests from 6 test cases.
[==========] 111 tests from 6 test cases ran. (229136 ms total)
[  PASSED  ] 111 tests.
```

## Evidence Files In Container

```text
/workspace/proof_source_replace_0_8/logs/runtime003_resource_labels_functionsystem_hashes.txt
/workspace/proof_source_replace_0_8/logs/runtime003_resource_labels_package_yuanrong.log
/workspace/proof_source_replace_0_8/logs/runtime003_resource_labels_openyuanrong_hashes.txt
/workspace/proof_source_replace_0_8/logs/runtime003_resource_labels_full_cpp_st.log
/workspace/proof_source_replace_0_8/logs/runtime003_resource_labels_full_cpp_st_evidence.txt
```

## Remaining RUNTIME-003 Work

Closed by this proof:

- Scheduler-facing CPU/memory/disk/custom-resource projection.
- C++ metrics flags forwarding through embedded agent.
- Scheduler-facing resource labels projection.

Still open if release scope requires exact advanced resources:

- Byte-level C++ `ResourceUnit` protobuf propagation instead of JSON projection.
- GPU/NPU collector parity.
- NUMA resource vectors and placement coupling.
- CPU percentage math parity for per-instance collectors.
