# Proto Compatibility Restoration

Date: 2026-04-27
Branch: `rust-rewrite`
Scope: C++ 0.8 public wire/API fields that were missing from Rust proto files after the 111/112 ST proof.

## Why this was changed

A code-level audit showed that the missing fields were not all dead schema:

- `GroupOptions.bind` is consumed by C++ local group control and converted into `bind_resource` / `bind_strategy` scheduling extensions for NUMA affinity.
- `SignalResponse.payload` is used by C++ function proxy to transfer runtime signal payloads into `KillResponse.payload`.
- `EventRequest` / `EventPayload` / `StreamingMessage.eventReq` are exposed by the upper-layer runtime/fsclient event path.

The source-replacement ST suite does not cover these paths, but removing the fields from Rust-generated APIs is unsafe for black-box parity because Rust could not express or round-trip the C++ 0.8 wire contract.

## Restored schema

The following C++ 0.8 schema items were restored in Rust proto files with their original tag numbers:

```text
proto/posix/common.proto
  enum BindStrategy { BIND_None = 0; BIND_Spread = 1; BIND_Pack = 2; }
  message EventPayload { serverIp = 1; serverPort = 2; serverInstanceId = 3; }

proto/posix/core_service.proto
  message BindOptions { resource = 1; policy = 2; }
  GroupOptions bind = 6;
  message EventRequest { requestID = 1; message = 2; instanceID = 3; }

proto/posix/runtime_rpc.proto
  StreamingMessage eventReq = 38;

proto/posix/runtime_service.proto
  SignalResponse payload = 3;
```

## Tests added

`functionsystem/src/common/utils/tests/proto_builder_tests.rs` now includes:

```text
group_options_bind_roundtrip_matches_cpp_wire_contract
signal_response_payload_roundtrip_matches_cpp_wire_contract
streaming_message_event_req_roundtrip_matches_cpp_wire_contract
```

These tests prove the Rust generated types can construct, encode, decode, and preserve the restored fields.

## Verification

Fresh verification commands:

```bash
CARGO_BUILD_JOBS=8 cargo test -p yr-common --test proto_builder_tests -- --nocapture
CARGO_BUILD_JOBS=8 cargo test -p yr-proto --tests -- --nocapture
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test invocation_handler_test -- --nocapture
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test group_create_test -- --nocapture
CARGO_BUILD_JOBS=8 cargo check --workspace --lib --bins
```

Results:

```text
yr-common proto_builder_tests: 31 passed
yr-proto tests: 3 passed
yr-proxy invocation_handler_test: 28 passed
yr-proxy group_create_test: 5 passed
workspace libs/bins cargo check: passed
```

`cargo check --workspace --tests` was also attempted and still fails on pre-existing unrelated test issues:

```text
function_proxy/tests/e2e_exec_stream.rs: AppContext initializer missing ready/ready_flag
function_proxy/tests/config_extended_test.rs: stale InstanceController::start_instance call signature
function_proxy/tests/integration/master_proxy_flow.rs: axum_core 0.4/0.5 Body type mismatch
```

Those failures are not introduced by the proto restoration; targeted tests and lib/bin compilation pass.
The one test compile issue introduced by `GroupOptions.bind` was fixed in `group_create_test` with an explicit `bind: None`.

## Remaining behavior work

This change restores schema compatibility only. It does not prove full behavior parity for the restored fields.

Next targeted behavior checks:

1. Group bind/NUMA propagation: create group with `GroupOptions.bind.resource = "NUMA"` and verify Rust scheduling metadata carries equivalent `bind_resource` / `bind_strategy` semantics.
2. Signal payload forwarding: runtime returns `SignalResponse.payload`; verify driver/master receives the payload through `KillResponse.payload`.
3. Event stream handling: send/receive `StreamingMessage.eventReq` through the Rust proxy/runtime stream and compare to C++ control behavior.
