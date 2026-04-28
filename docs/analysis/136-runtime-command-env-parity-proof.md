# RUNTIME-002 — Runtime Command/Env Parity Proof

Date: 2026-04-28  
Branch: `rust-rewrite`  
Scope: Rust-only `runtime_manager` / embedded `function_agent` launch path

## Goal

Close the `RUNTIME-002` gap: Rust runtime-manager must construct runtime process commands and framework envs with the same black-box shape as the C++ 0.8 command builder, without changing upper-layer `yuanrong`, ST scripts, runtime binaries, datasystem, or the clean C++ control.

## C++ Reference Checked

Clean C++ 0.8 control inspected in `yr-e2e-master:/workspace/clean_0_8/src/yuanrong-functionsystem/functionsystem/src`:

- `runtime_manager/config/command_builder.cpp`
  - `GetCppBuildArgs`: `cppruntime`, `-runtimeId=`, `-logLevel=`, `-jobId=`, `-grpcAddress=`, `-runtimeConfigPath=`.
  - `GetGoBuildArgs`: `goruntime`, `-runtimeId=`, `-instanceId=`, `-logLevel=`, `-grpcAddress=`.
  - `PythonBuildFinalArgs`: `--rt_server_address`, `--deploy_dir`, `--runtime_id`, `--job_id`, `--log_level`.
  - `CombineEnvs` and `InheritEnv`: framework env override, `YR_` inheritance, `PATH` behavior, `UNZIPPED_WORKING_DIR` exclusion, `DS_CONNECT_TIMEOUT_SEC`.
- `runtime_manager/config/build.cpp`
  - `GeneratePosixEnvs`: `POSIX_LISTEN_ADDR`, `POD_IP`, `SNUSER_LIB_PATH`, `DATASYSTEM_ADDR`, `YR_DS_ADDRESS`, `YR_SERVER_ADDRESS`, `FUNCTION_LIB_PATH`, `YR_FUNCTION_LIB_PATH`, `LD_LIBRARY_PATH`, log/env fields.
  - `GetPosixAddress`: `proxyIP:port`.
- `runtime_manager/config/flags.cpp`
  - C++ defaults and flag names for `runtime_config_dir`, `runtime_home_dir`, `runtime_logs_dir`, `python_log_config_path`, `java_system_property`, `java_system_library_path`, `runtime_ds_connect_timeout`.

## Rust Changes

- Added `RuntimeLaunchSpec` and `build_runtime_launch_spec(...)` so command/env/credential construction is testable without spawning child processes.
- Refactored `start_runtime_process(...)` to build a launch spec first, then apply it to `std::process::Command`.
- Changed C++ runtime command from legacy bare `instance_id` to C++ 0.8 shape:
  - `arg0 = cppruntime`
  - args: `-runtimeId`, `-logLevel`, `-jobId`, `-grpcAddress`, `-runtimeConfigPath`
- Added Go runtime command shape matching C++ `goruntime` args.
- Added C++ `setCmdCred` process-credential hook parity: launch specs carry `runtime_uid`/`runtime_gid`, and `start_runtime_process` applies `setuid`/`setgid` in the child pre-exec hook when enabled.
- Expanded runtime-manager config with C++ command/env flags:
  - `runtime_logs_dir`
  - `runtime_home_dir`
  - `runtime_config_dir`
  - `python_log_config_path`
  - `java_system_property`
  - `java_system_library_path`
  - `runtime_ds_connect_timeout`
- Added C++ framework env construction for runtime children:
  - `DATASYSTEM_ADDR`, `YR_DS_ADDRESS`, `YR_SERVER_ADDRESS`
  - `FUNCTION_LIB_PATH`, `YR_FUNCTION_LIB_PATH`
  - `SNUSER_LIB_PATH`, `HOME`, `HOST_IP`, `POD_IP`
  - `YR_LOG_LEVEL`, `GLOG_log_dir`, `YR_MAX_LOG_SIZE_MB`, `YR_MAX_LOG_FILE_NUM`
  - `DS_CONNECT_TIMEOUT_SEC`
  - C++-style `UNZIPPED_WORKING_DIR` exclusion and `YR_` env inheritance.
- Added `function_agent::Config::embedded_runtime_manager_config()` so C++ launch flags parsed by `yr-agent` are passed into the embedded Rust runtime-manager instead of being parse-only, including `runtime_uid`/`runtime_gid` for `setCmdCred`.

## Tests Added / Updated

- `functionsystem/src/runtime_manager/tests/command_env_snapshot_test.rs`
  - C++ runtime args snapshot.
  - C++ framework env snapshot.
  - Python runtime command shape + C++ env snapshot.
  - `setCmdCred` credential snapshot.
- `functionsystem/src/function_agent/tests/merge_process_config.rs`
  - Embedded runtime-manager now receives C++ runtime flags from agent config.
- `functionsystem/src/runtime_manager/tests/executor_pick_runtime.rs`
  - Python executable selection now documents C++ `LookPath(language)` style behavior instead of path-list selection.

## Verification

All commands used `CARGO_BUILD_JOBS=8`.

```bash
cargo test -p yr-runtime-manager --test command_env_snapshot_test -- --nocapture
# 4 passed

cargo test -p yr-runtime-manager --test command_env_snapshot_test --test executor_pick_runtime --test flag_compat_smoke --test config_defaults_grouped --test runtime_ops_start_stop --test standalone_mode_test -- --nocapture
# 37 passed

cargo test -p yr-agent --test merge_process_config --test flag_compat_smoke -- --nocapture
# 9 passed


cargo check --workspace --lib --bins
# passed; only pre-existing yr-proxy warnings remain

git diff --check
# passed
```

## Boundary / Remaining Work

This closes command/env construction parity for the Rust runtime-manager launch surface. It does not claim full parity for:

- `RUNTIME-001` debug server lifecycle.
- `RUNTIME-003` metrics/resource-report JSON parity.
- `RUNTIME-004` OOM/exit classification and std-log extraction.
- Java detailed JVM args beyond config field propagation.
- NUMA binding hooks and advanced custom runtime working-directory/container behavior.

Those remain separate backlog rows and must be closed with their own C++ source proof and tests.
