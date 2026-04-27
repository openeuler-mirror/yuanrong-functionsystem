# C++/Rust Interface Surface Matrix

Date: 2026-04-27
Branch: `rust-rewrite`
Audit input:

- C++ control source: `/workspace/clean_0_8/src/yuanrong-functionsystem`
- Rust source: `/home/lzc/workspace/code/yr_rust/yuanrong-functionsystem`
- Rust proof package: `/workspace/proof_source_replace_0_8/src/yuanrong-functionsystem/output/yr-functionsystem-v0.0.0.tar.gz`

## Summary

Rust is compatible with the source-replacement ST handoff surface, but not yet proven byte-for-byte or surface-complete against every C++ 0.8 contract. The most important non-ST risks are protobuf schema drift and broad CLI flag/config parity.

## Artifact and installed layout

| Surface | C++ control | Rust replacement | Status | Evidence |
| --- | --- | --- | --- | --- |
| Functionsystem tar name | `yr-functionsystem-v0.0.0.tar.gz` | same | Equivalent / ST verified | `docs/analysis/109-release-artifact-audit.md` |
| Functionsystem wheel name | `openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl` | same | Equivalent / ST verified | `docs/analysis/109-release-artifact-audit.md` |
| Aggregate openYuanrong tar consumption | upper layer consumes same tar name | same | ST verified | `docs/analysis/110-source-replacement-final-111-proof.md` |
| Core binary names | `domain_scheduler`, `function_agent`, `function_master`, `function_proxy`, `iam_server`, `meta_service`, `runtime_manager`, `yr` | all present | Equivalent | package list audit |
| Extra Rust binary | none | `meta_store` | Needs release decision | Rust embeds a metastore server binary; no ST failure observed |
| C++ libs missing in Rust package | only `libyaml_tool.so` remains after R4 layout closure | absent by policy | Release-policy boundary | Rust parses service YAML with `serde_yaml`; do not add copied C++ helper unless byte-for-byte/minimal package parity is required |
| Extra Rust libs | none | grpc/protobuf/datasystem/xml/pcre/iconv libs plus `libdatasystem_worker.so` | Needs release decision | harmless for ST, may affect minimality/security/license audit |

Package list comparison captured on 2026-04-27:

```text
C++ entries:       160
Rust entries:      183
C++ minus Rust:    4
Rust minus C++:    27
```

## Binary map

| Component | C++ source role | Rust source role | Status |
| --- | --- | --- | --- |
| `function_proxy` | local scheduler, bus proxy, POSIX stream, runtime route, instance control | `functionsystem/src/function_proxy` | ST verified for create/init/invoke/result/kill/recover/order/collective-forward paths |
| `runtime_manager` | runtime process start/stop, ports, env, logs, DS env | `functionsystem/src/runtime_manager` | ST verified for Python/C++ runtime launch paths used by cpp ST; needs broader config parity audit |
| `function_agent` | code package deployer and runtime manager bridge | `functionsystem/src/function_agent` | ST verified through source-replacement lane; needs plugin/deployer breadth audit |
| `function_master` | global scheduling, instance manager HTTP, named instance query | `functionsystem/src/function_master` | ST verified for named instance and scheduling basics; needs HTTP API parity audit |
| `domain_scheduler` | domain scheduler service and resource topology | `functionsystem/src/domain_scheduler` | lightly ST verified; needs scale/failure-mode audit |
| `iam_server` | IAM token/credential HTTP service | `functionsystem/src/iam_server` | mostly unit verified; current cpp ST does not strongly cover IAM |
| `meta_service` / `meta_store` | C++ package has `meta_service`; Rust package has compatibility `meta_service` and extra `meta_store` | `functionsystem/src/meta_store` | Needs release decision for extra binary and operational mode support |
| `yr` CLI | functionsystem CLI/app surface | Rust pack emits `yr` | Needs CLI parity audit beyond ST |

## Protobuf and gRPC wire surface

Direct proto comparison found several files identical, several whitespace-only changes, and several field/message removals in Rust.

Identical files:

```text
proto/posix/bus_adapter.proto
proto/posix/bus_service.proto
proto/posix/exec_service.proto
proto/posix/log_service.proto
proto/posix/message.proto
proto/posix/resource.proto
proto/posix/runtime_launcher_interface.proto
```

Rust-only internal protos:

```text
proto/inner/metastore.proto
proto/inner/scheduler.proto
```

C++-only internal protos:

```text
proto/inner/core_service.proto
proto/inner/runtime_service.proto
```

Observed wire-schema deltas after proto restoration:

| Proto | Former gap | Rust status after 2026-04-27 hardening | Remaining risk |
| --- | --- | --- | --- |
| `posix/common.proto` | `BindStrategy` enum | restored and round-trip tested | first-hop group scheduling extension mapping is unit-verified; full NUMA placement still needs a focused test if required |
| `posix/common.proto` | `EventPayload` message | restored | event behavior still needs a focused runtime/fsclient path test |
| `posix/core_service.proto` | `BindOptions` message | restored and round-trip tested | first-hop scheduling extension mapping is unit-verified; full NUMA placement still open |
| `posix/core_service.proto` | `GroupOptions.bind = 6` | restored and round-trip tested | same as above |
| `posix/core_service.proto` | `EventRequest` message | restored and round-trip tested via `StreamingMessage.eventReq` | proxy forwarding is unit-verified; end-to-end runtime/fsclient direct event path remains non-ST-covered |
| `posix/runtime_rpc.proto` | `StreamingMessage.eventReq = 38` | restored and round-trip tested | proxy forwarding is unit-verified; runtime direct event handling remains non-ST-covered |
| `posix/runtime_service.proto` | `SignalResponse.payload = 3` | restored and round-trip tested | local proxy `SignalRsp.payload` -> `KillRsp.payload` bridge is unit-verified |

Direct proto comparison now shows `posix/core_service.proto` is byte-identical to the C++ control. The remaining diffs in `common.proto`, `runtime_rpc.proto`, and `runtime_service.proto` are newline/formatting only for the restored fields, while `affinity.proto` and `inner_service.proto` retain pre-existing whitespace-only diffs. Internal proto layout still differs intentionally (`inner/metastore.proto` and `inner/scheduler.proto` are Rust-only; `inner/core_service.proto`, `inner/runtime_service.proto`, and `posix/agent_plugin.proto` remain C++-only).

## Runtime stream message handling

| Message variant | C++ evidence | Rust evidence | Status |
| --- | --- | --- | --- |
| `CreateReq` | local scheduler / instance control creates instance and sends init call | `invocation_handler.rs::handle_create_req` | ST + unit verified |
| `CreateReqs` group create | C++ group scheduling and local group control | `invocation_handler.rs::handle_create_reqs` | ST verified for 7/8 collective under `-G`; duplicate invalid-group is control-failing |
| `InvokeReq` -> `CallReq` | C++ `InvokeRequestToCallRequest` copies function/args/request/trace/sender | Rust `invoke_to_call` copies function/args/request/trace/sender/returns | ST + unit verified |
| `CallResultReq` | C++ routes initcall specially, normal results to instance proxy | Rust `on_runtime_call_result` handles init result, normal routing, ack | ST + unit verified |
| `NotifyReq`/`NotifyRsp` | C++ translates notify/call-result ack paths | Rust ack/routing tables in `BusProxyCoordinator` | ST verified |
| `KillReq` | C++ signal route, local/remote forward, shutdown semantics | Rust local kill, group kill, forward user signal | ST + unit verified for common signals |
| `ExitReq` | C++ converts exit into kill/cleanup side effects | Rust `handle_exit_req` and `apply_exit_event` | ST + unit verified |
| `SaveReq`/`LoadReq` | C++ state handler / DistributedCacheClient state path, checkpoint id = instance id | Rust keeps memory fast path and mirrors to persistent `StateStore` when enabled | Regression verifies load from a new BusProxyCoordinator with empty memory; see `docs/analysis/117-state-persistence-parity.md` |
| `RecoverReq`/`RecoverRsp` | C++ control plane recover through posix client | Rust `forward_recover`, runtime reconnect recover | ST + unit verified for covered recovery paths |
| `eventReq` | C++ proto exposes stream event data and upper-layer runtime uses direct event writes | Rust proto exposes variant and proxy forwards it to `EventRequest.instanceID` if seen | Schema + proxy behavior unit-verified; runtime direct event path still needs end-to-end coverage if required |

## Etcd/metastore key surface

Rust has a dedicated compatibility module:

```text
functionsystem/src/common/utils/src/etcd_keys.rs
```

It maps key constants from C++ files such as `metastore_keys.h`, `meta_store_kv_operation.h`, and explorer/election constants.

| Key family | C++ source | Rust source | Status |
| --- | --- | --- | --- |
| scheduler topology | `SCHEDULER_TOPOLOGY = /scheduler/topology` | same constant | Unit verified |
| group schedule | `/yr/group` | same constant | Unit verified / ST exercised by collective |
| ready agent count | `/yr/readyAgentCount` | same constant | Unit verified |
| instance info | `/sn/instance/business/yrk/tenant/...` | `gen_instance_key` | Unit verified |
| route info | `/yr/route/business/yrk/{instance}` | `gen_instance_route_key` | Unit verified |
| bus proxy node | `/yr/busproxy/business/yrk/tenant/{tenant}/node/{node}` | `gen_busproxy_node_key` | Unit verified |
| function meta | `/yr/functions/business/yrk/tenant/...` | `gen_func_meta_key` | Unit verified |
| IAM token / AKSK | `/yr/iam/token`, `/yr/iam/aksk` | same constants/generators | Unit verified |
| metastore table prefix | C++ concatenates prefix + logical key | Rust `with_prefix` does concatenation | Unit verified |

## CLI/config surface

C++ exposes a broad flag surface through `AddFlag(...)` in each component. Rust uses `clap`, plus a centralized compatibility adapter in `functionsystem/src/common/utils/src/cli_compat.rs`.

Current audit finding after `docs/analysis/116-binary-flag-parity-gate.md`:

- Every C++ help-only flag missing from Rust help is accepted by the Rust binary under a black-box startup probe: 297/297 accepted, 0 rejected.
- Every official deployment-layer flag extracted from process install scripts and TOML/Jinja launch templates is accepted: function_proxy 173/173, function_agent 106/106, function_master 75/75, runtime_manager 69/69, iam_server 46/46.
- Rust still does not advertise every C++ legacy flag in `--help`; hidden accepted flags are intentionally accepted/ignored for startup compatibility.

Behavioral boundaries that are not closed by launch-parser acceptance:

```text
function_proxy: optional rate-limit, tenant isolation, OIDC/workload identity, and advanced auth knobs
runtime_manager: optional tracing/metrics/log reuse/direct-connection/massif modes
function_agent: optional SCC/signature-validation/metrics SSL/deployer mode knobs
function_master: optional upgrade/fake suspend-resume/etcd decrypt knobs
iam_server: credential provider and token TTL are parser-compatible; broader IAM e2e remains a separate risk
```

## Current interface conclusion

Rust satisfies the proven ST replacement interface, but the audit identifies non-ST deltas that should be handled before calling the project a broad production black-box replacement:

1. Restored proto fields now have schema tests and first-hop proxy behavior tests, but full NUMA placement and runtime direct event integration remain broader non-ST risks.
2. CLI flag parity needs binary-level `--help` comparison, not just source inspection.
3. Package extra/missing libraries need a release decision: acceptable superset vs minimal byte-for-byte layout.
