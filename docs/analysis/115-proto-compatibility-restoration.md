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
yr-proxy invocation_handler_test: 32 passed
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

## Behavior hardening after schema restoration

After restoring the wire fields, the proxy behavior was hardened against the C++ 0.8 paths that consume them:

1. User signal payload bridge:
   - C++ sends user-defined signals as `runtime_service.SignalRequest` and maps `SignalResponse.payload` back into `KillResponse.payload`.
   - Rust now forwards local user signals as `StreamingMessage.signalReq`, tracks the generated signal message id, and bridges the later `SignalRsp` back to the caller as `KillRsp` with the payload preserved.
   - Unit test: `user_signal_forwards_signal_req_and_returns_signal_payload`.

2. Event request forwarding:
   - C++ upper-layer runtime normally writes event messages directly between runtime streams.
   - Rust now handles `StreamingMessage.eventReq` if it reaches the proxy stream and forwards it to `EventRequest.instanceID` without silently dropping the message.
   - Unit test: `event_req_forwards_to_target_runtime_stream`.

3. Group bind first-hop metadata propagation:
   - C++ local group control maps `GroupOptions.bind` into scheduling extension keys `bind_resource` and `bind_strategy`.
   - Rust now applies the same first-hop metadata to each group create request before scheduling. Unknown policies default to `BIND_None`, matching the C++ map fallback.
   - Unit tests: `group_bind_options_are_mapped_to_scheduling_extension`, `group_bind_unknown_policy_defaults_to_cpp_bind_none_string`.

Important boundary: this is not a claim of full NUMA scheduler plugin parity. It proves the C++-compatible bind metadata is propagated into Rust scheduling options; a separate scheduler/filter/scorer test is still needed if NUMA placement itself becomes a release gate.

## Remaining behavior work

Schema compatibility and the first proxy behavior layer are now unit-verified for the restored fields. The remaining non-ST coverage gaps are broader integration semantics:

1. Full NUMA placement: verify that `bind_resource = NUMA` and `bind_strategy` affect placement the same way as the C++ NUMA filter/scorer path, or explicitly mark NUMA placement out of scope.
2. Runtime direct event path: compare upper-layer runtime/fsclient event behavior end-to-end against clean C++ control; proxy forwarding is covered, but the normal direct runtime path is outside this proxy unit test.
3. Full ST rerun: optional for this hardening commit because the 111/112 proof already passed before these non-ST paths were hardened.
