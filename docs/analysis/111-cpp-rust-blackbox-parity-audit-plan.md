# C++/Rust Blackbox Parity Audit Plan

Date: 2026-04-27
Branch: `rust-rewrite`
Scope: openYuanrong 0.8.0 `yuanrong-functionsystem` C++ control vs Rust source-replacement implementation.

## Why this audit exists

The source-replacement ST target is already green for the current Rust-owned scope: 111 tests pass when only `CollectiveTest.InvalidGroupNameTest` is excluded, and that excluded test also fails under the same `-G` runtime profile with the clean C++ functionsystem control.

ST is still not a full behavioral proof. This audit reviews whether Rust is a credible black-box replacement beyond the exercised ST paths by comparing the external contract surfaces of:

- C++ control: `/workspace/clean_0_8/src/yuanrong-functionsystem`
- Rust implementation: `/home/lzc/workspace/code/yr_rust/yuanrong-functionsystem`
- Rust proof lane: `/workspace/proof_source_replace_0_8`

## Constitution

These rules are part of the audit target, not optional preferences.

1. Do not modify non-Rust source to make Rust pass.
2. Do not modify upper-layer `yuanrong`, runtime, datasystem, ST scripts, or C++ tests for Rust compatibility.
3. Keep build, pack, install layout, and ST command flow unchanged after the initial source swap.
4. Build and pack parallelism must stay at `-j8` or lower.
5. Single-shot `bash test.sh -b -l cpp` is acceptance; `test.sh -s -r` is debug-only.
6. Do not guess on failures; compare C++ code/control logs before assigning ownership.
7. Read-only audit comes before any hardening implementation.

## Evidence already accepted

Final source-replacement proof:

```text
Command: bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
Result:  [  PASSED  ] 111 tests
Deploy:  /tmp/deploy/26095206
Log:     /workspace/proof_source_replace_0_8/logs/source_replace_full_minus_invalid_group_after_gloo.log
Doc:     docs/analysis/110-source-replacement-final-111-proof.md
```

C++ control for the one excluded test:

```text
Deploy: /tmp/deploy/26090853
Log:    /workspace/proof_source_replace_0_8/logs/cpp_fs_control_invalid_group_after_gloo.log
Result: same duplicate CreateCollectiveGroup expectation fails under clean C++ functionsystem.
```

## Audit method

The audit compares contract surfaces, not line-by-line implementation details.

1. Artifact and installed-layout surface.
2. Binary names and command-line/config surface.
3. Protobuf/gRPC wire surface.
4. Runtime stream message handling.
5. Instance lifecycle and state-machine semantics.
6. Etcd/metastore key/value schema.
7. Datasystem/KV/object interaction boundaries.
8. Ordering, concurrency, recovery, and duplicate/idempotent handling.
9. Test coverage and known untested risk.

## Classification vocabulary

- `Equivalent`: Rust appears contract-equivalent to C++ by direct source or artifact comparison.
- `ST verified`: Official source-replacement ST directly exercises this path.
- `Unit verified`: Rust tests cover the intended compatibility behavior.
- `Control-failing`: Clean C++ fails the same scenario under the same profile; not Rust-owned yet.
- `Needs test`: Risk is plausible but no current test directly protects it.
- `Needs implementation`: Rust lacks a contract surface that C++ exposes and callers may rely on.
- `Out of current scope`: Not required for the current 0.8 ST target but relevant before broader release.

## Done criteria

This audit is complete when:

1. C++ external behavior surfaces are listed with source evidence.
2. Rust corresponding implementation surfaces are mapped with file evidence.
3. Every observed gap is classified by risk and ownership.
4. High-risk unknowns are explicit and not hidden behind green ST.
5. Follow-up work can be executed by another AI without rediscovering the same evidence.
