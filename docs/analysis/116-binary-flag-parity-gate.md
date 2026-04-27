# Binary Flag Parity Gate

Date: 2026-04-27
Branch: `rust-rewrite`
Scope: Rust `yuanrong-functionsystem` package built as a black-box replacement for clean C++ 0.8.0 functionsystem.

## Goal

Validate that Rust packaged binaries do not fail startup when official 0.8.0 deployment layers pass C++-style flags.

This is intentionally broader than `--help` text comparison. Some C++ compatibility flags are accepted by Rust but hidden from Rust help because the Rust implementation ignores them for startup compatibility.

## Build under test

Container: `yr-e2e-master`

Clean C++ control package:

```text
/workspace/clean_0_8/src/yuanrong-functionsystem/output
```

Rust package under test:

```text
/workspace/rust_current_fs/output
```

Build and pack commands:

```bash
cd /workspace/rust_current_fs
export CARGO_BUILD_JOBS=8
./run.sh build -j 8
./run.sh pack
```

Build result:

```text
Build function-system successfully in 133.15 seconds
output/yr-functionsystem-v0.0.0.tar.gz     156M
output/metrics.tar.gz                      6.7M
output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl 110M
```

## Gate 1: C++ help surface vs Rust hidden acceptance

Command:

```bash
python3 tools/ops/compare_binary_flags.py \
  --cpp-root /workspace/clean_0_8/src/yuanrong-functionsystem/output \
  --rust-root /workspace/rust_current_fs/output \
  --json output/compat_audit/binary_flags.json \
  --md output/compat_audit/binary_flags.md \
  --timeout 8
```

Result summary:

| Binary | C++ flags hidden from Rust help | Hidden flags accepted by Rust | Hidden flags rejected by Rust |
| --- | ---: | ---: | ---: |
| `domain_scheduler` | 49 | 49 | 0 |
| `function_agent` | 37 | 37 | 0 |
| `function_master` | 39 | 39 | 0 |
| `function_proxy` | 36 | 36 | 0 |
| `iam_server` | 38 | 38 | 0 |
| `runtime_manager` | 98 | 98 | 0 |
| `meta_service` | 0 | 0 | 0 |
| `yr` | 0 | 0 | 0 |

Binary-name delta:

```text
C++-only binaries: none
Rust-only binaries: meta_store
```

Interpretation:

- Rust still does not advertise every C++ flag in `--help`.
- Rust does accept every C++-only help flag under the probe `--flag=dummy --help` without `unexpected argument` startup failure.
- Hidden accepted flags are compatibility shims, not proof that every ignored option has full behavioral parity.

## Gate 2: official deployment-layer flags

The C++ `--help` surface is not the whole launch contract. The official upper-layer deployment also passes flags from shell scripts and TOML/Jinja templates.

Command:

```bash
python3 tools/ops/probe_deployment_flags.py \
  --clean-yuanrong-root /workspace/clean_0_8/src/yuanrong \
  --rust-root /workspace/rust_current_fs/output \
  --json output/compat_audit/deployment_flags.json \
  --md output/compat_audit/deployment_flags.md \
  --timeout 8
```

Source files inspected:

```text
/workspace/clean_0_8/src/yuanrong/functionsystem/yuanrong-functionsystem/output/functionsystem/deploy/function_system/install.sh
/workspace/clean_0_8/src/yuanrong/output/openyuanrong/runtime/service/python/yr/cli/config.toml.jinja
/workspace/clean_0_8/src/yuanrong/deploy/k8s/charts/openyuanrong/templates/common/components-toml-configmap.yaml
```

Result summary:

| Binary | Extracted deployment flags | Accepted | Rejected |
| --- | ---: | ---: | ---: |
| `function_agent` | 106 | 106 | 0 |
| `function_master` | 75 | 75 | 0 |
| `function_proxy` | 173 | 173 | 0 |
| `iam_server` | 46 | 46 | 0 |
| `runtime_manager` | 69 | 69 | 0 |
| `domain_scheduler` | 0 | 0 | 0 |

Initial deployment probe found 39 rejected flags. They are now accepted by the centralized Rust CLI compatibility adapter:

```text
function_master: enable_fake_suspend_resume, etcd_decrypt_tool, metrics_ssl_enable, system_upgrade_watch_enable
function_proxy: create_limitation_enable, custom_resources, enable_fake_suspend_resume, enable_inherit_env, enable_ipv4_tenant_isolation, external_iam_endpoint, invoke_limitation_enable, metrics_ssl_enable, oidc_audience, oidc_project_id, oidc_project_name, oidc_workload_identity, s3_credential_type, temporary_accessKey_expiration_seconds
function_agent: enable_trace, metrics_ssl_enable, scc_algorithm, scc_base_path, scc_enable, scc_log_path, scc_primary_file, scc_standby_file, signature_validation, ssl_decrypt_tool, ssl_pwd_file
runtime_manager: enable_clean_stream_producer, enable_metrics, enable_trace, gpu_collection_enable, is_protomsg_to_runtime, log_expiration_enable, log_reuse_enable, massif_enable, runtime_direct_connection_enable, runtime_instance_debug_enable
```

## Implementation

Reusable gate tools:

```text
tools/ops/compare_binary_flags.py
tools/ops/probe_deployment_flags.py
```

Runtime compatibility adapter:

```text
functionsystem/src/common/utils/src/cli_compat.rs
```

Entrypoints using the adapter:

```text
functionsystem/src/domain_scheduler/src/main.rs
functionsystem/src/function_agent/src/main.rs
functionsystem/src/function_master/src/main.rs
functionsystem/src/function_proxy/src/main.rs
functionsystem/src/iam_server/src/lib.rs
functionsystem/src/runtime_manager/src/main.rs
```

Adapter behavior:

1. `--snake_case` is rewritten to `--snake-case` when the Rust clap command exposes the hyphenated name.
2. Known C++ 0.8 legacy flags that Rust does not implement are accepted and ignored.
3. Unknown flags outside the compatibility lists still fail normally.

## Status

R2 binary/config flag parity is closed for startup compatibility:

```text
C++ help-only hidden flags rejected by Rust: 0
Official deployment flags rejected by Rust: 0
```

Remaining boundary:

- This gate proves launch-parser compatibility, not full semantics for every ignored flag.
- Behavioral parity for advanced features such as tracing, SCC, metrics SSL, OIDC, rate limiting, and runtime direct connection remains part of broader feature-specific audits if those modes become release gates.
