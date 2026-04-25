# Rust black-box 0.8.0 filtered ST runbook and handoff

Date: 2026-04-25
Branch: `rust-rewrite`
Container: `yr-e2e-rust-ab`

## Scope

This runbook is for the agreed delivery target only:
- Rust function-system binaries replace the clean 0.8.0 lane as a black box
- run the filtered cpp ST suite to green
- collective cases remain excluded by `scripts/ab/excluded_cpp_cases.txt`

## Hard constraints

- only Rust repo code may change
- do not modify C++ / SDK / ST / datasystem code to adapt to Rust
- build parallelism must stay within `-j8`
- compare against control-lane behavior/logs before guessing

## Source and container layout

Host repo:
- `/home/lzc/workspace/code/yr_rust/yuanrong-functionsystem`

B-lane container:
- `yr-e2e-rust-ab`

Container source mirror:
- `/workspace/rust_ab_0_8/src/yuanrong-functionsystem`

Container ST root:
- `/workspace/rust_ab_0_8/src/yuanrong/test/st`

Black-box replacement destination:
- `/workspace/rust_ab_0_8/src/yuanrong/output/openyuanrong/functionsystem/bin/`

## Sync host Rust source into container

```bash
cd /home/lzc/workspace/code/yr_rust/yuanrong-functionsystem

tar --exclude='target' --exclude='functionsystem/output' --exclude='.git' \
  -cf - Cargo.toml Cargo.lock run.sh functionsystem \
| docker exec -i yr-e2e-rust-ab /bin/bash -lc '
    cd /workspace/rust_ab_0_8/src/yuanrong-functionsystem && tar -xf -
  '
```

## Build Rust binaries (max j8)

```bash
docker restart yr-e2e-rust-ab >/dev/null

docker exec yr-e2e-rust-ab /bin/bash -lc '
  set -euo pipefail
  cd /workspace/rust_ab_0_8/src/yuanrong-functionsystem
  source /etc/profile.d/buildtools.sh >/dev/null 2>&1 || true
  CARGO_BUILD_JOBS=8 cargo build --release -p yr-proxy -p yr-agent -p yr-runtime-manager
'
```

## Replace black-box binaries

```bash
docker exec yr-e2e-rust-ab /bin/bash -lc '
  cp -f /workspace/rust_ab_0_8/src/yuanrong-functionsystem/target/release/function_proxy \
        /workspace/rust_ab_0_8/src/yuanrong/output/openyuanrong/functionsystem/bin/function_proxy
  cp -f /workspace/rust_ab_0_8/src/yuanrong-functionsystem/target/release/function_agent \
        /workspace/rust_ab_0_8/src/yuanrong/output/openyuanrong/functionsystem/bin/function_agent
  cp -f /workspace/rust_ab_0_8/src/yuanrong-functionsystem/target/release/runtime_manager \
        /workspace/rust_ab_0_8/src/yuanrong/output/openyuanrong/functionsystem/bin/runtime_manager
  chmod 0550 \
        /workspace/rust_ab_0_8/src/yuanrong/output/openyuanrong/functionsystem/bin/function_proxy \
        /workspace/rust_ab_0_8/src/yuanrong/output/openyuanrong/functionsystem/bin/function_agent \
        /workspace/rust_ab_0_8/src/yuanrong/output/openyuanrong/functionsystem/bin/runtime_manager
'
```

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

Expected final result:
- `Success to run cpp st`

## Final known-good evidence

Green deploy:
- `/tmp/deploy/24165656`

Key evidence file:
- `/tmp/deploy/24165656/driver/cpp_output.txt`

Summary:
- filtered cpp ST = `104 / 104 PASS`
- excluded collective cases = unchanged

## Last-mile fixes to keep in mind

### Ordered actor invoke semantics
- concurrency=1 actor invokes must be serialized in proxy
- ack immediately to the driver, then flush in order

### Recovery replay semantics
- when a runtime restarts under `RecoverRetryTimes`, replay preserved in-flight requests
- only flush replayed requests after `RecoverRsp`

### Duplicate late result suppression
- once a request's message/caller mapping is consumed, late duplicate `CallResult` must be dropped
- otherwise retry-driven cases can loop or hang

## Related docs

- `docs/analysis/100-rust-blackbox-filtered-st-green.md`
