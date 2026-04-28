# COMMON-001 Service Metadata Validation Parity Proof

Date: 2026-04-28
Branch: `rust-rewrite`
Gap: `COMMON-001`
Status: Closed for Rust validation gate and proxy `services.yaml` use path.

## Objective

Close the C++ parity gap where Rust parsed service metadata but did not enforce the validation gates present in C++ 0.8 `service_json.cpp` before the metadata affected runtime launch.

This is a Rust-only change. The upper-layer `yuanrong`, ST scripts, runtime, datasystem, and clean C++ control are unchanged.

## C++ Reference

Reference source in clean C++ 0.8 control:

- `functionsystem/src/common/service_json/service_info.h`
- `functionsystem/src/common/service_json/service_json.cpp`

C++ validation gates used as spec:

- Service name: `^[0-9a-z]{1,16}$`
- Function name: `^[a-z][a-z0-9-]{0,126}[a-z0-9]$|^[a-z]$`
- Layer name: `^[a-z][0-9a-z-]{0,30}[0-9a-z]$`
- Kind whitelist: `faas`, `yrlib`, `custom`, `posix-runtime-custom`
- Runtime whitelist: `cpp11`, `java1.8`, `java11`, `python`, `python3`, `python3.6`, `python3.7`, `python3.8`, `python3.9`, `python3.10`, `python3.11`, `go1.13`, `posix-custom-runtime`
- CPU range: `0..=4000`
- Memory range: `0..=16000`
- Environment total key/value size: max `4096`
- Reserved env names rejected: `FAAS_FUNCTION_NAME`, `FAAS_FUNCTION_VERSION`, `FAAS_FUNCTION_BUSINESS`, `FAAS_FUNCTION_TENANTID`, `FAAS_FUNCTION_USER_FILE_PATH`, `FAAS_FUNCTION_USER_PATH_LIMITS`, `FAAS_FUNCTION_DEPLOY_DIR`, `FAAS_LAYER_DEPLOY_DIR`, `FAAS_FUNCTION_TIMEOUT`, `FAAS_FUNCTION_MEMORY`, `FAAS_FUNCTION_REGION`, `FAAS_FUNCTION_TIMEZONE`, `FAAS_FUNCTION_LANGUAGE`, `FAAS_FUNCTION_LD_LIBRARY_PATH`, `FAAS_FUNCTION_NODE_PATH`, `FAAS_FUNCTION_PYTHON_PATH`, `FAAS_FUNCTION_JAVA_PATH`
- Layers: max 5, each `layerName:version`, version `1..=1000000`
- Worker config: `minInstance >= 0`, `maxInstance 1..=1000`, `minInstance <= maxInstance`, `concurrentNum 1..=100`
- Hook handler validation:
  - `cpp11`: max len 256, no regex
  - `python`, `python3`, `python3.7`, `python3.8`, `python3.9`, `python3.10`, `python3.11`, `go1.13`: max len 64, `module.func` regex
  - `java1.8`: max len 256, Java class/method regex
  - `python3.6`, `java11`, and `posix-custom-runtime` are accepted runtimes, but C++ does not include them in the hook-handler regex map when hook handlers are present.
  - C++ rejects checkpoint and recover handlers when both are present. The log message says they "must exist at the same time", but the code returns `false`; Rust follows the code, not the message.

## Rust Changes

Changed files:

- `functionsystem/src/common/utils/src/service_json.rs`
  - Added `validate_service_infos` and `validate_service_info`.
  - Added C++-compatible validators for names, kind, runtime, resources, env, layers, worker settings, and hook handlers.
  - Added unit tests for valid metadata, invalid service/runtime/env/layers, worker limits, and hook handler limits.
- `functionsystem/src/function_proxy/src/instance_ctrl.rs`
  - Changed `service_function_meta` to parse `services.yaml` into typed `ServiceInfo` records.
  - Runs `validate_service_infos` before metadata can provide `codePath`, env, storage type, or hook handlers to instance launch.
  - Removes the previous raw YAML helper path for service metadata.
- `functionsystem/src/function_proxy/tests/instance_lifecycle_test.rs`
  - Added regression test proving a `services.yaml` with reserved env `FAAS_FUNCTION_NAME` is rejected before metadata is used.

## RED Evidence

Before implementation:

- A direct call to `validate_service_info` could not compile because no validator existed in Rust.
- The proxy path accepted an invalid `services.yaml` containing reserved env `FAAS_FUNCTION_NAME`; `service_code_path_for("hello")` returned `Some("/tmp/hello")`.

These failures matched the backlog risk: Rust accepted metadata that C++ would reject.

## GREEN Evidence

Commands run from `functionsystem/` unless noted:

```bash
CARGO_BUILD_JOBS=8 cargo test -p yr-common service_json -- --nocapture
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test instance_lifecycle_test service_yaml_with_reserved_env_is_rejected_before_metadata_use -- --nocapture
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test instance_lifecycle_test -- --nocapture
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test invocation_handler_test -- --nocapture
CARGO_BUILD_JOBS=8 cargo check --workspace --lib --bins
git diff --check
```

Observed results:

- `yr-common service_json`: 5 passed, 0 failed.
- `yr-proxy instance_lifecycle_test`: 21 passed, 0 failed.
- `yr-proxy invocation_handler_test`: 35 passed, 0 failed.
- `cargo check --workspace --lib --bins`: passed with pre-existing warnings only.
- `git diff --check`: passed.

## Scope Boundary

This closes the Rust validation and proxy metadata-use gap. It does not claim byte-for-byte replacement of the C++ `common/yaml_tool` helper surface; that remains tracked as `COMMON-002` / `RELEASE-002` release policy work.

Full source-replacement ST was not rerun for this small validation-gate change because the accepted `111/111 PASS` black-box ST proof remains covered by docs `120` and `121`, and this change is guarded by unit/integration tests. The next release proof should rerun single-shot ST after a batch of parity fixes.
