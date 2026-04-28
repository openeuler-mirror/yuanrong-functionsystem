# RUNTIME-004 — Runtime Exit Classification First Closure

Date: 2026-04-28
Branch: `rust-rewrite`
Scope: Rust-only `runtime_manager` child-exit classification

## Goal

Close the first safe slice of `RUNTIME-004`: classify reaped runtime child exits with C++-compatible semantics before forwarding status to the function-agent path.

This does not modify upper-layer `yuanrong`, ST scripts, runtime binaries, datasystem, or the clean C++ control.

## C++ Reference Checked

Clean C++ 0.8 control inspected in `yr-e2e-master:/workspace/clean_0_8/src/yuanrong-functionsystem/functionsystem/src`:

- `runtime_manager/healthcheck/healthcheck_actor.cpp`
  - `SendInstanceStatus(..., status, ...)` treats `status == 0` as normal runtime return and emits message `runtime had been returned`.
  - `status == -1` is RuntimeMemoryExceedLimit/OOM with message `runtime memory exceed limit`.
  - Other exits flow through exception/std-log/OOM-log fallback and finally unknown-error text.
  - `WaitProcessCyclical()` is the source of reaped child status events.

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

## Verification

All commands used `CARGO_BUILD_JOBS=8`.

```bash
cargo test -p yr-runtime-manager --test health_exit_classification -- --nocapture
# 4 passed

cargo test -p yr-runtime-manager --test health_exit_classification --test runtime_ops_start_stop --test standalone_mode_test -- --nocapture
# 23 passed

cargo test -p yr-agent --test merge_process_config --test flag_compat_smoke -- --nocapture
# 9 passed

cargo check --workspace --lib --bins
# passed; only pre-existing yr-proxy warnings remain

git diff --check
# passed
```

## Boundary / Remaining RUNTIME-004 Work

This is only the first closure for exit classification. Remaining `RUNTIME-004` work:

- C++ exception backtrace file extraction (`BackTrace_<runtimeID>.log`).
- Standard log extraction and truncation semantics.
- OOM-log / dmesg / cgroup OOM attribution parity.
- Full runtime-manager OOM callback lifecycle parity with `MetricsActor`.
