# Rust black-box filtered cpp ST closure (0.8.0)

Date: 2026-04-25
Branch: `rust-rewrite`
Container: `yr-e2e-rust-ab`
Final green deploy: `/tmp/deploy/24165656`

## Goal

Use the Rust-rewrite function-system binaries as a black-box replacement for clean openYuanrong 0.8.0 and run the agreed filtered cpp ST suite to green.

Filtered scope:
- full cpp ST
- minus the 8 collective cases listed in `scripts/ab/excluded_cpp_cases.txt`

## Final result

Final status: **PASS**

- Filtered cpp ST: **104 / 104 PASS**
- Collective cases: still excluded by design
- Verification command returned:
  - `----------------------Success to run cpp st----------------------`

## Final verification command

```bash
FILTER="*-$(grep -Ev '^[[:space:]]*$' scripts/ab/excluded_cpp_cases.txt | paste -sd: -)"
docker restart yr-e2e-rust-ab >/dev/null

docker exec yr-e2e-rust-ab /bin/bash -lc '
  set -o pipefail
  source /etc/profile.d/buildtools.sh >/dev/null 2>&1 || true
  FILTER="'"$FILTER"'"
  cd /workspace/rust_ab_0_8/src/yuanrong/test/st
  bash test.sh -b -l cpp -f "$FILTER"
'
```

Green evidence came from deploy:
- `/tmp/deploy/24165656/driver/cpp_output.txt`

## Last-mile fixes that made the suite green

### 1. Ordered actor invoke path

For `Concurrency=1` actors, invoke requests are now handled as a local serialized queue with immediate `CallRsp` ack back to the driver, followed by controlled flush to runtime.

This closed:
- `ActorTest.ActorOrderTest`
- `ActorTest.InvokeFailedWhenKillRuntime`

### 2. Recovery replay after runtime restart

When a runtime dies but `RecoverRetryTimes` allows restart, the proxy now:
- preserves in-flight `CallRequest`
- requeues them on runtime stream close
- flushes them only after `RecoverRsp`

This closed:
- `ActorTest.ActorGroupRecoverSuccessfully`

### 3. Ignore duplicate late CallResult deliveries

If a request's caller/message mapping has already been consumed, late duplicate `CallResult` messages are dropped instead of being forwarded again.

This closed:
- `TaskTest.RetryChecker`

## Key files

Primary closure changes:
- `functionsystem/src/function_proxy/src/busproxy/invocation_handler.rs`
- `functionsystem/src/function_proxy/src/busproxy/mod.rs`

Suite still also depends on the broader Rust black-box replacement work already present in:
- `functionsystem/src/function_proxy/...`
- `functionsystem/src/function_agent/...`
- `functionsystem/src/function_master/...`
- `functionsystem/src/runtime_manager/src/executor.rs`
- `functionsystem/src/runtime_manager/src/port_manager.rs`

## Constraints that remained in force

- only Rust repo code was changed
- no C++ / SDK / ST / datasystem code was changed to adapt to Rust
- build parallelism stayed within `-j8`
- debugging was driven by comparison against control-lane behavior and logs, not guesswork

## Current conclusion

The agreed target is complete:

- Rust function-system binaries can replace the clean 0.8.0 lane as a black box
- the agreed filtered cpp ST suite is green end-to-end
