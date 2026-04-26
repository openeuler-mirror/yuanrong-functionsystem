# Rust source replacement scoped-order proof

Date: 2026-04-26
Branch: `rust-rewrite`
Container: `yr-e2e-master`
Proof root: `/workspace/proof_source_replace_0_8`

## Goal

Keep the upper-layer openYuanrong 0.8.0 build, packaging, install layout, and ST command unchanged while replacing the official `yuanrong-functionsystem` source tree with the Rust implementation.

The acceptance suite is the official single-shot C++ ST command with only the known 8 collective tests excluded:

```bash
FILTER="*-CollectiveTest.InvalidGroupNameTest:CollectiveTest.InitGroupInActorTest:CollectiveTest.CreateGroupInDriverTest:CollectiveTest.ReduceTest:CollectiveTest.SendRecvTest:CollectiveTest.AllGatherTest:CollectiveTest.BroadcastTest:CollectiveTest.ScatterTest"
bash test.sh -b -l cpp -f "$FILTER"
```

`test.sh -s -r` plus exported environment remains a debug-only flow and is not used as acceptance.

## Root cause fixed in this round

`ActorTest.OrderedInvocations_DesignatedNameOfNamedInstance` hung after the previous ordered-dispatch fix. The failing flow was:

1. Driver invokes named instance `yr_defalut_namespace-my_name` with `invocation_sequence_no=1`.
2. The named instance completes, advancing the old Rust proxy's per-target expected sequence to `2`.
3. A different actor then invokes the same named instance with its own `invocation_sequence_no=1`.
4. The old Rust proxy treated sequence as global per target instance and permanently waited for sequence `2`, so the actor-origin call never reached runtime.

The sequence number is scoped by caller/invoker plus target, not by target alone. The Rust proxy now tracks ordered invoke progress by caller-target scope.

This round also moved queue pop and in-flight recording out of the background send task. The old code cloned `DashMap` state into the async task and could also let an empty flush task race with later enqueue operations. The new flow decides readiness, pops the selected request, and records in-flight state synchronously before spawning only the channel send.

## Code changes

- `functionsystem/src/function_proxy/src/busproxy/request_dispatcher.rs`
  - Added `sequence_scope` to pending forwards.
  - Added `pop_ready` so one blocked scope does not block unrelated ready scopes.

- `functionsystem/src/function_proxy/src/busproxy/mod.rs`
  - Replaced per-target `instance_next_sequence` with `sequence_scope_next` keyed by caller-target scope.
  - Added request-to-scope tracking and scope cleanup on instance detach.
  - Made sequential flush select the first ready pending request for its own scope.
  - Records dispatched requests before spawning the async send.

- `functionsystem/src/function_proxy/src/busproxy/invocation_handler.rs`
  - Builds sequence scope from `caller_id + target_instance` for ordered invokes.
  - Stores the scope alongside the request sequence.
  - Keeps invokes queued while a runtime stream exists but init has not completed.

- `functionsystem/src/function_proxy/tests/invocation_handler_test.rs`
  - Added regression for two different callers both starting at sequence `1` against the same target.
  - Preserved duplicate-create-before-init regression coverage.

## Verification

Local Rust verification:

```bash
cargo fmt -p yr-proxy
cargo test -p yr-proxy --test invocation_handler_test -- --nocapture
cargo test -p yr-proxy --lib -- --nocapture
```

Results:

- `invocation_handler_test`: 28 passed / 0 failed
- `yr-proxy` lib tests: 7 passed / 0 failed

Source replacement build and package, with max build parallelism `-j8`:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong-functionsystem
./run.sh build -j 8
./run.sh pack
cd /workspace/proof_source_replace_0_8/src/yuanrong
bash scripts/package_yuanrong.sh -v v0.0.1
```

Result: exit 0. The Rust build installed the expected 7 binaries: `function_proxy`, `function_master`, `function_agent`, `domain_scheduler`, `runtime_manager`, `iam_server`, and `meta_store`.

Targeted ST verification:

- `ActorTest.OrderedInvocations_DesignatedNameOfNamedInstance`: passed
- `ActorTest.ActorOrderTest`: passed
- `TaskTest.RetryChecker`: passed

Final acceptance:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "$FILTER"
```

Result: `----------------------Success to run cpp st----------------------`

Final deploy path: `/tmp/deploy/26075526`
Final log: `/workspace/proof_source_replace_0_8/logs/source_replace_filtered_cpp_st_scoped_sequence.log`

## Current conclusion

For the current 0.8.0 source-replacement target, Rust `yuanrong-functionsystem` satisfies the black-box requirement against the official filtered C++ ST baseline: upper-layer build, packaging, installation layout, and ST command remain unchanged; only the functionsystem source implementation is replaced.
