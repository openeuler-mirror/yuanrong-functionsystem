# Release Surface Parity Audit

Date: 2026-04-28
Branch: `rust-rewrite`
Scope: build, pack, installed layout, command names, package names, version defaults, and release-policy boundaries.

## Summary

Rust now builds and packages through the same top-level `./run.sh build` and `./run.sh pack` interface expected by upper-layer `yuanrong`. The current black-box policy is compatible-superset rather than byte-for-byte identical. That policy is enough for the accepted ST source-replacement proof, but it must stay explicit: extra Rust artifacts and missing C++ helper libraries are not automatically acceptable for a broader release.

## Source evidence

| Surface | Evidence | Finding |
| --- | --- | --- |
| Build entry | `run.sh` delegates to `scripts/executor/make_functionsystem.py`; default builder is `rust` | Upper-layer command shape can remain unchanged. |
| Build jobs | `make_functionsystem.py build -j/--job_num`; project constitution caps at `-j8` | Always pass `-j 8` or lower in automation. |
| Pack entry | `make_functionsystem.py pack -v 0.0.0` and `scripts/executor/tasks/pack_task.py` | Pack layout reuses official `functionsystem/output` layout. |
| Binary install | `scripts/executor/builder/build_rust.py` installs Rust binaries into `functionsystem/output/bin` | Same consumer path as C++ output. |
| Binary set | Build script includes Rust bins such as `function_proxy`, `function_agent`, `function_master`, `runtime_manager`, `domain_scheduler`, `iam_server`, `meta_store` | Rust also preserves `meta_service` through Go build; extra `meta_store` is a release-policy item. |
| Tar name | `pack_task.py` emits `yr-functionsystem-v{version}.tar.gz` | Upper-layer `yuanrong` can consume same tar name. |
| Wheel | `scripts/config/pyproject.toml` + `build_whl.py` emit function system wheel | Wheel name/layout is compatible with current source-replacement proof. |
| Metrics | `pack_task.py` skips missing metrics build output for Rust builder with warning | Compatible with ST; metrics package parity remains policy/test item. |
| Missing C++ helper | Prior docs identify `libyaml_tool.so` as C++-minus-Rust boundary | No ST failure, but not byte-for-byte package parity. |
| Extra libs/bins | Prior docs identify Rust package as larger/superset | Needs release owner acceptance or cleanup. |

## Findings

### RELEASE-001: release policy is compatible-superset, not byte-for-byte

The current package has passed the accepted source-replacement ST target, but previous package audits show Rust and C++ package entries differ. Rust package compatibility relies on consumers using the official install/runtime surfaces rather than exact file inventory.

Classification: `Release-policy boundary` / `P3`.

### RELEASE-002: `libyaml_tool.so` remains the clearest C++-minus-Rust file

Rust parses service YAML directly. If any external consumer loads `libyaml_tool.so`, the Rust package is not a drop-in file-level replacement.

Classification: `Release-policy boundary` / `P3`, or `P1` only if an actual consumer requires it.

### RELEASE-003: metrics package parity is intentionally weak for Rust builder

`pack_task.py` logs that missing metrics build output is expected for Rust builder and skips metrics packaging. This is acceptable for current ST but not full release parity if metrics artifacts are required.

Classification: `Release-policy boundary` / `P2`.

### RELEASE-004: versioning is command-compatible but not semantic release parity

Build/pack default version is `0.0.0`, matching the internal functionsystem artifact consumed by upper-layer packaging. This is not the same as upstream OpenYuanrong 0.8.0 product versioning. It is acceptable only because upper-layer packaging owns the aggregate release version.

Classification: `Release-policy boundary` / `P3`.

### RELEASE-005: one-shot ST command remains the acceptance command

The accepted flow is the official one-shot source-replacement ST command. Two-step `test.sh -s -r` then `test.sh -b -l cpp` is a debug mode because the second step may not redeploy cleanly and can create misleading port/state conflicts.

Classification: `Equivalent for acceptance flow`; keep documented to avoid reintroducing stale harness assumptions.

## Required release gates before broad claim

1. Build with `./run.sh build -j 8` and pack with `./run.sh pack` from a clean Rust source checkout.
2. Replace only `yuanrong-functionsystem` source/artifacts under upper-layer `yuanrong`; do not edit upper-layer scripts.
3. Run one-shot accepted ST: `bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"`.
4. Compare package file inventory against clean C++ and explicitly approve each C++-minus-Rust and Rust-plus-C++ entry.
5. Run binary flag parser gate and keep no-op behavior inventory current.

## Next checks

1. Re-run package inventory after the next implementation sprint.
2. Decide `libyaml_tool.so` and metrics artifact policy.
3. Add a small release-readiness script that prints build command, pack output names, binary list, and package diffs without modifying upper-layer code.
