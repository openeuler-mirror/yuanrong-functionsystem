# PROXY-003 Invoke Admission / Rate Limit Parity Proof

Date: 2026-04-28
Branch: `rust-rewrite`
Scope: Rust `function_proxy` only

## Constitution

- C++ 0.8.0 remains the behavioral reference.
- Only Rust `yuanrong-functionsystem` was modified.
- Upper-layer `yuanrong`, runtime, datasystem, ST scripts, and clean C++ control were not modified.
- Build/test parallelism stayed at `CARGO_BUILD_JOBS=8`.

## C++ Reference Extracted

Source files inspected in clean C++ control:

- `function_proxy/common/flags/flags.cpp`
- `function_proxy/main.cpp`
- `function_proxy/busproxy/invocation_handler/invocation_handler.cpp`
- `function_proxy/busproxy/invocation_handler/invocation_handler.h`
- `function_proxy/busproxy/memory_monitor/memory_monitor.cpp`
- `function_proxy/busproxy/memory_monitor/memory_monitor.h`
- `function_proxy/common/rate_limiter/token_bucket_rate_limiter.cpp`
- `function_proxy/local_scheduler/instance_control/instance_ctrl_actor.cpp`

C++ flags and defaults:

| Flag | C++ default | Behavior |
| --- | ---: | --- |
| `invoke_limitation_enable` | `false` | Enables busproxy invoke memory admission. |
| `low_memory_threshold` | `0.6` | Low-memory fairness threshold. |
| `high_memory_threshold` | `0.8` | Hard reject threshold. |
| `message_size_threshold` | `20 * 1024` | Requests at/below this size bypass low-threshold accounting. |
| `create_limitation_enable` | `false` | Enables create token-bucket limiter. |
| `token_bucket_capacity` | `1000` | Per-tenant bucket capacity and refill rate. |

C++ invoke memory algorithm:

1. `InvocationHandler::Invoke` estimates message size as `InvokeRequest::ByteSizeLong() * 2`.
2. If memory monitor is disabled, continue normally.
3. Reject when `current + msgSize` overflows or exceeds `limit * highMemoryThreshold`.
4. Allow small messages `msgSize <= msgSizeThreshold`.
5. If current and estimated memory are both below low threshold, allocate estimated memory and allow.
6. Else calculate per-instance estimated usage and average estimated usage.
7. Allow new instance usage or usage not above average; otherwise reject.
8. Rejected invokes return `ERR_INVOKE_RATE_LIMITED` with message `system memory usage not enough, reject invoke request`.
9. Estimated memory is released when a call result is processed.

C++ create limiter algorithm:

1. Disabled unless `create_limitation_enable=true`.
2. Skip rescheduled requests and tenant `0`.
3. Use one `TokenBucketRateLimiter` per tenant.
4. Capacity and refill rate both equal `token_bucket_capacity`.

## Rust Changes

Implemented in Rust-only files:

- `functionsystem/src/function_proxy/src/invoke_admission.rs`
  - Adds C++-compatible `InvokeMemoryConfig` and `InvokeMemoryMonitor`.
  - Preserves default thresholds and `MSG_ESTIMATED_FACTOR=2`.
  - Implements high-threshold reject, small-message bypass, low-threshold fair sharing, and release.
  - Uses cgroup v2 memory files when available, with `/proc/meminfo` fallback.
- `functionsystem/src/function_proxy/src/config.rs`
  - Adds C++ flags: `invoke_limitation_enable`, `low_memory_threshold`, `high_memory_threshold`, `message_size_threshold`, `create_limitation_enable`, `token_bucket_capacity`.
- `functionsystem/src/function_proxy/src/busproxy/mod.rs`
  - Stores invoke memory monitor in `BusProxyCoordinator`.
  - Adds memory admission before runtime dispatch.
  - Releases estimated memory when `CallResultReq` arrives.
- `functionsystem/src/function_proxy/src/busproxy/invocation_handler.rs`
  - Rejects denied invokes before IAM/routing with `InvokeRsp(ERR_INVOKE_RATE_LIMITED)` and C++ message.
- `functionsystem/src/function_proxy/src/local_scheduler.rs`
  - Converts the legacy global `create_rate_limit_per_sec` limiter into C++ per-tenant token buckets when `create_limitation_enable=true`.
  - Preserves tenant `0` bypass.
- Tests:
  - `functionsystem/src/function_proxy/tests/invoke_admission_test.rs`
  - `functionsystem/src/function_proxy/tests/create_rate_limiter_test.rs`
  - `functionsystem/src/function_proxy/tests/flag_compat_smoke.rs`

## Intentional Boundary

Rust does not port C++ LiteBus `SystemMemoryCollector` actor byte-for-byte. It implements the same admission decision model using Linux cgroup v2 memory counters when available and `/proc/meminfo` fallback otherwise. That preserves black-box invoke admission behavior without importing C++ actor internals.

The create limiter still keeps the older Rust-only `create_rate_limit_per_sec` flag as a compatibility fallback when the C++ `create_limitation_enable` flag is false.

## TDD Evidence

RED observed first:

```text
cargo test -p yr-proxy --test invoke_admission_test -- --nocapture
error[E0432]: unresolved import `yr_proxy::invoke_admission`
```

GREEN verification:

```text
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test create_rate_limiter_test -- --nocapture
1 passed; 0 failed
```

```text
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test invoke_admission_test -- --nocapture
3 passed; 0 failed
```

Regression verification:

```text
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test invocation_handler_test -- --nocapture
37 passed; 0 failed
```

```text
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test flag_compat_smoke -- --nocapture
5 passed; 0 failed
```

```text
CARGO_BUILD_JOBS=8 cargo check --workspace --lib --bins
Finished dev profile successfully
```

```text
git diff --check
(no output)
```

Known pre-existing warnings remain: duplicate bin target warnings, `schedule_reporter` unused import, dead fields in busproxy structs, and unused helper items.

## Result

`PROXY-003` is closed for invoke memory admission and C++ create token-bucket flag behavior. Rust no longer treats these C++ flags as parse-only/no-op behavior.
