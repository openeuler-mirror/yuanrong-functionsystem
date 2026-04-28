# COMMON-003 LiteBus SSL Env Parity Proof

Date: 2026-04-28
Branch: `rust-rewrite`
Gap: `COMMON-003`
Status: Closed for C++ `ssl_config.cpp` LiteBus SSL certificate path resolution, validation, and process env setup.

## Objective

Close the Rust gap where C++ launch flags such as `--ssl_enable`, `--ssl_base_path`, `--ssl_root_file`, `--ssl_cert_file`, and `--ssl_key_file` were accepted by Rust components but did not drive the C++ `ssl_config.cpp` behavior.

This is a Rust-only change. No upper-layer `yuanrong`, ST script, runtime, datasystem, or clean C++ control file is changed.

## C++ Reference

Clean C++ 0.8 reference files:

- `functionsystem/src/common/utils/ssl_config.h`
- `functionsystem/src/common/utils/ssl_config.cpp`
- `functionsystem/src/function_proxy/main.cpp`
- `functionsystem/src/function_agent/main.cpp`
- `functionsystem/src/function_master/main.cpp`
- `functionsystem/src/domain_scheduler/main.cpp`

C++ behavior used as spec:

- `GetSSLCertConfig(flags)` returns default disabled config when both `ssl_enable` and `metrics_ssl_enable` are false.
- When either is enabled, it resolves real paths for:
  - `ssl_base_path`
  - `ssl_base_path/ssl_root_file`
  - `ssl_base_path/ssl_cert_file`
  - `ssl_base_path/ssl_key_file`
- If any required file is missing, it returns a config whose `isEnable` remains false.
- When `ssl_enable` is true, startup calls `InitLitebusSSLEnv`; failure aborts component startup.
- `InitLitebusSSLEnv` sets:
  - `LITEBUS_SSL_ENABLED=1`
  - `LITEBUS_SSL_VERIFY_CERT=1`
  - `LITEBUS_SSL_DECRYPT_TYPE=0`
  - `LITEBUS_SSL_CA_FILE=<root cert>`
  - `LITEBUS_SSL_CA_DIR=<cert base path>`
  - `LITEBUS_SSL_CERT_FILE=<module cert>`
  - `LITEBUS_SSL_KEY_FILE=<module key>`

## Rust Changes

Changed files:

- `functionsystem/src/common/utils/src/ssl_config.rs`
  - Adds `SslInputs`, `SslCertConfig`, `get_real_path`, `get_ssl_cert_config`, `litebus_ssl_envs`, and `apply_litebus_ssl_envs`.
  - Mirrors C++ path resolution and missing-file disabled behavior.
- `functionsystem/src/common/utils/src/lib.rs`
  - Exposes `ssl_config` module.
- `functionsystem/src/function_proxy/src/config.rs`
- `functionsystem/src/function_agent/src/config.rs`
- `functionsystem/src/function_master/src/config.rs`
  - Preserve `metrics_ssl_enable` instead of dropping it as an ignored legacy flag.
- `functionsystem/src/function_proxy/src/main.rs`
- `functionsystem/src/function_agent/src/main.rs`
- `functionsystem/src/function_master/src/main.rs`
- `functionsystem/src/domain_scheduler/src/config.rs`
- `functionsystem/src/domain_scheduler/src/main.rs`
  - Initialize LiteBus SSL envs during early startup before servers/tasks are spawned.
  - Abort startup when `ssl_enable=true` but cert files are invalid, matching C++.
- `functionsystem/src/common/utils/tests/ssl_config_tests.rs`
  - Adds RED/GREEN regression tests for disabled, valid, and missing-file cases.
- `functionsystem/src/function_agent/tests/flag_compat_smoke.rs`
  - Updates a stale Rust-only test expectation to match the current production runtime path expansion, which includes Python runtime in addition to C++ and Go.

## RED Evidence

Before implementation:

```text
error[E0432]: unresolved import `yr_common::ssl_config`
 --> functionsystem/src/common/utils/tests/ssl_config_tests.rs:3:16
  |
3 | use yr_common::ssl_config::{get_ssl_cert_config, litebus_ssl_envs, SslInputs};
  |                ^^^^^^^^^^ could not find `ssl_config` in `yr_common`
```

This proved Rust had no equivalent public SSL config module for the C++ behavior.

## GREEN Evidence

Commands run with build parallelism capped by `CARGO_BUILD_JOBS=8`:

```bash
CARGO_BUILD_JOBS=8 cargo test -p yr-common --test ssl_config_tests -- --nocapture
CARGO_BUILD_JOBS=8 cargo check --workspace --lib --bins
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test flag_compat_smoke -- --nocapture
CARGO_BUILD_JOBS=8 cargo test -p yr-agent --test flag_compat_smoke -- --nocapture
CARGO_BUILD_JOBS=8 cargo test -p yr-master --test flag_compat_smoke -- --nocapture
CARGO_BUILD_JOBS=8 cargo test -p yr-domain-scheduler --test flag_compat_smoke -- --nocapture
git diff --check
```

Observed results:

- `yr-common ssl_config_tests`: 3 passed, 0 failed.
- `cargo check --workspace --lib --bins`: passed with pre-existing warnings only.
- `yr-proxy flag_compat_smoke`: 5 passed, 0 failed.
- `yr-agent flag_compat_smoke`: 5 passed, 0 failed after aligning stale runtime-path expectation to existing production behavior.
- `yr-master flag_compat_smoke`: 11 passed, 0 failed.
- `yr-domain-scheduler flag_compat_smoke`: 4 passed, 0 failed.
- `git diff --check`: passed.

## Scope Boundary

This closes the LiteBus SSL environment part of `COMMON-003`. It does not claim full etcd/MetaStore TLS transport parity. Rust `MetaStoreClientConfig.ssl_config` is still a placeholder and direct etcd clients still connect without TLS material; that should stay tracked as a separate MetaStore/etcd TLS parity item if secure etcd deployments are in scope.
