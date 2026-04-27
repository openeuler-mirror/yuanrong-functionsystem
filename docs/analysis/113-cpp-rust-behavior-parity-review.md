# C++/Rust Behavior Parity Review

Date: 2026-04-27
Branch: `rust-rewrite`
Purpose: review core behavior paths after the 111/112 source-replacement ST proof.

## Read this first

This is not a claim that Rust is byte-for-byte equivalent to C++. It is a path-by-path review of black-box behavior with evidence strength.

Evidence strength order:

1. Official source-replacement ST passed.
2. Clean C++ control behaves the same way.
3. Rust unit/integration tests lock the contract.
4. Source comparison supports the behavior.
5. Inference only; needs test.

## Create -> init -> ready notification

C++ behavior:

- `CreateReq` schedules/starts an instance through local scheduler and function agent.
- Runtime connects back to the proxy stream.
- Proxy sends an `isCreate=true` `CallReq` to initialize the runtime instance.
- Driver receives ready notification only after initialization is complete.

Rust behavior:

- `functionsystem/src/function_proxy/src/busproxy/invocation_handler.rs` handles `CreateReq`.
- `BusProxyCoordinator::send_init_sequence` sends `isCreate=true` `CallReq`.
- Duplicate create during pending init returns idempotent `CreateRsp` and does not emit early `NotifyReq`.

Evidence:

- `docs/analysis/110-source-replacement-final-111-proof.md` proves ST path.
- `functionsystem/src/function_proxy/tests/invocation_handler_test.rs` includes duplicate-create and init-order regressions.

Status: `ST verified` / `Unit verified`.

Residual risk: init ordering is covered for current runtime flows, but other language runtimes not present in the proof lane should be retested when their packages are available.

## Invoke -> call -> result -> ack

C++ behavior:

- `InvocationHandler::InvokeRequestToCallRequest` maps `InvokeReq` fields to `CallReq`, including function, args, request id, trace id, return object ids, sender id, and create options.
- `CallResultReq` routes result to the waiting caller and returns an ack.

Rust behavior:

- `InvocationHandler::invoke_to_call` copies function, args, trace id, request id, sender id, return ids, and span id.
- `BusProxyCoordinator` preserves request-to-caller routing and handles `CallResultAck`/`NotifyRsp` translations.

Evidence:

- Final 111 ST includes task, actor, named instance, collective, and recovery invoke paths.
- Unit tests cover field copy, non-empty message id preservation, named runtime stream routing, and result handling.

Status: `ST verified` / `Unit verified`.

Residual risk: `createoptions().customtag()` parity should remain watched because C++ places invoke custom tags into `CallReq.createoptions`; Rust currently uses direct generated fields and metadata decoding for sequencing/function targeting.

## Actor ordering and caller scope

C++ behavior:

- Ordered invokes are runtime/actor semantic contracts. Earlier debugging showed target-global ordering is wrong because different callers may each start at sequence 1.

Rust behavior:

- `RequestDispatcher` keeps queued calls sorted within an optional `sequence_scope`.
- `BusProxyCoordinator` tracks next expected sequence by caller-target scope, not by target instance globally.

Evidence:

- `docs/analysis/106-source-replacement-scoped-order-proof.md`.
- Final 111 ST passes `ActorTest.ActorOrderTest` and related ordered named-instance paths.
- Unit test: `ordered_invokes_from_distinct_callers_start_at_sequence_one`.

Status: `ST verified` / `Unit verified`.

Residual risk: high-concurrency ordering under multi-proxy network partitions is not fully covered by ST.

## Named instance and query paths

C++ behavior:

- Function master exposes instance manager HTTP routes, including named instance query through `InstancesApiRouter` and `QueryNamedInsHandler`.

Rust behavior:

- `function_master/src/http.rs` includes `query_named_instances` compatibility logic.
- Instance tracking mirrors metastore events into in-memory indexed JSON for HTTP queries.

Evidence:

- Earlier lifecycle sprint fixed GetInstance/named instance behavior.
- Final ST includes named instance coverage.

Status: `ST verified` for current named instance cases.

Residual risk: query shape compatibility for every HTTP `Type` header and protobuf/JSON variant should be broadened with targeted HTTP API tests.

## Runtime recovery and stale cleanup

C++ behavior:

- Runtime reconnect/recover paths rebuild runtime state, replay needed calls, and clear terminal state through instance control.

Rust behavior:

- `BusProxyCoordinator::forward_recover` and runtime reconnect handling send `RecoverReq`, replay state snapshots, and gate pending invokes until recovery completes.
- Cleanup scripts are used for ST harness state, but Rust proxy also handles runtime stream close and terminal instance GC.

Evidence:

- Final ST source replacement passed recovery-related cases.
- Prior docs proved stale environment cleanup eliminated sequential harness timeouts.

Status: `ST verified` for current recovery cases.

Residual risk: recovery after multi-proxy partial failure and DS/metastore reconnect storms remains broader than ST.

## Kill, exit, signal, shutdown

C++ behavior:

- `InstanceCtrlActor::Kill` routes signals locally or remotely, handles shutdown, group/family exit, checkpoint/suspend/resume, notification, and user signals.
- `runtime_service.SignalResponse` in C++ proto includes a `payload` field.

Rust behavior:

- Rust handles local kill, group kill, notify/subscription ack, checkpoint/suspend/resume ack, user signal forwarding, and exit side effects.
- Rust proto currently lacks `SignalResponse.payload`.

Evidence:

- ST covers graceful shutdown and kill/terminate paths that are in the 111 green set.
- Unit tests cover local/explicit kill and exit normal/error behavior.

Status: `ST verified` for covered signals; `Needs test` for payload-carrying custom signals.

Risk: if an upstream caller depends on `SignalResponse.payload`, Rust-generated code cannot currently express it and may drop it.

## Save/load state

C++ behavior:

- State handling is a first-class subsystem with state handler/client and DS-backed storage modes.

Rust behavior:

- `SaveReq`/`LoadReq` are implemented in proxy as in-memory snapshot storage keyed by checkpoint id / instance id.

Evidence:

- ST no longer spins on unhandled save/load variants.
- Stateful order paths are covered in the 111 proof only to the extent current ST uses them.

Status: `Partially ST verified`; broader DS-backed persistence is `Needs test` and may be `Needs implementation` for production parity.

Risk: process restart or cross-proxy recovery may not preserve state if only in-memory storage is used.

## Collective group path

C++ behavior:

- Collective cases require upper-layer runtime built with `-G` Gloo support.
- Clean C++ control under the same `-G` runtime fails `CollectiveTest.InvalidGroupNameTest` duplicate-group expectation.

Rust behavior:

- With official `bash build.sh -P -G -j 8`, Rust passes 7/8 collective cases.
- The single remaining duplicate-group case fails the same way in clean C++ control.

Evidence:

- `docs/analysis/108-collective-st-expansion-investigation.md`.
- `docs/analysis/110-source-replacement-final-111-proof.md`.

Status: 7 cases `ST verified`; duplicate invalid-group case `Control-failing`.

Residual risk: if a stricter clean C++ profile later passes invalid-group, Rust must be retested before ownership stays non-Rust.

## Protobuf schema drift behavior

C++ behavior:

- C++ 0.8 protos expose group bind policy, event request/payload, and signal response payload fields.

Rust behavior:

- Rust protos now restore those C++ 0.8 wire fields/messages and include round-trip tests for `GroupOptions.bind`, `SignalResponse.payload`, and `StreamingMessage.eventReq`.

Evidence:

- Direct diff between `/workspace/clean_0_8/src/yuanrong-functionsystem/proto` and Rust `proto` after restoration.
- `functionsystem/src/common/utils/tests/proto_builder_tests.rs` has the compatibility round-trip tests.

Status: schema compatibility is `Unit verified`; behavior handling remains `Needs test` for group bind/NUMA propagation, custom signal payload forwarding, and event stream handling.

Risk: schema restoration prevents Rust generated code from making these fields impossible to express, but it does not by itself prove the Rust services implement every C++ behavior path using the fields.

## Behavior conclusion

The Rust implementation is strong for the behavior paths exercised by the official ST and by focused Rust regression tests. The remaining black-box uncertainty is concentrated in contracts that ST does not exercise:

1. Behavior use of restored proto fields/messages: group bind/NUMA, signal payload, and event stream.
2. Full CLI/config flag parity.
3. DS-backed state persistence beyond in-memory snapshot handling.
4. IAM, plugin/deployer, traefik, tenant-isolation, and advanced resource/bind policy paths.
