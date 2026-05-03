# Remaining Black-Box Parity Closure AI Task

> **For agentic workers:** This is a single large handoff task for continuing openYuanrong Rust FunctionSystem 0.8 black-box replacement closure. Do **not** restart from zero. Read the required context, execute the subgoals in order, commit/push each completed slice, and keep the proof lane green.

**Goal:** Finish the remaining code-level C++ 0.8 parity closure so Rust `yuanrong-functionsystem` can be trusted as a black-box replacement for official C++ `yuanrong-functionsystem` under unchanged upper-layer `yuanrong` build/pack/install/test commands and installed layout.

**Architecture:** C++ 0.8.0 is the behavioral oracle. Rust may be implemented differently internally, but externally it must preserve C++-compatible package surfaces, flags, protocols, runtime behavior, resource semantics, and ST behavior. Every gap must be closed by comparing C++ source first, writing Rust-only tests/probes, implementing Rust-only changes, rebuilding/packing in the container, replacing only FunctionSystem artifacts in the proof lane, and running unchanged single-shot ST.

**Tech Stack:** Rust workspace in `yuanrong-functionsystem`; C++ 0.8 reference source; Docker container `yr-e2e-master`; proof lane `/workspace/proof_source_replace_0_8`; ST command `bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"`.

---

## 0. Hard Constitution

These rules are mandatory. Do not weaken them.

1. **Only modify Rust `yuanrong-functionsystem` code and docs in this repo.**
   - Allowed repo root: `/home/lzc/workspace/code/yr_rust/yuanrong-functionsystem`
   - Allowed code changes are Rust FunctionSystem implementation, Rust workspace metadata, Rust-owned proto/contracts when strictly necessary, and repo docs.
2. **Do not modify non-Rust systems to make Rust pass.**
   - Do not modify upper-layer `yuanrong` source, clean C++ control, datasystem, runtime ST scripts, generated deploy trees, installed wheel contents by hand, or test cases to adapt to Rust.
3. **C++ first, no guessing.**
   - Before fixing a gap, read the C++ 0.8 source and write down the expected behavior.
   - Do not patch from symptoms alone.
4. **Host checks are preflight only.**
   - Host `cargo test` / `cargo check` can catch errors early.
   - They do **not** count as acceptance.
   - Do not claim completion if only Host verification passed.
5. **Container build/pack is mandatory for acceptance.**
   - Must run in `yr-e2e-master` under `/workspace/rust_current_fs`:
     ```bash
     export CARGO_BUILD_JOBS=8
     ./run.sh build -j 8
     ./run.sh pack
     ```
6. **Parallelism cap is absolute.**
   - Use `-j8` maximum.
   - Use `CARGO_BUILD_JOBS=8` maximum.
   - Never run unbounded build jobs.
7. **Proof lane acceptance is mandatory.**
   - Replace only FunctionSystem artifacts in `/workspace/proof_source_replace_0_8/src/yuanrong/output`:
     - `yr-functionsystem-v0.0.0.tar.gz`
     - `openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl`
     - `metrics.tar.gz`
   - Re-run unchanged upper-layer packaging script.
   - Run unchanged single-shot ST.
8. **Single-shot ST is the acceptance command.**
   - Required command:
     ```bash
     cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
     bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
     ```
   - `test.sh -s -r` plus exported env is debug-only, not acceptance.
9. **Commit and push completed slices.**
   - Commit message must include `Signed-off-by` and useful Lore trailers.
   - Push to `origin/rust-rewrite` after each verified slice.
10. **No broad formatting churn.**
    - Avoid workspace-wide `cargo fmt` unless specifically needed.
    - Prefer targeted `rustfmt --edition 2021 <files>`.
11. **If hardware is unavailable, close what is testable.**
    - GPU/NPU real-device behavior may need hardware.
    - Add parser/fixture tests where possible and document exact hardware proof blockers.
12. **If a proto/shared contract changes, justify it.**
    - A proto change inside this Rust repo may be acceptable only when it is required for Rust internal black-box replacement and remains compatible with upper-layer packaging/tests.
    - Document why it is not asking non-Rust modules to adapt to Rust.

---

## 1. Current State Snapshot

### Repo and branch

```bash
cd /home/lzc/workspace/code/yr_rust/yuanrong-functionsystem
git checkout rust-rewrite
git pull --ff-only origin rust-rewrite
```

Current latest verified commit at handoff time:

```text
637cbc4 fix(runtime): close ResourceUnit propagation path
```

Expected local state before starting:

```text
branch: rust-rewrite
remote: origin git@gitcode.com:luozhancheng/yuanrong-functionsystem.git
only local untracked noise may be .omx/
```

### C++ reference source

```text
/home/lzc/workspace/code/yr_rust/0.8.0/src/yuanrong-functionsystem/functionsystem/src
```

### Rust source

```text
/home/lzc/workspace/code/yr_rust/yuanrong-functionsystem/functionsystem/src
```

### Container and proof lane

```text
container: yr-e2e-master
container Rust build copy: /workspace/rust_current_fs
proof lane: /workspace/proof_source_replace_0_8
proof lane upper source: /workspace/proof_source_replace_0_8/src/yuanrong
```

### Current accepted proof

Recent proof after the latest control-plane and release/package closure:

```text
proof deploy: /tmp/deploy/03020415
single-shot cpp ST: 111/111 PASS
```

The current ST pass is strong evidence for the active black-box ST path, but it is **not** complete evidence for all C++ production feature surfaces.

---

## 2. Required Reading Before Any Code Change

Read these files in order:

```text
docs/analysis/129-rust-gap-backlog.md
docs/analysis/145-next-ai-handoff-blackbox-closure.md
docs/analysis/146-resourceunit-propagation-audit.md
docs/analysis/147-resourceunit-propagation-proof.md
```

Then read the proof docs relevant to the area you are about to change:

```text
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

---

## 3. Why More Work Remains

Current source-size evidence shows Rust still covers far less code surface than official C++ 0.8:

```text
C++ source: 178,376 lines / 982 files
Rust source: 43,757 production-ish lines / 194 files
Rust is roughly 24.5% of C++ production-ish source by line count.
```

Approximate per-module ratio:

```text
common             C++  67,718 lines | Rust 14,209 lines | 21%
function_proxy     C++  37,677 lines | Rust  8,914 lines | 24%
function_master    C++  22,779 lines | Rust  4,904 lines | 22%
runtime_manager    C++  19,900 lines | Rust  4,976 lines | 25%
function_agent     C++   8,750 lines | Rust  1,984 lines | 23%
domain_scheduler   C++   4,964 lines | Rust  3,513 lines | 71%
meta_store         C++  11,755 lines | Rust  2,739 lines | 23%
iam_server         C++   4,833 lines | Rust  2,518 lines | 52%
```

This does not prove every missing line is required. Rust can be smaller. But it does prove ST success alone is not representative enough for production parity. The remaining task is to close or explicitly document release-scope gaps.

---

## 4. Main Remaining Subgoals

Execute these subgoals in order. Each subgoal should produce:

1. C++ behavior matrix / audit doc.
2. Rust tests/probes for release-scope behavior.
3. Rust-only implementation changes if needed.
4. Host preflight tests.
5. Container build/pack.
6. Proof lane package replacement.
7. Single-shot ST proof.
8. Updated `docs/analysis/129-rust-gap-backlog.md`.
9. Commit + push.

### Subgoal A — Group / NUMA / Placement Behavior Closure

**Purpose:** Close the highest-risk remaining black-box gap: C++ group control, group bind, NUMA bind, placement/bin-pack/spread/strict-spread, duplicate/recovery/partial-failure behavior.

**Backlog rows:**

```text
COMMON-004
PROXY-004
PROXY-005
RUNTIME-003 full NUMA/group placement follow-up
```

**C++ sources to inspect first:**

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/utils/numa_binding.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/utils/numa_binding.h
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_proxy/**/local_group_ctrl_actor*
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_proxy/local_scheduler/**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/scheduler_framework/**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/resource_view/**
```

**Rust areas likely involved:**

```text
functionsystem/src/function_proxy/src/**
functionsystem/src/domain_scheduler/src/**
functionsystem/src/runtime_manager/src/**
functionsystem/src/common/**
```

**Required audit output:**

Create:

```text
docs/analysis/148-group-numa-placement-parity-matrix.md
```

The matrix must include at least:

```text
- group create validation: names, min/max/range, duplicates
- group bind option parsing
- bin-pack / spread / strict-spread semantics
- NUMA CPU/memory bind behavior
- group state persistence/recovery
- partial group create failure behavior
- group delete/kill/suspend/resume behavior if present in C++
- which behaviors are covered by current ST
- which are not covered and need Rust tests
```

**Implementation rule:**

Do not jump directly to patching. First write the matrix and identify exact release-scope gaps.

**Acceptance:**

- Release-scope group/NUMA/placement gaps either closed with tests or explicitly documented as out-of-scope/hardware-gated.
- Current single-shot ST remains `111/111 PASS`.

---

### Subgoal B — GPU / NPU Collector Detail Closure Where Testable

**Purpose:** Close testable GPU/NPU collector gaps without physical hardware, and document exact hardware-only proof blockers.

**Backlog rows:**

```text
RUNTIME-003 full GPU/NPU hardware collector detail
```

**C++ sources to inspect first:**

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/runtime_manager/metrics/collector/**gpu**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/runtime_manager/metrics/collector/**npu**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/runtime_manager/metrics/collector/**xpu**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/runtime_manager/metrics/collector/**heterogeneous**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/runtime_manager/metrics/metrics_actor.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/resource_view/**
```

Use `find`/`grep` if names differ:

```bash
find /home/lzc/workspace/code/yr_rust/0.8.0/src/yuanrong-functionsystem/functionsystem/src/runtime_manager -type f \
  | grep -Ei 'gpu|npu|xpu|hetero|hccn|topology|device'
```

**Rust areas likely involved:**

```text
functionsystem/src/runtime_manager/src/metrics.rs
functionsystem/src/runtime_manager/src/config.rs
functionsystem/src/function_agent/src/config.rs
functionsystem/src/function_agent/src/main.rs
functionsystem/src/function_proxy/src/main.rs
functionsystem/src/function_master/src/resource_agg.rs
functionsystem/src/domain_scheduler/src/resource_view.rs
```

**Required audit output:**

Create:

```text
docs/analysis/149-gpu-npu-collector-parity-proof.md
```

The doc must distinguish:

```text
- Parser/shape behavior testable without hardware
- Runtime flag plumbing testable without hardware
- ResourceUnit/vector/heterogeneousInfo projection testable without hardware
- True hardware behavior requiring GPU/NPU devices
- Exact commands/logs needed from a hardware host to close the remaining proof
```

**Tests to add where possible:**

Add fixture/unit tests for C++-shaped parser inputs, for example:

```text
- nvidia-smi output variants
- npu-smi output variants
- topology/HCCN examples if C++ parses them
- visible-device env semantics
- missing command / malformed output behavior
- zero-device behavior
```

**Acceptance:**

- All parser/shape/plumbing behavior that can be tested without hardware is covered.
- Hardware-only gap is documented precisely, not hand-waved.
- Current single-shot ST remains `111/111 PASS`.

---

### Subgoal C — Control-Plane Parity Matrix and P1 Closure

**Purpose:** ST is not broad enough to prove master/scheduler/meta/IAM/control-plane compatibility. Generate concrete C++ vs Rust matrices, then close P1 release-scope gaps.

**Backlog rows:**

```text
MASTER-001 snapshot metadata HTTP/watch/sync/delete/list/restore
MASTER-002 resource group persist/query/migrate/bundles
MASTER-003 taint/migration/upgrade flags
MASTER-004 full C++ HTTP route/status/body/protobuf matrix
SCHED-001 domain group control and underlayer scheduler manager
SCHED-002 taints, group policy, migration, preemption/quota policies
META-001 KV/watch/lease/revision behavior
META-002 backup/persistence flush/restart behavior
PROXY-007 high-risk flags not yet closed
```

**C++ sources to inspect first:**

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_master/**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/domain_scheduler/**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/meta_store/**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/iam_server/**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/iam/**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/flags/**
```

**Rust areas likely involved:**

```text
functionsystem/src/function_master/src/**
functionsystem/src/domain_scheduler/src/**
functionsystem/src/meta_store/src/**
functionsystem/src/iam_server/src/**
functionsystem/src/function_proxy/src/**
functionsystem/src/common/**
```

**Required audit output:**

Create:

```text
docs/analysis/150-control-plane-parity-matrix.md
```

The matrix must include:

```text
- HTTP route path
- method
- request body/protobuf shape
- response status/body/protobuf shape
- auth/IAM requirement if any
- persistence side effect
- watch/lease/revision semantics if relevant
- C++ source reference
- Rust source reference
- current status: closed / partial / missing / intentionally excluded
- priority: P1 / P2 / P3
```

**Implementation rule:**

Do not try to close all P2/P3 features blindly. Close P1 release-scope gaps first. Explicitly document excluded surfaces.

**Acceptance:**

- P1 control-plane gaps are either closed with Rust tests/probes or documented as explicit release-scope exclusions.
- Current single-shot ST remains `111/111 PASS`.

---

### Subgoal D — Final Release / Package Surface Audit

**Purpose:** Prove Rust FunctionSystem package is a black-box replacement at artifact/layout/command level, not only at ST behavior level.

**Reference artifacts:**

Official C++ 0.8 package artifacts in the clean baseline/proof lane and Rust replacement artifacts in `/workspace/rust_current_fs/output`.

**Compare these surfaces:**

```text
- tar.gz root layout
- bin names and executable permissions
- config files
- deploy files
- lib / so files
- metrics.tar.gz layout
- wheel metadata
- wheel top-level package contents
- CLI names
- accepted flags
- startup command lines
- package_yuanrong.sh inputs/outputs
```

**Required audit output:**

Create:

```text
docs/analysis/151-release-package-surface-audit.md
```

**Acceptance:**

- Any missing release-surface item is either restored by Rust-side shim or documented as intentionally unnecessary.
- Upper-layer packaging command remains unchanged.
- Current single-shot ST remains `111/111 PASS`.

---

## 5. Verification Commands

### 5.1 Host preflight commands

Run targeted tests for the slice you changed, then run at least:

```bash
cd /home/lzc/workspace/code/yr_rust/yuanrong-functionsystem
export CARGO_BUILD_JOBS=8
cargo check --workspace --lib --bins
git diff --check
```

Host preflight is not acceptance.

### 5.2 Sync to container

Use `rsync` or `tar` to update `/workspace/rust_current_fs` from the host repo. Example:

```bash
cd /home/lzc/workspace/code/yr_rust/yuanrong-functionsystem
rsync -a --delete \
  --exclude target \
  --exclude .git \
  --exclude .omx \
  ./ yr-e2e-master:/workspace/rust_current_fs/
```

If Docker name resolution for `rsync` is unavailable, use `docker cp` or tar streaming.

### 5.3 Container build/pack acceptance

```bash
docker exec yr-e2e-master bash -lc '
set -euo pipefail
cd /workspace/rust_current_fs
export CARGO_BUILD_JOBS=8
./run.sh build -j 8
./run.sh pack
'
```

Capture logs under:

```text
/workspace/proof_source_replace_0_8/logs/<slice>_container_build.log
/workspace/proof_source_replace_0_8/logs/<slice>_container_pack.log
```

### 5.4 Replace only FunctionSystem artifacts in proof lane

```bash
docker exec yr-e2e-master bash -lc '
set -euo pipefail
ROOT=/workspace/proof_source_replace_0_8
SRC=/workspace/rust_current_fs/output
LOGDIR=$ROOT/logs
cd "$ROOT/src/yuanrong"
mkdir -p "$LOGDIR"

cp -f "$SRC/yr-functionsystem-v0.0.0.tar.gz" output/yr-functionsystem-v0.0.0.tar.gz
cp -f "$SRC/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl" output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
cp -f "$SRC/metrics.tar.gz" output/metrics.tar.gz

sha256sum \
  output/yr-functionsystem-v0.0.0.tar.gz \
  output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl \
  output/metrics.tar.gz \
  | tee "$LOGDIR/<slice>_functionsystem_hashes.txt"

bash scripts/package_yuanrong.sh -v v0.0.1 2>&1 | tee "$LOGDIR/<slice>_package_yuanrong.log"

sha256sum \
  output/openyuanrong-v0.0.1.tar.gz \
  output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl \
  | tee "$LOGDIR/<slice>_openyuanrong_hashes.txt"
'
```

Replace `<slice>` with a short name, for example:

```text
group_numa_placement
gpu_npu_collectors
control_plane_matrix
release_package_audit
```

### 5.5 Single-shot ST acceptance

```bash
docker exec yr-e2e-master bash -lc '
set -o pipefail
ROOT=/workspace/proof_source_replace_0_8
cd "$ROOT/src/yuanrong/test/st"
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest" 2>&1 \
  | tee "$ROOT/logs/<slice>_full_cpp_st.log"
'
```

Extract evidence:

```bash
docker exec yr-e2e-master bash -lc '
ROOT=/workspace/proof_source_replace_0_8
LOG="$ROOT/logs/<slice>_full_cpp_st.log"
DEPLOY=$(grep -o "/tmp/deploy/[0-9]*" "$LOG" | tail -n1 || true)
echo "deploy=$DEPLOY"
if [ -n "$DEPLOY" ] && [ -f "$DEPLOY/driver/cpp_output.txt" ]; then
  grep -E "Running [0-9]+ tests|tests from|PASSED|FAILED|FAILED TEST" "$DEPLOY/driver/cpp_output.txt" | tail -n 100
  {
    echo "deploy=$DEPLOY"
    grep -E "Running [0-9]+ tests|tests from|PASSED|FAILED|FAILED TEST" "$DEPLOY/driver/cpp_output.txt" | tail -n 100
  } > "$ROOT/logs/<slice>_full_cpp_st_evidence.txt"
fi
'
```

Acceptance requires:

```text
[==========] Running 111 tests from 6 test cases.
[  PASSED  ] 111 tests.
```

---

## 6. Documentation Requirements Per Slice

For each completed slice, create or update docs under `docs/analysis/`:

```text
- C++ behavior references with file paths
- Rust pre-fix state
- Tests/probes added
- Code changes made
- Host preflight result
- Container build/pack result
- Artifact hashes
- Proof lane deploy path
- ST result
- Remaining risks / hardware blockers / release-scope exclusions
```

Always update:

```text
docs/analysis/129-rust-gap-backlog.md
```

---

## 7. Commit Message Template

Use this shape:

```text
fix(<scope>): close <specific parity gap>

Explain the C++ behavior, the Rust gap, and why the Rust-only implementation preserves black-box replacement without changing upper-layer yuanrong or C++ control.

Constraint: Modify only Rust yuanrong-functionsystem and repo docs
Constraint: Build/test parallelism capped at -j8 / CARGO_BUILD_JOBS=8
Rejected: Adapting upper-layer yuanrong/package/ST inputs | Black-box replacement requires Rust to fit the existing contract
Confidence: <low|medium|high>
Scope-risk: <narrow|moderate|broad>
Directive: <future warning>
Tested: Host targeted tests + cargo check --workspace --lib --bins + git diff --check
Tested: Container yr-e2e-master /workspace/rust_current_fs ./run.sh build -j 8 && ./run.sh pack
Tested: Proof lane /workspace/proof_source_replace_0_8 single-shot ST deploy <deploy> => 111/111 passed
Not-tested: <honest gaps, hardware blockers, excluded P2/P3 surfaces>
Signed-off-by: luozhancheng <luozhancheng@gmail.com>
```

Then push:

```bash
git push origin rust-rewrite
```

---

## 8. Stop Condition

Do not stop after only one local fix unless blocked.

The task is complete only when:

1. Subgoal A, B, C, and D are either closed or explicitly documented as release-scope exclusions/hardware blockers.
2. `docs/analysis/129-rust-gap-backlog.md` reflects the final status.
3. Rust FunctionSystem container build/pack succeeds with `-j8`.
4. Proof lane uses only replaced FunctionSystem artifacts and unchanged upper-layer package scripts.
5. Final single-shot ST is `111/111 PASS`.
6. All commits are pushed to `origin/rust-rewrite`.
7. Final report lists:
   - closed backlog rows
   - remaining documented exclusions
   - final commit IDs
   - final deploy path
   - final ST evidence file path
