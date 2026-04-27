# Rust FunctionSystem Blackbox Risk Register

Date: 2026-04-27
Branch: `rust-rewrite`
Baseline: after final 111/112 source-replacement ST proof.

## Current headline

Rust `yuanrong-functionsystem` is complete for the proven 0.8 ST source-replacement lane, but broader black-box parity still has open risks. The risks below are sorted by release impact, not by implementation difficulty.

## Risk register

| ID | Risk | Status | Severity | Evidence | Recommended next action |
| --- | --- | --- | --- | --- | --- |
| R1 | Rust proto schema formerly omitted C++ 0.8 fields/messages (`BindStrategy`, `BindOptions`, `GroupOptions.bind`, `EventRequest`, `StreamingMessage.eventReq`, `SignalResponse.payload`) | Schema restored / Unit verified | Low for schema, Medium for behavior | direct proto diff plus round-trip tests on 2026-04-27 | Keep schema restored; add behavior tests for group bind/NUMA, signal payload forwarding, and event stream handling |
| R2 | CLI/config flag parity is not fully proven at binary level | Needs test | High | source regex shows broad C++ `AddFlag` surface and broad Rust clap surface, but not exact accepted binary flags | Build both packages and compare `--help`/startup accepted flags for every binary under a scripted gate |
| R3 | DS-backed state persistence may not be equivalent to C++ state handler | Needs test / possible implementation | High | Rust `SaveReq`/`LoadReq` uses in-memory snapshots; C++ has state handler/client subsystem | Add state persistence ST or integration test covering restart/recover/cross-proxy checkpoint load |
| R4 | Package is not byte-for-byte/minimal equivalent | Needs release decision | Medium | C++ package has 160 entries, Rust 183; missing `libcrypto.so.3`, `libssl.so.3`, `libyaml_tool.so`; extra `meta_store` and libs | Decide policy: compatible superset is allowed, or tighten packaging to match C++ minimal layout |
| R5 | `SignalResponse.payload` behavior is not ST-covered | Needs behavior test | Medium | schema restored and round-trip tested; C++ proxy maps `signalRsp.payload()` into `KillResponse.payload` | Add custom signal payload forwarding test and implement forwarding if the Rust proxy path drops it |
| R6 | Stream event data path (`eventReq`) schema exists but behavior is not ST-covered | Needs behavior test | Medium | schema restored and round-trip tested; upper-layer runtime/fsclient has event request paths | Add event stream behavior test or explicitly mark event handling out-of-scope |
| R7 | Group bind/resource policy schema exists but Rust scheduling propagation is not proven | Needs behavior test | Medium | schema restored and round-trip tested; C++ maps bind options to `bind_resource`/`bind_strategy` NUMA extensions | Add group bind/NUMA scheduling propagation test and implement equivalent propagation if missing |
| R8 | IAM behavior is mostly unit-level, not ST-level | Needs test | Medium | Rust IAM tests exist; current cpp ST does not strongly exercise IAM server | Add IAM route/token e2e in source-replacement package or mark IAM out of current delivery scope |
| R9 | Function agent deployer/plugin breadth not fully covered by ST | Needs test | Medium | C++ agent supports multiple deployers/plugins; Rust tests cover config/registration more than every deployer backend | Add local/copy/S3/plugin matrix tests or document unsupported deployer modes |
| R10 | Advanced scheduler/resource behavior under scale/failure not covered by ST | Needs test | Medium | ST is small relative to scheduler topology, taint, upgrade, migration, pool features | Add targeted scheduler HTTP/resource/eviction tests with C++ control comparison |
| R11 | Multi-proxy ordering/recovery under partitions not exhaustively covered | Needs test | Medium | current ST/unit tests cover normal ordering and some multi-proxy routing | Add stress/fault matrix: two proxies, reconnect, duplicate sequences, partial runtime crash |
| R12 | Remaining `CollectiveTest.InvalidGroupNameTest` is not closed by Rust proof | Control-failing | Low for Rust ownership | clean C++ control fails same test under same `-G` profile | Keep excluded until a clean C++ control passes; then rerun Rust and assign ownership based on evidence |

## Green areas with strong evidence

| Area | Classification | Evidence |
| --- | --- | --- |
| Source replacement handoff command flow | ST verified | `docs/analysis/110-source-replacement-final-111-proof.md` |
| Non-collective cpp ST | ST verified | 104/104 filtered proof |
| Collective runtime profile except duplicate invalid group | ST verified | 7/8 after `bash build.sh -P -G -j 8` |
| Create/init/ready ordering | ST + unit verified | final ST plus duplicate-create regression tests |
| Invoke/result routing | ST + unit verified | final ST plus `invocation_handler_test.rs` |
| Actor caller-scoped ordering | ST + unit verified | `docs/analysis/106-source-replacement-scoped-order-proof.md` |
| Named instance basics | ST verified | final 111 proof and lifecycle sprint evidence |
| Etcd key constants/generators | Unit verified | `etcd_keys.rs` tests |
| Instance state enum/transition basics | Unit verified | `instance_state_tests.rs` |

## Immediate next hardening tasks

These tasks are safe, Rust-owned, and do not require changing non-Rust code.

1. Proto parity hardening:
   - Done for schema: restored C++ 0.8 fields/messages in Rust proto files with the same tag numbers.
   - Done for schema: added round-trip tests for `GroupOptions.bind`, `StreamingMessage.eventReq`, and `SignalResponse.payload`.
   - Next: add behavior tests for group bind/NUMA propagation, custom signal payload forwarding, and event stream handling.

2. Binary flag parity gate:
   - Package clean C++ and Rust functionsystem.
   - For each binary, capture `--help` output and startup parse behavior.
   - Classify every C++ flag as supported, ignored-compatible, intentionally unsupported, or missing.

3. State persistence parity gate:
   - Add focused Rust integration test for save -> process/proxy restart -> load.
   - If current in-memory implementation fails, implement DS/metastore-backed state compatible with C++ storage mode.

4. Release layout decision:
   - Decide whether Rust package may be a compatible superset.
   - If not, remove extra release files and/or add compatibility symlinks/libs required by consumers.

## Stop conditions for this audit

Do not claim broader production black-box parity until R1, R2, and R3 are resolved or explicitly accepted as out-of-scope by release owners. The 111 ST proof remains valid, but it is narrower than full black-box equivalence.
