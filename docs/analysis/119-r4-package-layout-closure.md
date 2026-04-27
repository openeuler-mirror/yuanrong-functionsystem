# R4 Package Layout Closure

Date: 2026-04-27
Branch: `rust-rewrite`
Scope: Release/package layout parity after R3 state-store closure.

## Goal

Reduce release artifact drift between clean official C++ 0.8.0 `yr-functionsystem-v0.0.0.tar.gz` and the Rust source-replacement package, without changing upper-layer `yuanrong` build/pack/test commands.

This is a black-box compatibility audit, not a byte-for-byte identity requirement.

## Inputs

Clean C++ package:

```text
/workspace/clean_0_8/src/yuanrong-functionsystem/output/yr-functionsystem-v0.0.0.tar.gz
```

Rust package after R4 layout changes:

```text
/workspace/rust_current_fs/output/yr-functionsystem-v0.0.0.tar.gz
```

Audit output:

```text
/tmp/release_audit_r4_layout
```

## Changes made

1. `scripts/executor/builder/build_rust.py`
   - Creates `functionsystem/output/lib/cmake/opentelemetry-cpp/` so the packaged Rust tar preserves the clean C++ empty CMake metadata directory.
2. `scripts/executor/tasks/pack_task.py`
   - Excludes Rust-only release-gate scripts from `functionsystem/tools/`:
     - `compare_binary_flags.py`
     - `probe_deployment_flags.py`
   - Keeps the official runtime tool `cluster_manager.py` in the package.

These changes are packaging-only. They do not alter runtime behavior, upper-layer scripts, ST cases, runtime, datasystem, or clean C++ control.

## Verification

Local syntax/check:

```bash
python3 -m py_compile scripts/executor/builder/build_rust.py scripts/executor/tasks/pack_task.py
git diff --check
```

Container build and pack:

```bash
cd /workspace/rust_current_fs
export CARGO_BUILD_JOBS=8
./run.sh build -j 8
./run.sh pack
```

Result:

```text
Build function-system successfully in 4.80 seconds
output/yr-functionsystem-v0.0.0.tar.gz generated
output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl generated
output/metrics.tar.gz generated
```

## Latest package comparison

```text
cpp entries:        160
rust entries:       186
cpp minus rust:       1
rust minus cpp:      27
```

Remaining clean C++ entry not in Rust:

```text
functionsystem/lib/libyaml_tool.so
```

Rust-only entries still present:

```text
functionsystem/bin/meta_store
functionsystem/lib/libabseil_dll.so
functionsystem/lib/libcjson.so
functionsystem/lib/libcjson.so.1.7.17
functionsystem/lib/libdatasystem_worker.so
functionsystem/lib/libgrpc_authorization_provider.so
functionsystem/lib/libgrpc_authorization_provider.so.1.65
functionsystem/lib/libgrpc_authorization_provider.so.1.65.4
functionsystem/lib/libgrpc_plugin_support.so
functionsystem/lib/libgrpc_plugin_support.so.1.65
functionsystem/lib/libgrpc_plugin_support.so.1.65.4
functionsystem/lib/libgrpc_unsecure.so
functionsystem/lib/libgrpc_unsecure.so.42
functionsystem/lib/libgrpc_unsecure.so.42.0.0
functionsystem/lib/libgrpcpp_channelz.so
functionsystem/lib/libgrpcpp_channelz.so.1.65
functionsystem/lib/libgrpcpp_channelz.so.1.65.4
functionsystem/lib/libiconv.so
functionsystem/lib/libiconv.so.2.6.0
functionsystem/lib/libpcre.so.1
functionsystem/lib/libpcre.so.1.2.13
functionsystem/lib/libprotobuf.so
functionsystem/lib/libprotoc.so
functionsystem/lib/libprotoc.so.25.5.0
functionsystem/lib/libxml2.so
functionsystem/lib/libxml2.so.2.9.12
functionsystem/sym/meta_store.sym
```

## `libyaml_tool.so` decision

Clean C++ uses `libyaml_tool.so` through `functionsystem/src/common/service_json/service_json.cpp`:

```text
YAML_LIB_NAME = "libyaml_tool.so"
LoadFuncMetaFromServiceYaml(... litebus::os::Join(libPath, YAML_LIB_NAME))
```

That C++ helper converts service YAML to JSON via `yaml-cpp` and is dynamically loaded by the C++ `service_json` path.

Rust does not use this C++ dlopen path. Rust `function_proxy/src/instance_ctrl.rs` parses the same service YAML directly through `serde_yaml` in `service_function_meta()`. The current ST proof already exercises the official upper-layer service metadata flow and passes 111/111 accepted cases.

Therefore this change does **not** add a copied C++ `libyaml_tool.so` binary into the Rust package. Doing so would be a C++ compatibility shim inside the Rust rewrite package, and should only be done if release owners explicitly require byte-for-byte/minimal C++ helper retention rather than black-box behavior compatibility.

## Status

R4 is closed for current black-box package compatibility:

```text
Artifact names: compatible
Upper-layer package handoff: compatible
Installed runtime binary/lib layout: compatible for accepted ST
C++ CMake metadata empty directory: restored
Rust-only audit scripts in release package: removed
Remaining libyaml_tool.so: release-policy boundary, not Rust runtime behavior gap
```

Do not claim byte-for-byte identity. The Rust package remains a compatible superset with one intentionally unresolved C++ helper library boundary.
