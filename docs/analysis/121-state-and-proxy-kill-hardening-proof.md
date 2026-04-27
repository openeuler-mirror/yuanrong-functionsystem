# State And Proxy-Kill Hardening Proof

Date: 2026-04-27
Branch: `rust-rewrite`
Container: `yr-e2e-master`
Proof root: `/workspace/proof_source_replace_0_8`

## Goal

Add post-R4 evidence around two failure-sensitive areas without changing non-Rust code:

1. State save/load and recover behavior in the official ST suite.
2. Proxy process kill/reconnect behavior using an existing disabled upstream ST case as a debug-only probe.

This document does not replace the formal acceptance lane in `docs/analysis/120-r4-layout-st-proof.md`; formal acceptance remains the single-shot 111/111 ST run.

## Constitution followed

```text
Only Rust yuanrong-functionsystem source may change.
Do not patch upper-layer yuanrong, runtime, datasystem, ST scripts, or clean C++ control.
Build/test parallelism must not exceed -j8.
Official acceptance is single-shot test.sh -b -l cpp.
Debug probes may use existing disabled upstream cases, but must not be counted as formal acceptance.
Do not guess on failures; inspect whether the failure is test setup, Rust, or control behavior.
```

## Official ST state evidence

The R4 formal ST proof includes the upstream state tests:

```text
Deploy: /tmp/deploy/27150146
GTest output: /tmp/deploy/27150146/driver/cpp_output.txt
Command log: /workspace/proof_source_replace_0_8/logs/source_replace_full_minus_invalid_group_r4_layout.log
```

Relevant passing cases:

```text
[ RUN      ] ActorTest.ActorSaveStateAndLoadStateSuccessfully
[       OK ] ActorTest.ActorSaveStateAndLoadStateSuccessfully (145 ms)
[ RUN      ] ActorTest.ActorGroupRecoverSuccessfully
[       OK ] ActorTest.ActorGroupRecoverSuccessfully (757 ms)
```

Meaning:

- `ActorSaveStateAndLoadStateSuccessfully` exercises runtime `YR::SaveState()` followed by `YR::LoadState()` through the Rust FunctionSystem package with `--state_storage_type=datasystem`.
- `ActorGroupRecoverSuccessfully` exercises checkpoint plus runtime process kill/recover for a group state path.
- These are formal ST evidence because they are part of the 111/111 accepted single-shot run.

Boundary:

- These cases do not kill and restart the Rust proxy process between save and load. Proxy-memory-loss behavior is covered by Rust regression tests in `docs/analysis/117-state-persistence-parity.md`, where a new `BusProxyCoordinator` with empty memory loads from a shared durable `StateStore`.

## Debug-only proxy kill probe

An upstream disabled C++ ST case already exists:

```text
/workspace/proof_source_replace_0_8/src/yuanrong/test/st/cpp/src/base/task_test.cpp
TaskTest.DISABLED_TestGrpcClientReconnect
```

The case starts an invoke, kills `function_proxy`, and expects `YR::Get` to return successfully rather than hang.

Because this case is disabled upstream, it was run only as a debug hardening probe, not as acceptance.

Command:

```bash
ROOT=/workspace/proof_source_replace_0_8
cd "$ROOT/src/yuanrong/test/st"
GTEST_ALSO_RUN_DISABLED_TESTS=1 \
  bash test.sh -b -r -l cpp -f "TaskTest.DISABLED_TestGrpcClientReconnect"
```

Evidence:

```text
Deploy: /tmp/deploy/27151238
Log: /workspace/proof_source_replace_0_8/logs/proxy_kill_debug_official_script.log
GTest output: /tmp/deploy/27151238/driver/cpp_output.txt
```

Result:

```text
[ RUN      ] TaskTest.DISABLED_TestGrpcClientReconnect
[       OK ] TaskTest.DISABLED_TestGrpcClientReconnect (10103 ms)
[  PASSED  ] 1 test.
----------------------Success to run cpp st----------------------
official_script_debug_rc=0
```

## Setup failure that was rejected

A first attempt used `test.sh -s -r` to start a reserved cluster and then ran `yrapicpp` manually. That failed before exercising proxy kill:

```text
ErrCode: 3003
runtime_manager_address is not configured and no local function_agent is registered
```

This was classified as an incomplete debug setup, not a Rust behavior failure. The successful rerun used the full `test.sh -b -r -l cpp` path so the official script performed compilation, package staging, deployment, environment export, and driver execution.

## Conclusion

Current evidence by layer:

```text
Formal source-replacement ST: 111/111 PASS after R4 layout changes.
Formal state ST cases: save/load and group recover PASS inside that 111/111 run.
Rust proxy-memory-loss regression: PASS in yr-proxy unit/integration tests with a new coordinator and shared durable store.
Debug-only proxy kill/reconnect probe: PASS using existing disabled upstream C++ ST case.
```

Remaining boundary:

- A single formal ST case that saves state, kills/restarts the proxy process, and then loads state does not exist in the upstream ST suite. Adding such a case would require new test code, so it should remain optional unless release owners require state-specific proxy-restart acceptance beyond the current black-box target.
