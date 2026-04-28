# RUNTIME-004 — Runtime Exit Classification First Closure

Date: 2026-04-28
Branch: `rust-rewrite`
Scope: Rust-only `runtime_manager` child-exit classification, log-message enrichment, and internal OOM advance-notify dedupe

## Goal

Close the next safe slices of `RUNTIME-004`: classify reaped runtime child exits with C++-compatible semantics, then enrich abnormal-exit messages from the same local log artifacts C++ checks before forwarding status to the function-agent path.

This does not modify upper-layer `yuanrong`, ST scripts, runtime binaries, datasystem, or the clean C++ control.

## C++ Reference Checked

Clean C++ 0.8 control inspected in `yr-e2e-master:/workspace/clean_0_8/src/yuanrong-functionsystem/functionsystem/src`:

- `runtime_manager/healthcheck/healthcheck_actor.cpp`
  - `SendInstanceStatus(..., status, ...)` treats `status == 0` as normal runtime return and emits message `runtime had been returned`.
  - `status == -1` is RuntimeMemoryExceedLimit/OOM with message `runtime memory exceed limit`.
  - Other exits flow through `GetRuntimeException(...)`: `BackTrace_<runtimeID>.log` first, then OOM/dmesg probe, then `StdRedirector::GetStdLog(..., ERROR)`, then unknown-error text.
  - `WaitProcessCyclical()` is the source of reaped child status events.
- `runtime_manager/utils/std_redirector.cpp` / `.h`
  - `STD_POSTFIX = -user_func_std.log`, `ERROR_LEVEL = ERROR`.
  - `GetStdLog` scans backward from the log tail, selecting recent lines containing both runtimeID and ERROR (default 20 hits within 1000 scanned lines).
- `runtime_manager/manager/runtime_manager.cpp` and `runtime_manager/metrics/metrics_actor.cpp`
  - `MetricsActor::SetRuntimeMemoryExceedLimitCallback(...)` wires memory-limit events into `RuntimeManager::OomKillInstance(...)`.
  - `OomKillInstance(...)` calls `HealthCheckActor::NotifyOomKillInstanceInAdvance(...)` before `InnerOomKillInstance(...)` force-stops the runtime.
  - `NotifyOomKillInstanceInAdvance(...)` sends status `-1` / `runtime memory exceed limit` first and records the OOM pid so later process reaping does not emit a second generic exit report.

## Rust Changes

- Added public `classify_wait_status(WaitStatus) -> Option<ChildExitEvent>` for deterministic testing.
- Extended `ChildExitEvent` with a `status` label so classification is not rebuilt ad hoc in the async handler.
- Normal zero exit now maps to:
  - `status = returned`
  - `exit_code = 0`
  - `error_message = runtime had been returned`
- Non-zero exit now maps to C++-style unknown-error fallback text.
- Signal exit remains a failed exit and preserves signal context.
- `handle_child_exits` forwards the classified status/message instead of recomputing `exited`/`failed` from only the exit code.
- Added `runtime_exit_log_message(...)` with C++-ordered log enrichment for abnormal exits:
  - first read `${runtime_logs_dir}/exception/BackTrace_${runtime_id}.log`;
  - then scan C++ std-log candidates (`${runtime_logs_dir}/${runtime_std_log_dir}/${std_log_name}-user_func_std.log`, runtime-id variants) for recent runtime ERROR lines, followed by Rust-captured per-runtime stderr/stdout tails;
  - preserve unknown fallback when no log artifact exists.
- Added `runtime_std_log_dir` to Rust runtime-manager config and propagated `function_agent --runtime_std_log_dir` into embedded runtime-manager config so the C++ flag no longer parse-only.
- Added an internal OOM advance-notify marker matching the C++ `NotifyOomKillInstanceInAdvance(...) -> InnerOomKillInstance(...)` ordering:
  - user-space OOM supervision now marks the runtime and sends `runtime memory exceed limit` / exit code `-1` before force-stopping the runtime;
  - the child reaper consumes that mark and suppresses the duplicate generic child-exit report if waitpid wins the stop race;
  - removing the runtime clears any stale OOM mark.

## Verification

All commands used `CARGO_BUILD_JOBS=8`.

```bash
cargo test -p yr-runtime-manager --test health_exit_classification -- --nocapture
# 7 passed

cargo test -p yr-runtime-manager --test oom_lifecycle --test health_exit_classification --test runtime_ops_start_stop --test standalone_mode_test --test oom_config --test config_defaults_grouped -- --nocapture
# 35 passed

cargo test -p yr-agent --test merge_process_config -- --nocapture
# 4 passed

cargo test -p yr-agent --test flag_compat_smoke -- --nocapture
# 5 passed

cargo check --workspace --lib --bins
# passed; only pre-existing yr-proxy warnings remain

git diff --check
# passed
```

## Boundary / Remaining RUNTIME-004 Work

This closes wait-status classification, local log-message enrichment, and internal OOM advance-notify dedupe. Remaining `RUNTIME-004` work:

- External OOM-log / dmesg attribution parity with C++ `GetOOMInfo(...)`.
- End-to-end MetricsActor-style memory-limit callback proof under deployed runtime resource pressure.
- End-to-end ST/runtime proof that Java crash files and user std logs are produced at the expected deployed paths under the Rust black-box package.
