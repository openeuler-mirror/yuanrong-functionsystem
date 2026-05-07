# Production Control-Plane Parity Closure AI Task

> **Status:** Executed and closed on `rust-rewrite` by commits `c7cc637` through `0b62f6b`.
> Latest remote HEAD at closure: `0b62f6b7e397c1abd8cd3f74b59cc70160594794`.
> Final proof deploy: `/tmp/deploy/03140532`; single-shot cpp ST evidence reports `111/111 PASS`.
> This document is retained as the large-task handoff/requirements record, not as a still-open work item.


> **For agentic workers:** This is a large continuation task for openYuanrong Rust FunctionSystem 0.8 black-box replacement. Do not restart exploration. Continue from the current `rust-rewrite` branch and close the remaining production-control-plane parity gaps against official C++ 0.8.

**Goal:** Move Rust `yuanrong-functionsystem` from “accepted ST lane works” toward “production-control-plane compatible with C++ 0.8” by auditing and closing the largest remaining gaps not covered by current ST.

**Current baseline:**

```text
Repo: /home/lzc/workspace/code/yr_rust/yuanrong-functionsystem
Branch: rust-rewrite
Latest verified commit at task creation: 73e46e6 fix(master): enforce exact masterinfo type parity
C++ 0.8 reference: /home/lzc/workspace/code/yr_rust/0.8.0/src/yuanrong-functionsystem/functionsystem/src
Container: yr-e2e-master
Container Rust build copy: /workspace/rust_current_fs
Proof lane: /workspace/proof_source_replace_0_8
Current accepted proof deploy: /tmp/deploy/03041832
Current accepted ST: 111/111 PASS
```

The current Rust lane is accepted for the official upper-layer build/pack/install/ST path, but **not** claimed to be byte-for-byte or file-inventory-identical to the C++ package for arbitrary external consumers.

---

## 0. Hard Constitution

These rules are mandatory.

1. **Only modify Rust `yuanrong-functionsystem` code and docs in this repo.**
   - Allowed root: `/home/lzc/workspace/code/yr_rust/yuanrong-functionsystem`
   - Do not modify upper-layer `yuanrong`, clean C++ control, datasystem, ST scripts, generated deploy trees, or tests to adapt to Rust.
2. **C++ first, no guessing.**
   - For every gap, inspect C++ 0.8 source first.
   - Write down the C++ behavior before changing Rust.
3. **Host checks are preflight only.**
   - Host `cargo test` / `cargo check` can catch issues early.
   - Host checks are not acceptance.
4. **Container build/pack is mandatory.**
   - Acceptance requires:
     ```bash
     docker exec yr-e2e-master bash -lc '
     set -euo pipefail
     cd /workspace/rust_current_fs
     export CARGO_BUILD_JOBS=8
     ./run.sh build -j 8
     ./run.sh pack
     '
     ```
5. **Parallelism cap is absolute.**
   - Maximum `-j8`.
   - Maximum `CARGO_BUILD_JOBS=8`.
6. **Proof lane must replace only FunctionSystem artifacts.**
   - Replace only:
     ```text
     yr-functionsystem-v0.0.0.tar.gz
     openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
     metrics.tar.gz
     ```
7. **Upper-layer packaging command must remain unchanged.**
   ```bash
   cd /workspace/proof_source_replace_0_8/src/yuanrong
   bash scripts/package_yuanrong.sh -v v0.0.1
   ```
8. **Single-shot ST is the acceptance command.**
   ```bash
   cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
   bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
   ```
   Expected:
   ```text
   [==========] Running 111 tests from 6 test cases.
   [  PASSED  ] 111 tests.
   ```
9. **Do not use `test.sh -s -r` as acceptance.**
   - It is debug-only.
10. **No ABI-risky fake compatibility.**
    - Do not add fake `libssl.so.3` / `libcrypto.so.3` symlinks unless an ABI-compatible OpenSSL 3 provider exists and is required.
    - Do not add fake `libyaml_tool.so` unless a real external consumer requires it and the shim is behaviorally justified.
11. **Commit discipline.**
    - Each completed slice must update docs/backlog, commit, and push.
    - Commit message must include `Signed-off-by` and verification evidence.
12. **Do not overclaim.**
    - ST passing is required but not sufficient to claim full C++ parity.
    - If a feature is excluded, document exact reason and release-scope boundary.

---

## 1. Required Reading

Read these first:

```text
docs/analysis/129-rust-gap-backlog.md
docs/analysis/148-remaining-blackbox-parity-ai-task.md
docs/analysis/148-group-numa-placement-parity-matrix.md
docs/analysis/149-group-numa-placement-proof.md
docs/analysis/150-gpu-npu-collector-parity-matrix.md
docs/analysis/151-gpu-npu-collector-proof.md
docs/analysis/152-control-plane-parity-matrix.md
docs/analysis/153-control-plane-master-http-proof.md
docs/analysis/154-release-package-surface-audit.md
```

Also inspect latest git state:

```bash
cd /home/lzc/workspace/code/yr_rust/yuanrong-functionsystem
git checkout rust-rewrite
git pull --ff-only origin rust-rewrite
git status --short --branch
git log --oneline -10
```

Expected: branch is clean except possible local `.omx/` noise.

---

## 2. Large Task Overview

Close or explicitly bound these remaining high-value parity areas:

1. Resource Group full state machine parity.
2. Snapshot manager deep parity.
3. MetaStore KV/watch/lease/revision/persistence parity.
4. IAM route/body/status/token parity.
5. Scheduler policy parity.
6. Final release package decision.

Execute in order. Each subgoal should produce:

- C++ behavior matrix / audit doc.
- Rust tests or probes.
- Rust-only implementation changes if needed.
- Host preflight.
- Container build/pack.
- Proof lane replacement + repack.
- Single-shot ST evidence.
- `docs/analysis/129-rust-gap-backlog.md` update.
- Commit + push.

---

## 3. Subgoal A — Resource Group Full State Machine Parity

### Purpose

Close the biggest likely C++/Rust production-control-plane gap around resource group lifecycle and scheduling semantics.

### C++ sources to inspect first

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_master/resource_group_manager/**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_proxy/**/local_group_ctrl_actor*
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_proxy/**/local_scheduler/**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/scheduler_framework/**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/resource_view/**
```

Use search if needed:

```bash
grep -R "ResourceGroup\|resource group\|rgroup\|RGroup\|rGroup" -n \
  /home/lzc/workspace/code/yr_rust/0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_master \
  /home/lzc/workspace/code/yr_rust/0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_proxy
```

### Rust sources likely involved

```text
functionsystem/src/function_master/src/**
functionsystem/src/function_proxy/src/**
functionsystem/src/domain_scheduler/src/**
functionsystem/src/common/**
functionsystem/src/function_master/tests/**
functionsystem/src/function_proxy/tests/**
```

### Must audit

- create resource group
- query resource group
- delete resource group
- duplicate group behavior
- pending / created / failed transitions
- detached vs non-detached lifecycle
- driver exit cleanup
- job kill cleanup
- bundle creation and deletion
- partial failure behavior
- sync/recover/migrate behavior
- C++ response codes/messages and Rust response codes/messages
- what current ST covers and does not cover

### Required outputs

Create:

```text
docs/analysis/155-resource-group-full-parity-matrix.md
docs/analysis/156-resource-group-full-parity-proof.md
```

### Acceptance

- Release-scope resource-group gaps are closed or explicitly documented.
- Tests/probes exist for every closed behavior.
- Single-shot ST remains 111/111 PASS.

---

## 4. Subgoal B — Snapshot Manager Deep Parity

### Purpose

The current Rust route-level snapshot query/list behavior is partially closed. Finish or bound deeper snapshot semantics.

### C++ sources to inspect first

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_master/snap_manager/**
```

### Rust sources likely involved

```text
functionsystem/src/function_master/src/snapshot.rs
functionsystem/src/function_master/src/http.rs
functionsystem/src/function_master/tests/**
```

### Must audit

- query snapshot
- list snapshots
- delete snapshot
- restore snapshot
- watch/sync behavior
- protobuf body/status exactness
- malformed body behavior
- empty body behavior
- missing snapshot behavior
- persistence interaction if any

### Required outputs

Create:

```text
docs/analysis/157-snapshot-manager-parity-matrix.md
docs/analysis/158-snapshot-manager-parity-proof.md
```

### Acceptance

- Query/list route parity remains closed.
- Delete/restore/watch/sync are either implemented/tested or explicitly excluded with C++ evidence.
- Single-shot ST remains 111/111 PASS.

---

## 5. Subgoal C — MetaStore KV / Watch / Lease / Revision / Persistence Parity

### Purpose

Current Rust MetaStore supports the subset needed for the accepted ST lane. Prove or close production edge semantics.

### C++ / official reference areas

Search under the clean 0.8 tree:

```bash
grep -R "KV\|Watch\|Lease\|revision\|compact\|MetaStore\|meta_service" -n \
  /home/lzc/workspace/code/yr_rust/0.8.0/src/yuanrong-functionsystem/functionsystem/src \
  /home/lzc/workspace/code/yr_rust/0.8.0/src/yuanrong 2>/dev/null | head -300
```

### Rust sources likely involved

```text
functionsystem/src/meta_store/src/**
functionsystem/src/meta_store/tests/**
functionsystem/src/function_master/src/topology.rs
functionsystem/src/function_master/src/instances.rs
```

### Must audit

- KV put/get/delete/range
- prefix query
- revision header behavior
- watch create/cancel/progress
- lease grant/keepalive/revoke
- compaction behavior
- crash/restart persistence
- interaction with master topology/instance watches

### Required outputs

Create:

```text
docs/analysis/159-metastore-parity-matrix.md
docs/analysis/160-metastore-parity-proof.md
```

### Acceptance

- Focused Rust tests/probes cover release-scope MetaStore behavior.
- Any unimplemented etcd edge semantics are explicitly documented.
- Single-shot ST remains 111/111 PASS.

---

## 6. Subgoal D — IAM Route / Body / Status / Token Parity

### Purpose

Rust proxy-local IAM policy is already closed for current use, but IAM server route/status/body/token behavior is not fully proven against C++.

### C++ sources to inspect first

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/iam_server/**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/iam/**
```

### Rust sources likely involved

```text
functionsystem/src/iam_server/src/**
functionsystem/src/iam_server/tests/**
functionsystem/src/function_proxy/src/iam_policy.rs
functionsystem/src/function_proxy/tests/**
```

### Must audit

- token route behavior
- AKSK route behavior
- internal IAM route behavior
- request body formats
- response status codes
- response body fields
- error behavior
- token format compatibility
- proxy-side token/AKSK/policy consumption

### Required outputs

Create:

```text
docs/analysis/161-iam-control-plane-parity-matrix.md
docs/analysis/162-iam-control-plane-parity-proof.md
```

### Acceptance

- Release-scope IAM route/body/status behavior is tested and closed or explicitly bounded.
- Token byte-format mismatch is either fixed or documented as a policy boundary.
- Single-shot ST remains 111/111 PASS.

---

## 7. Subgoal E — Scheduler Policy Parity

### Purpose

Current Rust scheduler covers the accepted ST path and some resource-view behavior, but C++ policy breadth is wider.

### C++ sources to inspect first

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/domain_scheduler/**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/scheduler_framework/**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/resource_view/**
0.8.0/src/yuanrong-functionsystem/functionsystem/src/common/scheduler_topology/**
```

### Rust sources likely involved

```text
functionsystem/src/domain_scheduler/src/**
functionsystem/src/function_master/src/scheduler.rs
functionsystem/src/function_master/src/resource_agg.rs
functionsystem/src/domain_scheduler/tests/**
```

### Must audit

- group policy
- bin-pack / spread / strict-spread
- taints / tolerations
- migration
- preemption
- quota/resource-view policy
- underlayer scheduler manager behavior
- policy interactions with ResourceUnit

### Required outputs

Create:

```text
docs/analysis/163-scheduler-policy-parity-matrix.md
docs/analysis/164-scheduler-policy-parity-proof.md
```

### Acceptance

- Release-scope scheduler policy gaps are closed or explicitly bounded.
- Tests/probes cover all closed claims.
- Single-shot ST remains 111/111 PASS.

---

## 8. Subgoal F — Final Release Package Decision

### Purpose

Turn the current package surface audit into a final decision record.

### Known current drift

C++ clean package currently has entries Rust lacks:

```text
functionsystem/lib/libcrypto.so.3
functionsystem/lib/libssl.so.3
functionsystem/lib/libyaml_tool.so
```

Rust currently has extra entries, including:

```text
functionsystem/bin/meta_store
```

See:

```text
docs/analysis/154-release-package-surface-audit.md
```

### Must decide

For each package drift category:

- restore by Rust-side implementation/shim
- keep as compatible superset
- explicitly exclude from current black-box claim

### Hard rules

- Do not add ABI-risky OpenSSL 3 symlinks.
- Do not add fake `libyaml_tool.so` without a real consumer and behavior contract.
- Do not claim file-inventory identity unless direct comparison proves it.

### Required outputs

Create:

```text
docs/analysis/165-final-release-surface-decision.md
```

Update:

```text
docs/analysis/129-rust-gap-backlog.md
```

### Acceptance

- Final claim is precise and honest.
- If no package change is needed, document why.
- Single-shot ST remains 111/111 PASS.

---

## 9. Standard Verification Procedure Per Slice

### Host preflight

Run targeted tests, then at minimum:

```bash
cd /home/lzc/workspace/code/yr_rust/yuanrong-functionsystem
export CARGO_BUILD_JOBS=8
cargo check --workspace --lib --bins
git diff --check
```

### Sync to container

Use either tar streaming, rsync, or docker copy. Example for changed files:

```bash
cd /home/lzc/workspace/code/yr_rust/yuanrong-functionsystem
CHANGED=$(git diff --name-only)
printf '%s\n' "$CHANGED" | tar -C /home/lzc/workspace/code/yr_rust/yuanrong-functionsystem -cf - -T - \
  | docker exec -i yr-e2e-master tar -C /workspace/rust_current_fs -xf -
```

If a clean full sync is needed, use a safe exclude list and do not delete proof lane.

### Container build/pack

```bash
docker exec yr-e2e-master bash -lc '
set -euo pipefail
ROOT=/workspace/proof_source_replace_0_8
SLICE=<slice-name>
cd /workspace/rust_current_fs
export CARGO_BUILD_JOBS=8
./run.sh build -j 8 2>&1 | tee "$ROOT/logs/${SLICE}_container_build.log"
./run.sh pack 2>&1 | tee "$ROOT/logs/${SLICE}_container_pack.log"
'
```

### Replace proof lane artifacts

```bash
docker exec yr-e2e-master bash -lc '
set -euo pipefail
ROOT=/workspace/proof_source_replace_0_8
SRC=/workspace/rust_current_fs/output
LOGDIR=$ROOT/logs
SLICE=<slice-name>
cd "$ROOT/src/yuanrong"
mkdir -p "$LOGDIR"

cp -f "$SRC/yr-functionsystem-v0.0.0.tar.gz" output/yr-functionsystem-v0.0.0.tar.gz
cp -f "$SRC/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl" output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
cp -f "$SRC/metrics.tar.gz" output/metrics.tar.gz

sha256sum \
  output/yr-functionsystem-v0.0.0.tar.gz \
  output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl \
  output/metrics.tar.gz \
  | tee "$LOGDIR/${SLICE}_functionsystem_hashes.txt"

bash scripts/package_yuanrong.sh -v v0.0.1 2>&1 | tee "$LOGDIR/${SLICE}_package_yuanrong.log"

sha256sum \
  output/openyuanrong-v0.0.1.tar.gz \
  output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl \
  | tee "$LOGDIR/${SLICE}_openyuanrong_hashes.txt"
'
```

### Single-shot ST

```bash
docker exec yr-e2e-master bash -lc '
set -o pipefail
ROOT=/workspace/proof_source_replace_0_8
SLICE=<slice-name>
cd "$ROOT/src/yuanrong/test/st"
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest" 2>&1 \
  | tee "$ROOT/logs/${SLICE}_full_cpp_st.log"
'
```

Extract evidence:

```bash
docker exec yr-e2e-master bash -lc '
ROOT=/workspace/proof_source_replace_0_8
SLICE=<slice-name>
LOG="$ROOT/logs/${SLICE}_full_cpp_st.log"
DEPLOY=$(grep -o "/tmp/deploy/[0-9]*" "$LOG" | tail -n1 || true)
echo "deploy=$DEPLOY"
if [ -n "$DEPLOY" ] && [ -f "$DEPLOY/driver/cpp_output.txt" ]; then
  grep -E "Running [0-9]+ tests|tests from|PASSED|FAILED|FAILED TEST" "$DEPLOY/driver/cpp_output.txt" | tail -n 100
  {
    echo "deploy=$DEPLOY"
    grep -E "Running [0-9]+ tests|tests from|PASSED|FAILED|FAILED TEST" "$DEPLOY/driver/cpp_output.txt" | tail -n 100
  } > "$ROOT/logs/${SLICE}_full_cpp_st_evidence.txt"
fi
'
```

---

## 10. Documentation Requirements Per Slice

Every proof doc must include:

```text
- C++ source references
- Rust pre-fix state
- exact closed behavior
- tests/probes added
- code files changed
- Host preflight evidence
- Container build/pack logs
- artifact hashes
- proof deploy path
- ST result
- explicit non-claims / exclusions
```

Always update:

```text
docs/analysis/129-rust-gap-backlog.md
```

---

## 11. Commit Template

Use:

```text
fix(<scope>): close <specific parity gap>

Explain C++ behavior, Rust gap, and why the Rust-only change preserves black-box replacement without adapting non-Rust modules.

Constraint: Modify only Rust yuanrong-functionsystem and repo docs
Constraint: Build/test parallelism capped at -j8 / CARGO_BUILD_JOBS=8
Rejected: Adapting upper-layer yuanrong/package/ST inputs | black-box replacement requires Rust to fit the existing contract
Confidence: <low|medium|high>
Scope-risk: <narrow|moderate|broad>
Directive: <future warning>
Tested: <Host targeted tests>
Tested: cargo check --workspace --lib --bins && git diff --check
Tested: Container yr-e2e-master /workspace/rust_current_fs ./run.sh build -j 8 && ./run.sh pack
Tested: Proof lane single-shot cpp ST deploy <deploy> => 111/111 passed
Not-tested: <honest exclusions/hardware blockers>
Signed-off-by: luozhancheng <luozhancheng@gmail.com>
```

Then:

```bash
git push origin rust-rewrite
```

---

## 12. Final Report Required

When done, report:

```text
- final commit IDs
- pushed branch
- closed backlog rows
- remaining explicit exclusions
- changed Rust files
- changed docs
- tests added
- Host preflight result
- Container build/pack logs
- artifact hashes
- proof deploy path
- ST evidence path
- final ST result
```

Do not claim full byte-for-byte C++ equivalence unless package inventory and behavior matrices actually prove it.
