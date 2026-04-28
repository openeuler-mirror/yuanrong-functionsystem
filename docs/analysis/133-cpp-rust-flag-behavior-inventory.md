# C++ / Rust 0.8 Flag Behavior Inventory

Date: 2026-04-28
Branch: `rust-rewrite`
Scope: `COMMON-006` and `PROXY-007`
Status: Inventory baseline; use this to select behavior implementation tasks. Parser acceptance alone is not parity.

## Constitution

- Only Rust `yuanrong-functionsystem` may be changed to close gaps.
- C++ 0.8 source is the behavior reference.
- Do not change upper-layer `yuanrong` launch scripts to hide Rust gaps.
- Keep build/test parallelism at `-j8` / `CARGO_BUILD_JOBS=8`.
- Every high-risk no-op flag must become either implemented behavior, explicit unsupported policy, or release-scope exclusion.

## Reference Inputs

C++ reference inspected from clean 0.8 control:

- `common/common_flags/common_flags.cpp`
- `function_proxy/common/flags/flags.cpp`
- `function_master/common/flags/flags.cpp`
- `common/utils/ssl_config.cpp`
- component startup call sites in `function_proxy/main.cpp`, `function_agent/main.cpp`, `function_master/main.cpp`, `domain_scheduler/main.cpp`

Rust reference inspected:

- `functionsystem/src/common/utils/src/cli_compat.rs`
- `functionsystem/src/common/utils/src/config.rs`
- `functionsystem/src/function_proxy/src/config.rs`
- `functionsystem/src/function_agent/src/config.rs`
- `functionsystem/src/function_master/src/config.rs`
- `functionsystem/src/domain_scheduler/src/config.rs`
- `functionsystem/src/common/utils/src/ssl_config.rs`

## Status Legend

| Status | Meaning |
| --- | --- |
| `Implemented` | Rust parses the flag and drives equivalent or intentionally compatible behavior. |
| `Partial` | Rust has some behavior, but C++ edge semantics or subsystem breadth are not proven. |
| `Accepted/no-op` | Rust accepts the flag so official launch does not fail, but no equivalent behavior is wired. |
| `Policy boundary` | Deliberately not byte-for-byte equivalent; must remain documented. |
| `Unknown` | Needs deeper source trace or A/B probe before classifying. |

## Common Flags

| Flag group | C++ behavior | Rust status | Evidence | Next action |
| --- | --- | --- | --- | --- |
| `ssl_enable`, `ssl_base_path`, `ssl_root_file`, `ssl_cert_file`, `ssl_key_file`, `metrics_ssl_enable` | Resolve paths, validate files, set `LITEBUS_SSL_*`; abort startup on enabled invalid certs | Implemented for LiteBus env | `ssl_config.rs`, docs `132` | Keep etcd/MetaStore TLS separate. |
| `etcd_auth_type=TLS`, `etcd_root_ca_file`, `etcd_cert_file`, `etcd_key_file`, `etcd_ssl_base_path`, `etcd_target_name_override` | Build gRPC TLS credentials for etcd/MetaStore clients | Accepted/partial placeholder | `MetaStoreClientConfig.ssl_config` exists but `Client::connect(..., None)` is used | Create dedicated etcd/MetaStore TLS task before secure etcd release. |
| `enable_metrics`, `metrics_config`, `metrics_config_file`, observability ports | Initialize metrics module/exporters | Partial | Rust has Prometheus/resource paths but no full C++ metrics stack proof | Build metrics A/B probe after core ST closure. |
| `enable_trace`, `trace_config` | Initialize tracing/observability | Accepted/no-op or partial | Mostly parsed/accepted in ignored structs | Classify as no-op unless source trace proves behavior. |
| `min_instance_*`, `max_instance_*`, `max_priority`, `enable_preemption`, `schedule_relaxed`, `aggregated_strategy` | Drive scheduler resource limits and policy | Partial | Rust scheduler/resource config exists; policy breadth not fully proven | Fold into `SCHED-002` policy matrix. |
| `system_auth_mode`, `resource_path`, `decrypt_algorithm` | AK/SK, secret, crypto setup | Partial | Rust `aksk.rs` exists; component coverage not fully proven | Tie to IAM/auth release scope. |
| `meta_store_excluded_keys`, metastore healthcheck flags | Affect MetaStore routing and health handling | Partial | Rust excluded key routing exists for client; health semantics not fully compared | Fold into `META-001` / `META-002`. |
| `quota_config_file` | Quota/resource policy | Accepted/no-op likely | No full Rust quota behavior proven | P2 unless quota release scope becomes P1. |

## Function Proxy Flags

| Flag group | C++ behavior | Rust status | Evidence | Next action |
| --- | --- | --- | --- | --- |
| `services_path`, `lib_path`, `function_meta_path` | Load service metadata through yaml helper/lib and local function meta | Partial | `services_path` now typed+validated; `lib_path`/yaml helper is policy boundary | Keep `COMMON-002` / `RELEASE-002` open. |
| `enable_merge_process`, runtime path/log/env flags | Launch embedded function-agent/runtime-manager path | Partial/implemented for ST lane | Rust merge mode and runtime env path helpers exist | Continue `RUNTIME-002` command/env snapshot work. |
| `enable_iam`, `iam_base_path`, `iam_policy_file`, `iam_meta_store_address`, `iam_credential_type` | InternalIAM verification and authorization for create/invoke | Partial/high risk | Rust create auth hook exists; invoke auth not proven | `PROXY-002` should be next P1 behavior task if IAM is in scope. |
| `invoke_limitation_enable`, `create_limitation_enable`, `token_bucket_capacity`, memory thresholds | Token bucket / overload / memory admission | Implemented | Rust has C++-compatible invoke memory admission and per-tenant create token buckets | Proof `docs/analysis/135-proxy-invoke-admission-parity-proof.md`. |
| `enable_traefik_registry`, `traefik_*` | Register service routes to Traefik/etcd | Accepted/no-op likely | Present in `ProxyCppIgnored`; no route registry behavior proven | P2/P1 depending ingress release scope. |
| `runtime_ds_auth_enable`, `runtime_ds_encrypt_enable`, `curve_key_path`, DS public/private keys | Runtime↔datasystem auth/encryption | Accepted/no-op likely | Flags accepted; no equivalent Rust behavior found in proxy path | Keep outside ST unless secure DS scope is required. |
| `state_storage_type`, cache storage host/port/auth/prefix | DS/MetaStore state persistence backend | Partial/policy boundary | Rust state store uses MetaStore-backed persistence | Keep `PROXY-006` policy unless DS exactness required. |
| `runtime_recover_enable`, runtime heartbeat timeouts, shutdown/connect timeouts | Runtime lifecycle/recovery behavior | Partial | Rust lifecycle work exists; exact timeout semantics unproven | Fold into runtime lifecycle A/B task. |
| `enable_tenant_affinity`, `tenant_pod_reuse_time_window`, tenant isolation | Tenant placement/reuse policies | Accepted/no-op or partial | Affinity modules exist but not fully traced | Fold into scheduler/affinity matrix. |

## Function Agent / Runtime-Manager Flags

| Flag group | C++ behavior | Rust status | Evidence | Next action |
| --- | --- | --- | --- | --- |
| `runtime_dir`, runtime home/config/std-log dirs, LD/Python/Java paths | Command builder and runtime env construction | Implemented for core C++/Go/Python launch shape | Rust launch spec builder, embedded agent config propagation, proof `docs/analysis/136-runtime-command-env-parity-proof.md` | Keep Java/custom-runtime/NUMA working-dir details as follow-up runtime parity rows. |
| `data_system_enable`, DS host/port/connect timeout | Runtime DS access | Partial | Rust config and agent/runtime paths exist; ObjRef/DS exactness remains separate | Keep DS A/B probes. |
| `oom_kill_*`, `massif_enable`, memory detection interval | OOM/health/exit classification | Partial | Rust OOM modules exist; exact semantics unproven | `RUNTIME-004` safe OOM/exit probes. |
| `user_log_*`, log expiration/reuse | User log routing/retention | Partial/no-op likely | Runtime manager log code exists; C++ parity not audited | P2 unless log compatibility is release scope. |
| `npu_collection_*`, `gpu_collection_enable`, resource label path | Device/resource reporting | Partial | Rust metrics/resource reporting partial | `RUNTIME-003` resource JSON A/B. |
| plugin/SCC/signature/venv flags | Plugin manager and secure code checks | Accepted/no-op or partial | Rust has lightweight venv hints; full plugin manager not proven | `AGENT-002` scope decision. |

## Function Master / Scheduler Flags

| Flag group | C++ behavior | Rust status | Evidence | Next action |
| --- | --- | --- | --- | --- |
| `d1`, `d2`, domain/local scheduler topology | Domain scheduler tree sizing | Implemented/partial | Rust config and topology manager exist | `SCHED-001` A/B scheduling scenarios. |
| `migrate_enable`, taint flags, system upgrade watch flags | Migration, taint tolerance, upgrade watch | Accepted/no-op or partial | Some config accepted; behavior not fully traced | `MASTER-003` P2/P1 matrix. |
| `services_path`, `function_meta_path`, `enable_sync_sys_func` | System function metadata sync | Partial | Rust system loader exists; exact C++ parity unproven | Add system function sync tests if release scope. |
| `enable_meta_store`, `enable_persistence`, `meta_store_mode`, flush limits | Embedded MetaStore/persistence | Partial | Rust embedded MetaStore exists | `META-002` crash/restart tests. |
| frontend pool / horizontal scale / pool config | Scaling and pool control | Partial | Rust scaling tests exist; full actor parity unproven | Fold into `MASTER-002`. |

## Immediate Recommendations

1. Treat `PROXY-002` IAM as the next highest-risk P1 if secure/internal auth deployments are in release scope.
2. Treat etcd/MetaStore TLS as a separate P1 if `etcd_auth_type=TLS` deployments are in release scope; do not hide it under `COMMON-003`, which now only closes LiteBus env behavior.
3. Treat `RUNTIME-002` command/env snapshot parity as the next broad black-box confidence task because ST cannot cover every runtime launch variant.
4. Keep accepted/no-op flags documented; do not remove them from parsing because official upper-layer launch commands must remain unchanged.
