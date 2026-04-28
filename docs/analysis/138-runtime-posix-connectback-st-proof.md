# RUNTIME-004 — Server-Mode Runtime Connect-Back Proof

Date: 2026-04-28
Branch: `rust-rewrite`
Scope: Rust-only `runtime_manager` C++/Go/Python runtime launch address selection under proxy server mode

## Goal

Close the deployed-path failure found while proving runtime abnormal-exit handling: C++ runtime processes launched by the Rust black-box package were connecting back to the allocated runtime port instead of the proxy POSIX gRPC port.

This keeps the black-box constitution intact: only Rust `yuanrong-functionsystem` was changed. No upper-layer `yuanrong`, ST script, runtime binary, datasystem, or clean C++ control file was modified.

## Failure Evidence Before Fix

Targeted exception ST command:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
FILTER="TaskTest.ExceptionChain:TaskTest.ExceptionDying:TaskTest.ExceptionMethod:ActorTest.InvokeFailedWhenRuntimeSEGV:ActorTest.ExceptionIllegalInstruction:ActorTest.ExceptionInterruptSignal"
bash test.sh -b -l cpp -f "$FILTER"
```

Failed deployment:

```text
/tmp/deploy/28091528
```

Observed symptoms:

- `ActorTest.ExceptionIllegalInstruction`, `ActorTest.ExceptionInterruptSignal`, and `TaskTest.ExceptionChain` failed with `runtime connect-back timed out after 60s`.
- Runtime log examples showed the C++ runtime connecting to the wrong target:

```text
grpc client target is function-proxy:172.17.0.2:28532
```

- The same deployment's Rust proxy advertised:

```text
starting yr-proxy POSIX gRPC posix_addr=172.17.0.2:8403
StartInstance success ... runtime_port=28532
```

So the runtime used the allocated runtime port (`28532`) as `-grpcAddress`, while C++ server-mode semantics require proxy POSIX gRPC (`8403`).

## C++ Reference Checked

Clean C++ 0.8 source:

- `runtime_manager/executor/runtime_executor.cpp`
  - `StartInstanceWithoutPrestart(...)` checks `tlsConfig.enableservermode()`.
  - When enabled, it uses `tlsConfig.posixport()` as `port` and sets `features.serverMode = false`.
  - Only the non-server-mode path calls `PortManager::RequestPort(runtimeID)` and marks `features.serverMode = true`.
- `runtime_manager/config/command_builder.cpp`
  - `GetCppBuildArgs(...)` passes `-grpcAddress=` from `GetPosixAddress(config_, port)`.
- `runtime_manager/config/build.cpp`
  - `GetPosixAddress(config, port)` returns `config.proxyIP + ":" + port`.
- `function_agent/common/utils.cpp`
  - `SetTLSConfig(...)` copies `DeployInstanceRequest.enableServerMode` and `posixPort` into `RuntimeConfig.tlsConfig`.
- `function_proxy/local_scheduler/instance_control/instance_ctrl_actor.cpp`
  - `AddCredToDeployInstanceReq(...)` sets `enableServerMode`, `posixPort`, and `dposixUdsPath` on the deploy request.

Rust's internal `StartInstanceRequest` does not carry the C++ `TLSConfig` fields, but the Rust proxy already inserts equivalent server-mode information into `env_vars`:

```text
POSIX_LISTEN_ADDR=<proxy_host>:<proxy_posix_port>
PROXY_GRPC_SERVER_PORT=<proxy_posix_port>
YR_SERVER_ADDRESS=<proxy_host>:<proxy_posix_port>
```

## Rust Change

File changed:

```text
functionsystem/src/runtime_manager/src/executor.rs
```

Added a launch-address helper:

- Prefer `StartInstanceRequest.env_vars["POSIX_LISTEN_ADDR"]` when present and non-empty.
- Fall back to the previous `runtime_grpc_address(cfg, allocated_port)` for standalone/non-server-mode requests.

The helper is now used by:

- `build_runtime_env(...)` for `POSIX_LISTEN_ADDR`.
- C++ runtime launch args `-grpcAddress=...`.
- Go runtime launch args `-grpcAddress=...`.
- Python runtime launch arg `--rt_server_address ...` via the existing shared `listen_addr`.

Regression test added:

```text
functionsystem/src/runtime_manager/tests/command_env_snapshot_test.rs
```

Test name:

```text
cpp_runtime_launch_args_use_proxy_posix_address_when_server_mode_env_is_supplied
```

The test first failed against old Rust behavior because `-grpcAddress=10.0.0.2:30123` was produced instead of `-grpcAddress=10.0.0.2:8403`. After the Rust fix, it passes and asserts that server-mode C++ launch args do not use the allocated runtime port.

## Verification

All Rust commands used `CARGO_BUILD_JOBS=8`.

Host verification:

```bash
cargo test -p yr-runtime-manager --test command_env_snapshot_test -- --nocapture
# 5 passed

cargo test -p yr-runtime-manager --test health_exit_classification --test oom_lifecycle -- --nocapture
# 11 passed

cargo test -p yr-runtime-manager --test runtime_ops_start_stop --test standalone_mode_test -- --nocapture
# 19 passed

cargo check --workspace --lib --bins
# passed; only pre-existing yr-proxy warnings remain

git diff --check
# passed
```

Container black-box build/package verification in `yr-e2e-master:/workspace/rust_current_fs`:

```bash
export CARGO_BUILD_JOBS=8
cargo test -p yr-runtime-manager --test command_env_snapshot_test -- --nocapture
# 5 passed

./run.sh build -j 8
./run.sh pack
# build and pack succeeded
```

New Rust functionsystem artifact hashes:

```text
b8e8b4a40b0c58b4f670f0879239d58d0e931f5e4a2c6c502c717b1613277a73  output/yr-functionsystem-v0.0.0.tar.gz
09bf43e9fe8e74d5582c23f05b9723405ba8226309498004bc996e45ba818007  output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
ea66547fda2ec9a1e454aa32e7659d4ce5660e9d38a2050300024cd3d9adae83  output/metrics.tar.gz
```

Upper-layer `yuanrong` package was rebuilt unchanged after replacing only functionsystem artifacts:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong
bash scripts/package_yuanrong.sh -v v0.0.1
```

Aggregate hashes:

```text
b66b74ea08f0f8dafb88a5e4656e670e6979074149f46b1990bc76ff946f7bb1  output/openyuanrong-v0.0.1.tar.gz
3a63f3471cc006f81612d0298d9023ff9406308995866fa2f8b0872eb05e9683  output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

Targeted deployed ST after fix:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
FILTER="TaskTest.ExceptionChain:TaskTest.ExceptionDying:TaskTest.ExceptionMethod:ActorTest.InvokeFailedWhenRuntimeSEGV:ActorTest.ExceptionIllegalInstruction:ActorTest.ExceptionInterruptSignal"
bash test.sh -b -l cpp -f "$FILTER"
# Success to run cpp st
```

Deployment:

```text
/tmp/deploy/28093802
```

GTest result:

```text
[ RUN      ] ActorTest.InvokeFailedWhenRuntimeSEGV
[       OK ] ActorTest.InvokeFailedWhenRuntimeSEGV (5088 ms)
[ RUN      ] ActorTest.ExceptionIllegalInstruction
[       OK ] ActorTest.ExceptionIllegalInstruction (5024 ms)
[ RUN      ] ActorTest.ExceptionInterruptSignal
[       OK ] ActorTest.ExceptionInterruptSignal (223 ms)
[ RUN      ] TaskTest.ExceptionChain
[       OK ] TaskTest.ExceptionChain (8216 ms)
[ RUN      ] TaskTest.ExceptionDying
[       OK ] TaskTest.ExceptionDying (6704 ms)
[ RUN      ] TaskTest.ExceptionMethod
[       OK ] TaskTest.ExceptionMethod (4365 ms)
[  PASSED  ] 6 tests.
```

Runtime connect-back proof after fix:

```text
starting yr-proxy POSIX gRPC posix_addr=172.17.0.2:8403
grpc client target is function-proxy:172.17.0.2:8403
```

The successful deployment also produced the expected abnormal runtime stderr evidence (`SIGFPE`, `SIGILL`, `SIGABRT`, backtrace snippets), confirming the runtime reached exception handling instead of stalling at connect-back.

## Evidence Files In Container

```text
/workspace/proof_source_replace_0_8/logs/runtime004_posix_fix_functionsystem_hashes.txt
/workspace/proof_source_replace_0_8/logs/runtime004_posix_fix_package_yuanrong.log
/workspace/proof_source_replace_0_8/logs/runtime004_posix_fix_openyuanrong_hashes.txt
/workspace/proof_source_replace_0_8/logs/runtime004_posix_fix_exception_st.log
/workspace/proof_source_replace_0_8/logs/runtime004_posix_fix_deployed_path_evidence.txt
```


## Full ST Regression After Fix

After the targeted abnormal-exit slice passed, the same replaced Rust functionsystem package was also verified with the current accepted single-shot cpp ST baseline:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
# Success to run cpp st
```

Deployment:

```text
/tmp/deploy/28094358
```

GTest result:

```text
[==========] Running 111 tests from 6 test cases.
[==========] 111 tests from 6 test cases ran. (233432 ms total)
[  PASSED  ] 111 tests.
```

Connect-back samples in the full run also show C++ runtimes targeting the proxy POSIX gRPC endpoint instead of allocated runtime ports:

```text
starting yr-proxy POSIX gRPC posix_addr=172.17.0.2:8403
grpc client target is function-proxy:172.17.0.2:8403
```

Additional evidence file:

```text
/workspace/proof_source_replace_0_8/logs/runtime004_posix_fix_full_cpp_st.log
/workspace/proof_source_replace_0_8/logs/runtime004_posix_fix_full_cpp_st_evidence.txt
```

## Status

`RUNTIME-004` deployed exception path is now proven for the C++ ST abnormal-exit cases that previously exposed the port bug. The remaining RUNTIME-004 work, if release scope requires it, is a dedicated resource-pressure/OOM proof rather than the runtime connect-back path.
