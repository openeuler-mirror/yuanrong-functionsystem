# Scheduler Policy Proof

Date: 2026-05-03
Branch: `rust-rewrite`
Scope: bounded scheduler-policy closure for selector operator parity + failure-domain filtering

## Closed slice

This proof closes only the following scheduler-policy gaps:

1. Rust's current JSON selector path now supports the missing `NotExist` operator, using the C++ label-affinity expression operator set as the behavioral reference for supported operators.
2. Rust scheduling now has explicit proof that `failure_domain` constraints are honored through the current `labels.zone` fallback path from worker resource JSON when no top-level `failure_domain` / `zone` is provided.

This proof does **not** claim full C++ scheduler parity. Weighted affinity/anti-affinity, taints/tolerations, migration, preemption breadth, underlayer scheduler manager parity, domain-group control parity, the other top-level `failure_domain` / `zone` parsing branches, and any claim that the Rust JSON selector shape literally matches C++ `ResourceSelectorFilter` remain explicit release-scope boundaries.

## C++ references used before implementation

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/scheduler_framework/utils/label_affinity_utils.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/domain_scheduler/domain_group_control/domain_group_ctrl.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/domain_scheduler/underlayer_scheduler_manager/underlayer_sched_mgr.cpp
```

## Rust changes

Code:

- `functionsystem/src/domain_scheduler/src/scheduler_framework/policy.rs`
  - add `SelectorExpr::NotExist`
  - parse JSON selector operator `NotExist`
  - reject nodes carrying the forbidden label when `NotExist` is requested

Tests:

- `functionsystem/src/domain_scheduler/tests/scheduling_test.rs`
  - `scheduler_framework_resource_selector_match_expression_not_exist`
  - `scheduling_engine_failure_domains_match_zone_fallback_from_resource_json`

Audit:

- `docs/analysis/165-scheduler-policy-parity-matrix.md`

## Host preflight

Executed from:

```text
/home/lzc/workspace/code/yr_rust/yuanrong-functionsystem/functionsystem
```

Commands:

```bash
cargo test -p yr-domain-scheduler -- --nocapture
cargo check --workspace --lib --bins
git diff --check
```

Key result:

- `yr-domain-scheduler` tests: `11 passed; 0 failed`
- `service_contract_test`: `12 passed; 0 failed`
- `resource_view_contract_test`: `3 passed; 0 failed`
- `git diff --check`: clean

## Container acceptance

Container:

```text
yr-e2e-master
```

Synced files into:

```text
/workspace/rust_current_fs/functionsystem/src/domain_scheduler/src/scheduler_framework/policy.rs
/workspace/rust_current_fs/functionsystem/src/domain_scheduler/tests/scheduling_test.rs
```

Commands:

```bash
cd /workspace/rust_current_fs/functionsystem
export CARGO_BUILD_JOBS=8
cargo test -p yr-domain-scheduler -- --nocapture

cd /workspace/rust_current_fs
export CARGO_BUILD_JOBS=8
./run.sh build -j 8
./run.sh pack
```

Key result:

- container `yr-domain-scheduler` tests: `11 passed; 0 failed`
- container build: success
- container pack: success

Traceability note:

- after review, the failure-domain proof test was tightened again to prove the `labels.zone` fallback path more strictly;
- that final tightening changed only tests/docs, not packaged production code;
- container `cargo test -p yr-domain-scheduler -- --nocapture` was rerun on the final tree after the tightening,
- while the build/pack hashes and proof-lane ST evidence below remain valid for the unchanged production artifacts.

FunctionSystem artifact hashes:

```text
38c3be2c175b508aaec73af4c4d86d7571494cd14e929c439c50a214cb2b425a  output/yr-functionsystem-v0.0.0.tar.gz
b61a6c8c2e4234144c0410085d3ddf47e00260479df39506d89037c509f925d1  output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
de824953efcdb0079fab2eb9e40711f5efb46e1e09cc14f907bf2de59d278607  output/metrics.tar.gz
```

## Proof-lane replacement and unchanged upper-layer repack

Only the three FunctionSystem artifacts were replaced in:

```text
/workspace/proof_source_replace_0_8/src/yuanrong/output
```

Repack command:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong
bash scripts/package_yuanrong.sh -v v0.0.1
```

Repacked artifact hashes:

```text
ed194b3e66a49abbcfb464b0d84d9c6cf7601d4ec80f8657ccde47062dd14a39  output/openyuanrong-v0.0.1.tar.gz
3a63f3471cc006f81612d0298d9023ff9406308995866fa2f8b0872eb05e9683  output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

## Single-shot cpp ST acceptance

Command:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
```

Accepted deploy path:

```text
/tmp/deploy/03140532
```

Evidence file:

```text
/tmp/deploy/03140532/driver/cpp_output.txt
```

Key lines:

```text
[==========] Running 111 tests from 6 test cases.
[  PASSED  ] 111 tests.
```

## Remaining explicit boundaries

This slice does **not** prove any of the following:

1. full weighted affinity / anti-affinity parity,
2. taint / toleration parity,
3. migration / preemption policy breadth,
4. domain-group control actor parity,
5. underlayer scheduler manager actor parity.

Those remain for the final release/package decision and must not be collapsed into a blanket “scheduler parity closed” claim.
