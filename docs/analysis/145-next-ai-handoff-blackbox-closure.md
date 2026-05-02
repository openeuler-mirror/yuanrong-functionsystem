# Handoff — Rust FunctionSystem 0.8 Black-Box Replacement Closure

Date: 2026-05-02
Repo: `/home/lzc/workspace/code/yr_rust/yuanrong-functionsystem`
Branch: `rust-rewrite`
This handoff is committed on branch `rust-rewrite` after the latest verified code slice.
Latest verified code commit: `8d9bda6 fix(runtime): project C++ XPU metrics surfaces`

## 0. One-line mission for the next AI

You are taking over the openYuanrong Rust FunctionSystem 0.8 black-box replacement closure work. Do **not** restart exploration. Continue from the current `rust-rewrite` branch and close the remaining C++ 0.8 parity gaps by comparing C++ source first, adding Rust-only tests, implementing missing Rust behavior, rebuilding/packing the unchanged upper-layer package, and proving the result with the existing single-shot ST command. The goal is: **Rust `yuanrong-functionsystem` can replace official C++ `yuanrong-functionsystem` under the same upper-layer `yuanrong` build/pack/install/test commands and installed layout.**

## 1. Non-negotiable constitution

These are hard rules, not preferences:

1. Modify only Rust `yuanrong-functionsystem` code and repo documentation.
2. Do **not** modify upper-layer `yuanrong`, clean C++ control, datasystem, runtime binaries, ST scripts, generated deployment trees, or test cases to make Rust pass.
3. C++ 0.8.0 is the behavior reference. When a failure or gap appears, inspect C++ source/logs first; do not guess and patch blindly.
4. Build/test parallelism must not exceed:
   - `-j8`
   - `CARGO_BUILD_JOBS=8`
5. Accepted ST command is single-shot:
   ```bash
   bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
   ```
   The older two-step `test.sh -s -r` + manual env export is debug-only, not acceptance.
6. Keep upper-layer `yuanrong` build/pack/test commands and install layout unchanged.
7. Preserve black-box replacement semantics: Rust adapts to the C++/upper-layer contract; non-Rust modules must not adapt to Rust.
8. Use targeted `rustfmt --edition 2021 <changed files>` only; avoid broad formatting churn.
9. Commit with Signed-off-by and decision-trailer style; push to `origin rust-rewrite` when a verified slice is complete.
10. If a gap cannot be closed due missing physical hardware or missing credentials, document exact blocker, required environment, C++ reference, Rust state, and partial tests. Do not claim completion without verification.

## 2. Current verified state

### 2.1 Git state

At handoff:

```text
Repo: /home/lzc/workspace/code/yr_rust/yuanrong-functionsystem
Branch: rust-rewrite
Remote: origin
The branch tip contains this handoff document after the latest verified code slice.
Latest verified code commit: 8d9bda6 fix(runtime): project C++ XPU metrics surfaces
Working tree: clean except local untracked .omx/ state
```

Recent commits already pushed:

```text
8d9bda6 fix(runtime): project C++ XPU metrics surfaces
ad08031 fix(runtime): project C++ disk resources into metrics
0645724 fix(runtime): project C++ NUMA vectors into metrics
a4d953d fix(runtime): project C++ resource labels into metrics
6adbf3e fix(runtime): project C++ resource capacity from metrics
0654c34 fix(runtime): align memory resources with C++ MB semantics
283f52c Record full ST proof after POSIX connect-back fix
b997678 Route server-mode runtimes to proxy POSIX port
```

### 2.2 Environment state

Container:

```text
yr-e2e-master: running
```

Important paths:

```text
Host root:              /home/lzc/workspace/code/yr_rust
Rust repo:              /home/lzc/workspace/code/yr_rust/yuanrong-functionsystem
Clean C++ source root:  /home/lzc/workspace/code/yr_rust/0.8.0/src/yuanrong-functionsystem/functionsystem/src
Container Rust copy:    /workspace/rust_current_fs
Proof lane:             /workspace/proof_source_replace_0_8
Proof lane ST root:     /workspace/proof_source_replace_0_8/src/yuanrong/test/st
Proof lane logs:        /workspace/proof_source_replace_0_8/logs
```

Latest proof evidence:

```text
Proof deploy: /tmp/deploy/02104356
[==========] Running 111 tests from 6 test cases.
[==========] 111 tests from 6 test cases ran. (229601 ms total)
[  PASSED  ] 111 tests.
```

Evidence files in container:

```text
/workspace/proof_source_replace_0_8/logs/runtime003_xpu_projection_full_cpp_st.log
/workspace/proof_source_replace_0_8/logs/runtime003_xpu_projection_full_cpp_st_evidence.txt
/workspace/proof_source_replace_0_8/logs/runtime003_xpu_projection_functionsystem_hashes.txt
/workspace/proof_source_replace_0_8/logs/runtime003_xpu_projection_openyuanrong_hashes.txt
```

## 3. What is already completed

Do not redo these unless a regression appears.

### 3.1 ST black-box proof lane

The Rust FunctionSystem artifacts have repeatedly been built, packed, inserted into the unchanged upper-layer `yuanrong` proof lane, and verified with single-shot cpp ST. Current proof is `111/111 PASS`.

### 3.2 Runtime/proxy parity slices already closed

Closed with tests + proof docs:

- `PROXY-001`: invoke `customTag` / create options parity.
- `COMMON-001`: service metadata validation parity.
- `COMMON-003`: LiteBus SSL env parity.
- `PROXY-002`: proxy-local IAM policy authorization.
- `PROXY-003`: invoke admission / memory / token-bucket parity.
- `RUNTIME-002`: runtime command/env/config builder parity.
- `RUNTIME-004` sub-slices:
  - exit classification
  - POSIX connect-back
  - memory resource unit semantics
- `RUNTIME-003` major metrics slices:
  - CPU/memory/disk/custom scalar projection
  - labels projection
  - NUMA vector projection
  - disk vector extension projection
  - basic GPU/NPU/XPU flag + vector projection surfaces

Proof docs to read for current state:

```text
docs/analysis/129-rust-gap-backlog.md
docs/analysis/130-proxy-invoke-customtag-parity-proof.md
docs/analysis/131-service-metadata-validation-parity-proof.md
docs/analysis/132-litebus-ssl-env-parity-proof.md
docs/analysis/133-cpp-rust-flag-behavior-inventory.md
docs/analysis/134-proxy-iam-policy-parity-proof.md
docs/analysis/135-proxy-invoke-admission-parity-proof.md
docs/analysis/136-runtime-command-env-parity-proof.md
docs/analysis/137-runtime-exit-classification-proof.md
docs/analysis/138-runtime-posix-connectback-st-proof.md
docs/analysis/139-runtime-memory-resource-unit-proof.md
docs/analysis/140-runtime-resource-projection-proof.md
docs/analysis/141-runtime-resource-labels-proof.md
docs/analysis/142-runtime-numa-projection-proof.md
docs/analysis/143-runtime-disk-resources-proof.md
docs/analysis/144-runtime-xpu-projection-proof.md
```

## 4. Main remaining requirement

The next AI should treat the remaining work as one large requirement with several subgoals:

> **Requirement: finish the code-level C++ 0.8 parity closure for Rust FunctionSystem black-box replacement beyond the current ST-covered path. For each release-scope gap, compare C++ source behavior, add Rust tests that fail before the fix, implement Rust-only behavior, verify with targeted tests, rebuild/pack with `-j8`, replace only FunctionSystem artifacts in the proof lane, run unchanged single-shot cpp ST, document the proof, commit, and push.**

The next AI should not spend calls asking what to do next. Execute the subgoals below in order unless a hard blocker appears.

## 5. Required execution sequence

### Step 1 — Rehydrate and sanity-check current baseline

Run:

```bash
cd /home/lzc/workspace/code/yr_rust/yuanrong-functionsystem
git status --short --branch
git fetch origin
git log --oneline -8
docker start yr-e2e-master >/dev/null || true
docker ps --format '{{.Names}} {{.Status}}' | grep '^yr-e2e-master '
```

Expected:

```text
Branch rust-rewrite tracks origin/rust-rewrite.
Latest commit includes 8d9bda6.
Only untracked .omx/ may be present.
yr-e2e-master is running.
```

If there are unexpected modified tracked files, inspect before touching anything.

### Step 2 — Read the backlog and pick the next release-scope gap

Start from:

```text
docs/analysis/129-rust-gap-backlog.md
```

Recommended priority order for the next AI:

1. **ResourceUnit / scheduler resource propagation** — highest value because current metrics work is JSON projection-heavy; black-box replacement needs to ensure C++-style `ResourceUnit`/resource view protobuf paths are not silently bypassed.
2. **Group / NUMA / placement semantics** — current ST has limited coverage; C++ has richer group state and placement behavior.
3. **Full GPU/NPU hardware collector detail** — close parser/shape gaps that can be tested without hardware; document hardware-only blockers separately.
4. **Master / scheduler / meta / IAM route and state matrices** — generate A/B matrices and close P1 behavioral gaps.
5. **Release/package surface audit** — inventory package layout and helper library deltas; implement shims only when a real consumer path exists.

## 6. Subgoal A — ResourceUnit / resource-view propagation closure

### Why this matters

Recent work added C++-shaped resource projection JSON in Rust `runtime_manager/src/metrics.rs`, but the C++ implementation builds and propagates protobuf `resources::ResourceUnit` through runtime-manager, function-agent, and resource-view/scheduler paths. If Rust only exposes JSON shortcuts, ST can pass while production scheduler/resource semantics diverge.

### C++ references

Inspect these first:

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/runtime_manager/metrics/metrics_actor.cpp
  - MetricsActor::GetResourceUnit
  - BuildResourceUnit
  - BuildResourceUnitWithSystem
  - BuildResourceUnitWithInstance
  - BuildResource
  - BuildHeteroDevClusterResource
  - BuildDiskDevClusterResource
  - BuildNUMAResource

0.8.0/src/yuanrong-functionsystem/functionsystem/src/runtime_manager/manager/runtime_manager.cpp
  - where MetricsClient::GetResourceUnit() is consumed

0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_agent/**
  - Register / UpdateMetrics / resource reporting path

0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/resource_view/**
  - ResourceViewActor::{AddResourceUnit, UpdateResourceUnit, PullResource}

0.8.0/src/yuanrong-functionsystem/proto/posix/resource.proto
```

Rust touchpoints to inspect:

```text
functionsystem/src/runtime_manager/src/metrics.rs
functionsystem/src/function_agent/src/**
functionsystem/src/function_proxy/src/resource_reporter.rs
functionsystem/src/function_proxy/src/resource_view.rs
functionsystem/src/common/utils/src/resource_view/**
functionsystem/src/common/utils/src/schedule_plugin/**
functionsystem/src/common/proto/tests/round_trip.rs
functionsystem/src/common/proto/src/**
```

### Required work

1. Produce a short doc first, e.g.:

```text
docs/analysis/146-resourceunit-propagation-audit.md
```

It must answer:

- Does Rust currently build `yr_proto::resources::ResourceUnit` equivalent to C++ for node capacity/actual/allocatable?
- Does Rust propagate vector resources, disk extensions, heterogeneousInfo, labels, and instance actual-use into scheduler/resource view paths?
- Which path is authoritative for scheduling today: JSON `ResourceProjection`, Rust-local `ResourceVector`, or protobuf `ResourceUnit`?
- Which C++ fields are absent or intentionally out of scope?

2. Add red tests before implementation. Candidate tests:

```text
functionsystem/src/runtime_manager/tests/resource_unit_projection.rs
functionsystem/src/common/utils/tests/resource_view_vector_parity.rs
functionsystem/src/function_agent/tests/resource_report_parity.rs
```

3. Implement Rust-only conversion/propagation if missing. Likely shape:

- Convert current scalar/vector projection into `yr_proto::resources::ResourceUnit` or equivalent Rust resource-view structure.
- Preserve:
  - CPU / Memory scalar names and units
  - disk vector extensions
  - NUMA vectors
  - GPU/NPU vectors and heterogeneousInfo
  - capacity / used / allocatable separation
  - instance actual use where C++ carries it
- Do not remove existing JSON compatibility unless tests prove it is redundant.

4. Verify targeted tests and full proof lane ST.

### Acceptance for Subgoal A

- Audit doc exists and is concrete.
- New tests prove vector/scalar fields survive into the authoritative Rust scheduling/resource path.
- Host targeted tests pass.
- Container build/pack pass.
- Single-shot cpp ST remains `111/111 PASS`.
- Commit and push.

## 7. Subgoal B — Group / NUMA / placement behavior closure

### Why this matters

Current ST pass does not guarantee C++ group state machine, NUMA binding, bin-pack/spread/strict-spread, recovery, and partial-failure semantics. Earlier backlog rows still mark `COMMON-004`, `PROXY-004`, and `PROXY-005` as open/partial.

### C++ references

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_proxy/**/local_group_ctrl_actor*
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/utils/numa_binding.*
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/schedule_plugin/**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/resource_view/**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/utils/struct_transfer.*
```

Rust touchpoints:

```text
functionsystem/src/function_proxy/src/**
functionsystem/src/common/utils/src/schedule_plugin/**
functionsystem/src/common/utils/src/resource_view/**
functionsystem/src/common/utils/src/scheduler_topology/**
```

### Required work

1. Create matrix doc:

```text
docs/analysis/147-group-numa-placement-parity-matrix.md
```

Include at least:

- Create group validation: names, range bounds, duplicates.
- Group policy: pack/spread/strict-spread.
- Group bind metadata to runtime launch/spec.
- NUMA CPU/memory bind enforcement or explicit unsupported boundary.
- Group recovery/persistence after restart.
- Suspend/resume/delete behavior if C++ supports it.

2. Add Rust tests for the highest-risk missing behaviors.
3. Implement missing Rust-only behavior when C++ semantics are clear and release-scoped.
4. If a behavior requires physical NUMA or privileged OS features, add parser/spec/unit tests and document the exact deployed proof gap.

### Acceptance for Subgoal B

- Matrix doc produced.
- At least the release-scope gaps have tests and fixes.
- No changes to ST scripts or C++ control.
- Single-shot ST remains green.
- Commit and push.

## 8. Subgoal C — Full GPU/NPU collector detail where testable

### Current state

Commit `8d9bda6` added basic C++-shaped XPU projection surfaces and flag plumbing. It explicitly did **not** claim full hardware collector parity.

### Remaining C++ details to compare

```text
runtime_manager/metrics/collector/heterogeneous_collector/gpu_probe.cpp
runtime_manager/metrics/collector/heterogeneous_collector/npu_probe.cpp
runtime_manager/metrics/collector/heterogeneous_collector/topo_probe.cpp
runtime_manager/metrics/collector/heterogeneous_collector/topo_info.h
runtime_manager/metrics/collector/system_xpu_collector.cpp
```

### Required work

1. Create:

```text
docs/analysis/148-xpu-hardware-collector-parity-matrix.md
```

2. Test and implement parser-level behavior that does not require physical hardware:

- `nvidia-smi --query-gpu` output parsing.
- `nvidia-smi topo -m` topology parsing if C++ uses it for partition.
- `npu-smi info` parser fixtures for 910B / 910C / 310P3.
- `npu-smi info -t topo` parser fixtures.
- `/etc/hccn.conf` IP parser.
- `/home/sn/config/topology-info.json` fallback edge cases.
- visible device env filtering:
  - `CUDA_VISIBLE_DEVICES`
  - `ASCEND_RT_VISIBLE_DEVICES`

3. Implement Rust-only parser helpers and projection updates.
4. If real hardware is unavailable, do not block the entire handoff. Commit parser/test improvements and document hardware proof still needed.

### Acceptance for Subgoal C

- Parser fixture tests pass.
- Existing no-hardware ST remains green.
- Hardware-only gaps are precisely documented with required command outputs.
- Commit and push.

## 9. Subgoal D — Master / Scheduler / Meta / IAM parity matrices

### Why this matters

Current cpp ST is not representative enough for all production APIs. The backlog still lists P1/P2 risks around function_master, schedulers, metastore, IAM, resource groups, snapshots, route matrices, watch/lease semantics, and persistence.

### Required work

Create one consolidated doc first:

```text
docs/analysis/149-control-plane-parity-matrix.md
```

It should cover:

- `MASTER-001`: snapshot metadata HTTP, watch/sync, delete/list/restore.
- `MASTER-002`: resource group persist/query/migrate/bundles.
- `MASTER-003`: taint/migration/upgrade watch flags.
- `MASTER-004`: HTTP route/status/body/protobuf matrix.
- `SCHED-001`: domain group control and underlayer scheduler manager.
- `SCHED-002`: taints, group policy, migration, preemption/quota policy.
- `META-001`: KV/watch/lease/revision compatibility.
- `META-002`: persistence/backup/restart behavior.
- `IAM-001`: IAM HTTP route/status/header/body compatibility.
- `IAM-002`: token format policy boundary.

For each row, mark:

```text
C++ source path
Rust source path
Current Rust status: closed / partial / no-op / unknown
Release-scope decision: must implement now / document unsupported / defer
Test needed
Implementation needed
Verification command
```

Then implement P1 release-scope gaps in batches. Do not try to close every P2/P3 policy boundary if it is not required for black-box replacement; document it clearly.

### Acceptance for Subgoal D

- Matrix doc exists and is concrete enough for review.
- P1 gaps either closed with Rust tests or documented as explicit release-scope exclusions.
- No non-Rust changes.
- Single-shot ST remains green after any code changes.
- Commit and push.

## 10. Subgoal E — Release/package surface audit

### Why this matters

The ultimate goal includes not changing upper-layer build/pack/install commands or installed layout. Current package works for ST, but byte-for-byte equivalence is not required yet. Still, file-level consumers may depend on helper libraries or path names.

### Required work

Create:

```text
docs/analysis/150-release-surface-final-audit.md
```

Compare C++ official 0.8.0 functionsystem package vs Rust package:

- tar.gz name
- wheel name
- binary names
- config paths
- deploy scripts
- lib directory contents
- metrics.tar.gz contents
- helper libs such as `libyaml_tool.so`
- version strings and dist-info metadata
- symlinks / permissions / executable bits

Implement only release-scope shims that are necessary for black-box replacement. If a file is intentionally omitted, document the reason and consumer impact.

### Acceptance for Subgoal E

- Final package inventory doc exists.
- Any required Rust-side package shims implemented and tested.
- Upper-layer package script still works unchanged.
- Single-shot ST remains green.
- Commit and push.

## 11. Standard commands

### 11.1 Host targeted verification

Run from host repo:

```bash
cd /home/lzc/workspace/code/yr_rust/yuanrong-functionsystem
export CARGO_BUILD_JOBS=8
cargo test -p yr-runtime-manager --test metrics_resource_projection --test flag_compat_smoke --test config_defaults_grouped -- --nocapture
cargo test -p yr-agent --test merge_process_config --test flag_compat_smoke -- --nocapture
cargo check --workspace --lib --bins
git diff --check
```

Add package-specific tests for whatever module you touched.

### 11.2 Sync changed files into container build copy

Example; replace file list with changed tracked files:

```bash
cd /home/lzc/workspace/code/yr_rust/yuanrong-functionsystem

tar -C /home/lzc/workspace/code/yr_rust/yuanrong-functionsystem -cf - \
  functionsystem/src/runtime_manager/src/config.rs \
  functionsystem/src/runtime_manager/src/metrics.rs \
  functionsystem/src/function_agent/src/config.rs \
  docs/analysis/129-rust-gap-backlog.md \
| docker exec -i yr-e2e-master tar -C /workspace/rust_current_fs -xf -
```

### 11.3 Container build/pack

```bash
docker exec yr-e2e-master bash -lc '
set -euo pipefail
cd /workspace/rust_current_fs
export CARGO_BUILD_JOBS=8
# Run touched-module tests first.
./run.sh build -j 8
./run.sh pack
'
```

### 11.4 Replace only FunctionSystem artifacts in proof lane

```bash
docker exec yr-e2e-master bash -lc '
set -euo pipefail
ROOT=/workspace/proof_source_replace_0_8
SRC=/workspace/rust_current_fs/output
LOGDIR=$ROOT/logs
PREFIX=<your_slice_prefix>
mkdir -p "$LOGDIR"
cd "$ROOT/src/yuanrong"

cp -f "$SRC/yr-functionsystem-v0.0.0.tar.gz" output/yr-functionsystem-v0.0.0.tar.gz
cp -f "$SRC/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl" output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
cp -f "$SRC/metrics.tar.gz" output/metrics.tar.gz

sha256sum output/yr-functionsystem-v0.0.0.tar.gz \
          output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl \
          output/metrics.tar.gz \
  | tee "$LOGDIR/${PREFIX}_functionsystem_hashes.txt"

bash scripts/package_yuanrong.sh -v v0.0.1 2>&1 | tee "$LOGDIR/${PREFIX}_package_yuanrong.log"

sha256sum output/openyuanrong-v0.0.1.tar.gz \
          output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl \
  | tee "$LOGDIR/${PREFIX}_openyuanrong_hashes.txt"
'
```

### 11.5 Single-shot ST acceptance

```bash
docker exec yr-e2e-master bash -lc '
set -o pipefail
ROOT=/workspace/proof_source_replace_0_8
PREFIX=<your_slice_prefix>
cd "$ROOT/src/yuanrong/test/st"
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest" 2>&1 | tee "$ROOT/logs/${PREFIX}_full_cpp_st.log"
'
```

Extract evidence:

```bash
docker exec yr-e2e-master bash -lc '
ROOT=/workspace/proof_source_replace_0_8
PREFIX=<your_slice_prefix>
LOG=$ROOT/logs/${PREFIX}_full_cpp_st.log
DEPLOY=$(grep -o "/tmp/deploy/[0-9]*" "$LOG" | tail -n1 || true)
echo DEPLOY=$DEPLOY
if [ -n "$DEPLOY" ]; then
  grep -E "Running [0-9]+ tests|tests from|PASSED|FAILED|FAILED TEST" "$DEPLOY/driver/cpp_output.txt" | tail -n 100
  { echo "deploy=$DEPLOY"; grep -E "Running [0-9]+ tests|tests from|PASSED|FAILED|FAILED TEST" "$DEPLOY/driver/cpp_output.txt" | tail -n 100; } \
    > "$ROOT/logs/${PREFIX}_full_cpp_st_evidence.txt"
fi
'
```

## 12. Debugging rules if ST fails

1. Do not assume Rust is wrong until you compare with C++ control or previous proof lane logs.
2. Do not patch ST scripts.
3. Find deploy path from ST log:
   ```bash
   grep -o "/tmp/deploy/[0-9]*" <log> | tail -n1
   ```
4. Check:
   ```text
   <deploy>/driver/cpp_output.txt
   <deploy>/logs/*
   <deploy>/runtime/*
   function_proxy / function_agent / runtime_manager logs
   etcd keys only for diagnostics, not manual repair as proof
   ```
5. If the failure is environmental, prove it with clean rerun or control lane. If it is Rust, add a regression test before fixing.

## 13. Commit and push requirements

Use focused commits. Example:

```bash
git add <changed files>
git commit -m "fix(runtime): propagate C++ ResourceUnit vectors" -m "<body with evidence>"
git push origin rust-rewrite
```

Commit body must include:

```text
Constraint: Modify only Rust yuanrong-functionsystem and repo docs
Constraint: Build/test parallelism capped at -j8 / CARGO_BUILD_JOBS=8
Rejected: <alternative> | <reason>
Confidence: <low|medium|high>
Scope-risk: <narrow|moderate|broad>
Directive: <future warning>
Tested: <targeted tests>
Tested: <container build/pack>
Tested: <single-shot ST deploy and pass count>
Not-tested: <honest gaps>
Signed-off-by: luozhancheng <luozhancheng@gmail.com>
```

## 14. Final deliverable expected from the next AI

The next AI should produce one consolidated completion report containing:

1. Commits pushed.
2. Files changed.
3. Which backlog rows were closed, partially closed, or explicitly deferred.
4. C++ references inspected.
5. Rust tests added.
6. Host verification evidence.
7. Container build/pack evidence.
8. Single-shot ST evidence with deploy path and pass count.
9. Remaining risks that truly need human/hardware decisions.
10. Updated docs paths.

## 15. Direct prompt to give the next AI

Copy this section as the actual task prompt:

```text
You are taking over openYuanrong Rust FunctionSystem 0.8 black-box replacement closure work.

Do not start from scratch. Work in:
/home/lzc/workspace/code/yr_rust/yuanrong-functionsystem
branch rust-rewrite.

The branch tip contains this handoff document after the latest verified code slice. Use `git log --oneline -3` for the exact current handoff-doc commit.

Current latest verified code commit is:
8d9bda6 fix(runtime): project C++ XPU metrics surfaces

Ultimate goal:
Rust yuanrong-functionsystem must black-box replace official C++ yuanrong-functionsystem 0.8.0 while upper-layer yuanrong build/pack/install/test commands and installed layout remain unchanged.

Hard rules:
- Modify only Rust yuanrong-functionsystem code and repo docs.
- Do not modify upper-layer yuanrong, clean C++ control, datasystem, runtime binaries, ST scripts, generated deployment trees, or tests to adapt to Rust.
- C++ 0.8.0 is the behavior reference. Inspect C++ source/logs before implementing; do not guess.
- Build/test parallelism max -j8 / CARGO_BUILD_JOBS=8.
- Acceptance ST is single-shot: bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest".
- Commit with Signed-off-by and push to origin rust-rewrite after each verified slice.

Before coding, read:
- docs/analysis/145-next-ai-handoff-blackbox-closure.md
- docs/analysis/129-rust-gap-backlog.md
- docs/analysis/144-runtime-xpu-projection-proof.md
- docs/analysis/140-runtime-resource-projection-proof.md through docs/analysis/143-runtime-disk-resources-proof.md

Current proof state:
- Container yr-e2e-master is running.
- Rust build copy: /workspace/rust_current_fs
- Proof lane: /workspace/proof_source_replace_0_8
- Latest ST proof deploy: /tmp/deploy/02104356
- Single-shot cpp ST: 111/111 PASS.

Your job is to finish the remaining release-scope parity closure in this order:
1. ResourceUnit/resource-view propagation: prove whether Rust propagates C++ ResourceUnit-equivalent scalar/vector/extension/heterogeneousInfo/labels into the authoritative scheduler/resource path. Add failing tests, implement Rust-only behavior if missing, verify, document, commit, push.
2. Group/NUMA/placement behavior: create a C++ vs Rust matrix for group control, bind, policy, recovery, NUMA placement. Close release-scope gaps with tests/fixes. Verify, document, commit, push.
3. Full GPU/NPU collector detail where testable without hardware: add parser fixture tests for nvidia-smi/npu-smi/topology/HCCN/visible-device env semantics; implement Rust-only parser/projection improvements; document hardware-only proof gaps. Verify, document, commit, push.
4. Control-plane parity matrix: master/scheduler/meta/IAM P1 gaps. Generate concrete C++ vs Rust matrix, close release-scope gaps or explicitly document exclusions. Verify, document, commit, push.
5. Release/package final audit: compare C++ official 0.8 package surface vs Rust package surface. Add only necessary Rust-side shims. Verify unchanged upper-layer packaging and single-shot ST. Commit and push.

For every code slice:
- Write/adjust tests first and observe failure when practical.
- Run host targeted tests and cargo check.
- Sync changed files into /workspace/rust_current_fs.
- Run container targeted tests, ./run.sh build -j 8, ./run.sh pack.
- Replace only FunctionSystem artifacts in /workspace/proof_source_replace_0_8/src/yuanrong/output.
- Run unchanged upper-layer package_yuanrong.sh and then single-shot cpp ST.
- Record evidence in docs/analysis and update docs/analysis/129-rust-gap-backlog.md.
- Push to origin rust-rewrite.

Do not ask for confirmation unless blocked by missing credentials, destructive action, or physical hardware that is strictly required. If hardware is unavailable, close parser/unit-testable behavior and document the exact remaining hardware proof requirement.
```
