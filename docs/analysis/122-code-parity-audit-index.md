# Rust/C++ 0.8 Code Parity Audit Index

Date: 2026-04-28
Branch: `rust-rewrite`
Mode: code-level audit, not implementation.

## Purpose

The current Rust source-replacement lane has strong ST evidence, but the project goal is broader: Rust `yuanrong-functionsystem` must behave as a black-box replacement for official C++ `yuanrong-functionsystem` 0.8.0 while keeping upper-layer `yuanrong` build, pack, install, directory layout, and test commands unchanged.

Therefore this audit goes deeper than ST. It compares C++ and Rust source module by module to find:

- C++ behavior that Rust fully implements.
- C++ behavior that Rust accepts at the interface but ignores or weakens.
- C++ behavior that current ST does not exercise.
- Rust implementation shortcuts that are acceptable only under a documented release policy.

## Constitution

These rules are audit constraints, not preferences.

1. Only Rust `yuanrong-functionsystem` may be changed in later hardening work.
2. Do not change upper-layer `yuanrong`, runtime, datasystem, ST scripts, or clean C++ control to adapt to Rust.
3. C++ 0.8.0 source is the functional reference.
4. ST green is evidence, not proof of feature completeness.
5. Single-shot `bash test.sh -b -l cpp` is acceptance; `test.sh -s -r` is debug-only.
6. Build parallelism must stay at `-j8` / `CARGO_BUILD_JOBS=8`.
7. Do not guess. If a behavior is unclear, compare C++ code/logs first and classify as unproven.
8. Audit first, patch later. This series records gaps before choosing fixes.

## Reference inputs

| Surface | Location |
| --- | --- |
| Rust source | `/home/lzc/workspace/code/yr_rust/yuanrong-functionsystem` |
| C++ source reference | `yr-e2e-master:/workspace/clean_0_8/src/yuanrong-functionsystem` |
| Rust ST proof lane | `/workspace/proof_source_replace_0_8` |
| Current accepted ST proof | `docs/analysis/120-r4-layout-st-proof.md` |
| Risk register before deep audit | `docs/analysis/114-blackbox-risk-register.md` |

## Existing evidence chain

Read these before using this audit series:

- `docs/analysis/111-cpp-rust-blackbox-parity-audit-plan.md`
- `docs/analysis/112-cpp-rust-interface-surface-matrix.md`
- `docs/analysis/113-cpp-rust-behavior-parity-review.md`
- `docs/analysis/114-blackbox-risk-register.md`
- `docs/analysis/115-proto-compatibility-restoration.md`
- `docs/analysis/116-binary-flag-parity-gate.md`
- `docs/analysis/117-state-persistence-parity.md`
- `docs/analysis/118-r3-state-store-st-proof.md`
- `docs/analysis/119-r4-package-layout-closure.md`
- `docs/analysis/120-r4-layout-st-proof.md`
- `docs/analysis/121-state-and-proxy-kill-hardening-proof.md`

Those documents prove the current ST/source-replacement lane. They do not prove full module parity.

## New deep-audit documents

| Doc | Scope | Status |
| --- | --- | --- |
| `docs/analysis/123-common-proto-config-parity-audit.md` | common service metadata, config, flags, SSL, NUMA, KV, YAML tooling | first pass complete |
| `docs/analysis/124-function-proxy-parity-audit.md` | proxy stream mapping, IAM, rate limit, group/range, state, scheduler handoff | first pass complete |
| `docs/analysis/125-function-agent-runtime-manager-parity-audit.md` | function_agent and runtime_manager | planned |
| `docs/analysis/126-master-scheduler-domain-parity-audit.md` | function_master and domain_scheduler | planned |
| `docs/analysis/127-metastore-iam-parity-audit.md` | meta_store and iam_server | planned |
| `docs/analysis/128-release-surface-parity-audit.md` | package layout, binaries, libraries, scripts, version names | planned |
| `docs/analysis/129-rust-gap-backlog.md` | prioritized implementation/test backlog | started |

## Classification vocabulary

| Class | Meaning |
| --- | --- |
| `Equivalent` | Rust appears behavior-compatible by direct source comparison or stronger evidence. |
| `ST verified` | Official source-replacement ST exercises the behavior. |
| `Unit verified` | Rust tests lock the intended compatibility behavior. |
| `Parse-compatible` | Rust accepts a C++ flag/message/schema, but behavior is absent or partial. |
| `First-hop compatible` | Rust preserves the first handoff field/metadata but not the downstream C++ behavior tree. |
| `Needs test` | Rust may be correct, but no direct test or source evidence closes it. |
| `Needs implementation` | C++ exposes behavior that Rust appears to lack. |
| `Release-policy boundary` | Behavior/layout differs, but may be acceptable if release owners explicitly accept the policy. |
| `Control-failing` | Clean C++ fails the same scenario under the same profile, so Rust ownership is not assigned yet. |

## Priority vocabulary

| Priority | Meaning |
| --- | --- |
| `P0` | Official deploy/ST path, data correctness, crash/hang, or black-box contract likely broken. |
| `P1` | Likely production black-box path not covered by ST. |
| `P2` | Optional or advanced behavior; parse-only/weak compatibility risk. |
| `P3` | Release policy or intentionally deferred equivalence decision. |

## Current first-pass conclusion

Rust is credible for the current 0.8 ST source-replacement lane, but not yet proven as full C++ module parity. The first code-level pass found a pattern:

- Rust is strong on the main ST path: create/init/invoke/result/kill/recover/state and package handoff.
- Rust often accepts C++ flags for launch compatibility, but many advanced flags are not behavior-complete.
- Rust implements some C++ fields as first-hop metadata propagation, not full downstream behavior.
- C++ common/proxy includes validation, IAM, SSL, NUMA, group recovery/sync, and DS client semantics that ST barely touches.

Use `docs/analysis/129-rust-gap-backlog.md` as the live execution backlog after all module audits are filled in.
