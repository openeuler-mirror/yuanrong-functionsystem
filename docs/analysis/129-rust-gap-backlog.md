# Rust Gap Backlog After Code-Level Parity Audit

Date: 2026-04-28
Branch: `rust-rewrite`
Purpose: prioritized backlog for Rust-only hardening after C++ 0.8 code parity review.

## Rules for using this backlog

- Only modify Rust `yuanrong-functionsystem` when implementing these items.
- Do not change upper-layer `yuanrong`, runtime, datasystem, ST scripts, or clean C++ control to make Rust pass.
- Reproduce/lock each gap with a Rust unit/integration test or C++/Rust A/B proof before patching when possible.
- Keep build parallelism at `-j8` / `CARGO_BUILD_JOBS=8`.
- Do not treat parser acceptance as behavior parity.

## Priority meanings

| Priority | Meaning |
| --- | --- |
| `P0` | Official deploy/ST/data-correctness path is likely broken. |
| `P1` | Likely production black-box path not covered by current ST. |
| `P2` | Advanced/optional behavior or parse-only compatibility risk. |
| `P3` | Release policy or intentionally deferred equivalence decision. |

## Current backlog

| Gap ID | Module | C++ capability | Rust status | Evidence | Risk | ST covered? | Priority | Recommendation |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| COMMON-001 | common service metadata | Full service/function validation for names, kind, runtime, CPU/memory, env, layers, hook handlers | Closed: Rust validates typed service metadata and rejects invalid `services.yaml` before proxy metadata use | C++ `common/service_json/service_json.cpp`; Rust `common/utils/src/service_json.rs`, `function_proxy/src/instance_ctrl.rs`; proof `docs/analysis/131-service-metadata-validation-parity-proof.md` | Invalid metadata acceptance regression | Unit/integration verified | Closed | Keep regression tests; rerun full source-replacement ST in next release proof batch. |
| COMMON-002 | common release/YAML | `common/yaml_tool` / `libyaml_tool.so` package surface | Rust uses serde YAML and omits helper | C++ `common/yaml_tool`; release docs 119-120 | External package consumers may miss helper | No | P3 | Decide explicit compatible-superset policy vs restore shim. |
| COMMON-003 | common SSL | Resolve cert paths, validate files, set LiteBus SSL env vars | Closed for LiteBus env: Rust resolves cert files and applies `LITEBUS_SSL_*` during early component startup | C++ `common/utils/ssl_config.cpp`; Rust `common/utils/src/ssl_config.rs`; proof `docs/analysis/132-litebus-ssl-env-parity-proof.md` | Regression would silently accept no-op `ssl_enable` | Unit/smoke verified | Closed | Keep etcd/MetaStore TLS as a separate transport-parity item if secure etcd deployments enter release scope. |
| COMMON-004 | common NUMA | CPU/memory NUMA bind and verification | First-hop group bind metadata only | C++ `common/utils/numa_binding.cpp`; Rust `apply_group_bind_options` | NUMA locality silently unenforced | Partially, collective only | P1/P2 | If NUMA is in release scope, port binding/placement behavior and add placement tests. |
| COMMON-005 | common DS KV | `datasystem::KVClient` direct SDK semantics | HTTP `/kv/v1` adapter and sequential batch loops | C++ `common/kv_client/kv_client.cpp`; Rust `common/data_client/src/kv.rs` | DS auth/failure/large-value semantics differ | State path indirectly | P2 | A/B test DS KV semantics; keep if black-box sufficient, otherwise replace adapter. |
| COMMON-006 | common/config | C++ flags drive behavior | Inventory created: implemented/partial/no-op/policy flags are classified for next behavior tasks | Rust `common/utils/src/cli_compat.rs`; proof `docs/analysis/133-cpp-rust-flag-behavior-inventory.md` | Operator enables no-op flag | Inventory only | P2 | Use inventory to promote release-scope no-op flags into targeted implementation tasks. |
| PROXY-001 | function_proxy invoke | Copy `InvokeOptions.customTag` into `CallReq.createoptions` | Closed: Rust now copies custom tags into `CallReq.create_options` | C++ `busproxy/invocation_handler/invocation_handler.cpp`; Rust `busproxy/invocation_handler.rs`; proof `docs/analysis/130-proxy-invoke-customtag-parity-proof.md` | Runtime/proxy loses force invoke, route, billing, or custom hints | Unit verified | Closed | Keep regression test; rerun full source-replacement ST in next release proof. |
| PROXY-002 | function_proxy IAM | Create/invoke authorization via `InternalIAM` and policy | Create auth hook pass-through; invoke auth not found in inspected path | C++ `common/iam/**`, `request_dispatcher.cpp`; Rust `instance_ctrl.rs::schedule_do_authorize_create` | Unauthorized calls allowed in IAM mode | No | P1 | Define IAM release scope; if in scope, wire Rust IAM checks into create/invoke. |
| PROXY-003 | function_proxy overload | Invoke memory monitor and token bucket limiter | Create rate limiter exists; invoke/memory limiter not proven | C++ `InvocationHandler::Invoke`, `token_bucket_rate_limiter.cpp`; Rust `local_scheduler.rs` | Overload behavior diverges | No | P2/P1 | Add overload tests; implement invoke/memory admission if release scope. |
| PROXY-004 | function_proxy group/NUMA | Group bind drives placement/bin-pack/spread scheduling | First-hop extension keys only | C++ `local_group_ctrl_actor.cpp`, affinity paths; Rust `apply_group_bind_options` | Placement promises not enforced | Collective only | P1/P2 | Build A/B placement matrix; port scheduler behavior if required. |
| PROXY-005 | function_proxy group control | Range validation/defaults, group persistence, duplicate, sync/recover | ST-focused group create/range lower-bound fanout; full state machine not found | C++ `local_group_ctrl_actor.cpp`; Rust `handle_group_create` | Group restart/partial-failure behavior differs | Partial | P1 | Port or explicitly scope group state machine semantics; add range/recover tests. |
| PROXY-006 | function_proxy state | DS-backed state handler/client | MetaStore-backed persistent state store | Docs 117/121; C++ `common/state_handler/**` | Backend semantics differ under DS-specific conditions | Yes for current state ST | P2/P3 | Keep as release-policy boundary unless DS backend exactness is required. |
| PROXY-007 | function_proxy flags | Tracing, metrics, traefik, runtime direct connection, memory/OOM/user-log/NPU flags drive behavior | Inventory created; high-risk groups split toward IAM, overload, Traefik, runtime/DS, lifecycle, and scheduler tasks | Rust `function_proxy/src/config.rs`; proof `docs/analysis/133-cpp-rust-flag-behavior-inventory.md` | False confidence from accepted flags | Inventory only | P2 | Close high-risk groups via targeted tests, starting with IAM or invoke overload if release-scoped. |

| AGENT-001 | function_agent deployer | Local/copy/remote/S3/shared-dir/working-dir deployers and plugin integration | Local/copy/S3/shared-dir implemented; remote/working/plugin exactness unproven | C++ `function_agent/code_deployer/**`; Rust `function_agent/src/deployer.rs` | Deploy mode divergence | Partial | P2/P1 | Build deployer A/B matrix; upgrade required modes to implementation tasks. |
| AGENT-002 | function_agent plugin | Plugin and virtualenv manager actors | Lightweight venv hints; no full plugin manager found | C++ `function_agent/plugin/**`; Rust `runtime_manager/src/venv.rs` | Plugin deployments unsupported/partial | No | P1/P2 | Decide plugin scope; port only if release requires it. |
| RUNTIME-001 | runtime_manager debug | C++/Python debug server lifecycle | No equivalent debug server manager found | C++ `runtime_manager/debug/**`; Rust runtime_manager tree | Debug instances differ | No | P2 | Mark unsupported or implement debug server manager. |
| RUNTIME-002 | runtime_manager command/env | C++ command builder/env/volume process construction | Rust manually builds Python/C++ commands and env | C++ `runtime_manager/config/command_builder.*`; Rust `runtime_manager/src/executor.rs` | Runtime start edge cases | Main ST yes | P1 | Add A/B command/env snapshots for Python/C++ runtimes. |
| RUNTIME-003 | runtime_manager metrics | CPU/memory/disk/XPU/NUMA/resource label collectors | Rust metrics/resource reporting partial | C++ `runtime_manager/metrics/collector/**`; Rust `runtime_manager/src/metrics.rs` | Scheduler/resource policy divergence | Partial | P2/P1 | Compare reported resource JSON under representative hosts. |
| RUNTIME-004 | runtime_manager OOM/exit | OOM callbacks, exit classification, exception/std log extraction | Rust OOM/cgroup modules exist; exact semantics unproven | C++ `healthcheck_actor.cpp`, `metrics_actor.*`; Rust `runtime_manager/src/oom/**` | Reliability/reporting divergence | No | P1 | Add safe OOM/exit A/B probes. |
| MASTER-001 | function_master snapshot | Snapshot metadata HTTP, watch/sync, delete/list/restore | Rust snapshot code/tests exist; route parity unproven | C++ `function_master/snap_manager/**`; Rust `function_master/src/snapshot.rs` | Snapshot feature divergence | No | P2/P1 | Generate endpoint matrix and A/B snapshot flows. |
| MASTER-002 | function_master resource group | Persist/query/migrate resource groups and bundles | Partial resource-group handling/tests; full actor parity unproven | C++ `function_master/resource_group_manager/**`; Rust master/proxy resource group code | Resource-group production divergence | Partial | P1 | A/B create/query/migrate/sync scenarios. |
| MASTER-003 | function_master taint/migration/upgrade | Taint tolerance, migration, system upgrade watch | Flags accepted; behavior not fully traced | C++ `function_master/common/flags/flags.cpp`; Rust config/tests | No-op operational flags | No | P2 | Inventory and close behavior-critical flags. |
| MASTER-004 | function_master HTTP | Full C++ HTTP route/status/body/protobuf matrix | Rust implements key compatibility routes | C++ instance/resource/snapshot drivers; Rust `function_master/src/http.rs` | Management API drift | Partial | P1 | Generate route matrix and A/B responses. |
| SCHED-001 | domain_scheduler | Domain group control and underlayer scheduler manager | Rust scheduler engine/framework exists; full underlayer parity unproven | C++ `domain_scheduler/domain_group_control/**`, `underlayer_scheduler_manager/**`; Rust `domain_scheduler/src/**` | Multi-domain/group scheduling divergence | Partial | P1/P2 | A/B domain/group scheduling scenarios. |
| SCHED-002 | scheduler policy | Taints, group policy, migration, preemption/quota policies | Rust has priorities/preemption/resource view; policy breadth partial | C++ scheduler/resource-group paths; Rust `scheduler.rs`, `resource_view.rs` | Placement/policy divergence | Partial | P1/P2 | Build policy fixture matrix from C++ behavior. |
| META-001 | meta_store | Etcd-compatible KV/watch/lease/revision behavior | Rust implements etcd protos and services; edge compatibility unproven | C++/Go meta_service; Rust `meta_store/src/**` | Metastore correctness edge cases | Partial | P1 | Run KV/watch/lease/revision probe matrix. |
| META-002 | meta_store persistence | Backup/persistence flush/restart behavior | Rust backup/snapshot modules exist | C++ master/meta flags; Rust `backup.rs`, `snapshot_file.rs` | Data durability divergence | No | P1 | Crash/restart persistence test. |
| IAM-001 | iam_server routes | IAM HTTP route/status/header/body compatibility | Rust route/e2e tests exist; C++ A/B unproven | C++ `iam_server/**`; Rust `iam_server/src/routes.rs` | IAM client compatibility drift | No | P2/P1 | Generate C++ vs Rust IAM endpoint matrix. |
| IAM-002 | iam token format | C++ token implementation and proxy consumption | Rust HMAC `payload_b64.sig_hex` token | Rust `iam_server/src/token.rs` | Byte-format mismatch if shared externally | No | P2 | Treat as black-box policy unless external token compatibility required. |
| RELEASE-001 | release package | Byte-for-byte/minimal package inventory | Compatible superset, not byte-identical | Docs 119-120, `scripts/executor/tasks/pack_task.py` | External file-level consumers | ST yes | P3 | Keep explicit release policy; inventory each release. |
| RELEASE-002 | release helper lib | `libyaml_tool.so` present in C++ package | Omitted by Rust policy | Docs 119/128 | Missing loaded helper | No | P3/P1 | Restore shim only if consumer requires it. |
| RELEASE-003 | release metrics | Metrics package artifacts | Rust builder may skip missing metrics output | `pack_task.py::pack_metrics` | Missing metrics package | No | P2 | Decide metrics package release scope. |
| RELEASE-004 | release version | Product 0.8.0 vs internal artifact version | Internal functionsystem defaults to `0.0.0` | `make_functionsystem.py`, upper-layer pack docs | Version confusion | ST yes | P3 | Keep layered versioning doc; do not force byte version yet. |

## Next module batches to add

- Additional module rows have been added above. Keep this section for future third-pass refinements such as per-flag matrices and endpoint-by-endpoint generated evidence.

## Current execution guidance

Start implementation only after the relevant gap has a targeted test/probe. The most actionable first Rust fix is `PROXY-001` because C++ and Rust source evidence is direct and the behavior can be locked by a small unit test. The broadest release decision is `COMMON-006` / `PROXY-007`: accepted no-op flags must be documented so they do not masquerade as implemented C++ features.
