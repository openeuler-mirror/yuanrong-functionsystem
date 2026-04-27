# State Persistence Parity

Date: 2026-04-27
Branch: `rust-rewrite`
Scope: R3 from `docs/analysis/114-blackbox-risk-register.md`.

## Goal

Close the Rust-owned gap where `SaveReq` / `LoadReq` checkpoint state only lived in the Rust proxy process memory. That was not equivalent to the C++ 0.8 flow, where state can survive proxy process loss through the state client layer.

## C++ control behavior

Code inspected in clean C++ 0.8.0:

```text
/workspace/clean_0_8/src/yuanrong-functionsystem/functionsystem/src/function_proxy/common/state_handler/state_handler.cpp
/workspace/clean_0_8/src/yuanrong-functionsystem/functionsystem/src/function_proxy/common/state_handler/state_actor.cpp
/workspace/clean_0_8/src/yuanrong-functionsystem/functionsystem/src/function_proxy/common/state_handler/state_client.cpp
/workspace/clean_0_8/src/yuanrong-functionsystem/functionsystem/src/function_proxy/common/common_driver/common_driver.cpp
/workspace/clean_0_8/src/yuanrong-functionsystem/functionsystem/src/function_proxy/local_scheduler/instance_control/instance_ctrl_actor.cpp
```

Observed flow:

1. `StreamingMessage::kSaveReq` and `StreamingMessage::kLoadReq` are registered to `StateHandler`.
2. `StateHandler::SaveState(instanceId, request)` forwards to `StateActor::SaveState`.
3. `StateActor::SaveState` uses `instanceId` as `checkpointID` and calls `StateClient::Set(instanceId, state)`.
4. `StateActor::LoadState` calls `StateClient::Get(checkpointID, state)` and returns `StateLoadResponse.state`.
5. `StateClient` delegates to `DistributedCacheClient::Set/Get/Del`.
6. `CommonDriver::NeedBindDs()` binds state handling when `state_storage_type == datasystem` or DS streaming features are used.
7. Checkpoint/recover paths in `InstanceCtrlActor` use instance id as checkpoint id for runtime checkpoint state.
8. When an instance is deleted from the control view, C++ calls `StateHandler::DeleteState(instanceID)`.

Important black-box behavior: checkpoint id is the instance id, load must be able to recover state after proxy memory is gone, and terminal cleanup should not leave the checkpoint as live state forever.

## Rust behavior before this change

Rust handled `SaveReq` / `LoadReq` in:

```text
functionsystem/src/function_proxy/src/busproxy/invocation_handler.rs
functionsystem/src/function_proxy/src/busproxy/mod.rs
```

Before this change:

- `SaveReq` wrote `state` into `BusProxyCoordinator.state_snapshots`, a process-local `DashMap`.
- `LoadReq` read from the same in-memory map.
- Restarting/recreating the proxy lost all checkpoint state.
- Recover paths also used the in-memory map, so recovered runtimes received empty state if the proxy process had restarted.

## Rust implementation now

New durable abstraction:

```text
functionsystem/src/function_proxy/src/busproxy/state_store.rs
```

Key pieces:

- `StateStore` trait: `set_state`, `get_state`, `delete_state`.
- `MetaStoreStateStore`: backs the trait with the existing `yr_metastore_client::MetaStoreClient`.
- Keys are stored under logical prefix `/yr/state/{checkpoint_id}`.
- `BusProxyCoordinator` keeps the in-memory map as fast path and mirrors state into `StateStore` when `state_storage_type` is `datasystem`, `data_system`, `metastore`, or `meta_store`.
- `SaveReq`, `LoadReq`, recover flows, and terminal cleanup use the persistent helpers.

Why this is Rust-only and black-box safe:

- No upper-layer `yuanrong`, runtime, datasystem, ST, or C++ code changed.
- The externally visible checkpoint id remains the C++ value: instance id.
- If no persistent store is configured, behavior falls back to the previous in-memory path.
- If persistent store write/read fails while state persistence is enabled, Rust returns `ErrInnerSystemError`, matching the C++ failure style for `StateClient` failures.

Boundary:

- C++ names the backend `DistributedCacheClient` / datasystem. Rust currently uses the already-integrated MetaStore/etcd client as the durable backing store. This closes the black-box proxy-restart state loss, but it is not a byte-for-byte backend implementation of the C++ DS cache client internals.

## Regression proof

New test:

```text
functionsystem/src/function_proxy/tests/invocation_handler_test.rs::state_save_load_survives_new_bus_with_shared_store
functionsystem/src/function_proxy/tests/invocation_handler_test.rs::state_delete_removes_memory_and_persistent_checkpoint
```

What it proves:

1. Bus A handles `SaveReq` for `instance-1`.
2. Bus B is newly constructed with empty in-memory state, sharing only the durable `StateStore`.
3. Bus B handles `LoadReq(checkpointID=instance-1)`.
4. The saved bytes are returned successfully.
5. A separate deletion regression proves terminal cleanup can remove both the in-memory and persistent checkpoint entry.

Verification:

```bash
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test invocation_handler_test state_save_load_survives_new_bus_with_shared_store -- --nocapture
# 1 passed

CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test invocation_handler_test -- --nocapture
# 33 passed

CARGO_BUILD_JOBS=8 cargo check --workspace --lib --bins
# passed
```

## Status

R3 is closed for the Rust-owned black-box state-loss gap:

```text
SaveReq/LoadReq no longer depend only on process-local Rust proxy memory when persistence is enabled.
```

Remaining optional hardening:

- Add a full ST or integration test that kills/restarts the proxy process between runtime checkpoint and recover.
- If release owners require exact C++ DS cache backend equivalence rather than black-box durability, add a dedicated Rust DS client/FFI implementation and compare its keys/operations against C++ `DistributedCacheClient` logs.
