# Final Release Surface Decision

Date: 2026-05-03
Branch: `rust-rewrite`
Scope: Subgoal F from `docs/analysis/161-production-control-plane-parity-ai-task.md`

## Purpose

Turn the accumulated release/package audits and production-control-plane proofs into a final decision record for what Rust FunctionSystem 0.8 is, and is not, allowed to claim as a black-box replacement of the official C++ FunctionSystem 0.8.0.

This document is the release-policy companion to:

- `docs/analysis/154-release-package-surface-audit.md`
- `docs/analysis/156-resource-group-full-parity-proof.md`
- `docs/analysis/158-snapshot-manager-protobuf-proof.md`
- `docs/analysis/162-metastore-lease-recovery-proof.md`
- `docs/analysis/164-iam-control-plane-proof.md`
- `docs/analysis/166-scheduler-policy-proof.md`

## Latest accepted evidence

Latest accepted FunctionSystem artifact hashes:

```text
38c3be2c175b508aaec73af4c4d86d7571494cd14e929c439c50a214cb2b425a  output/yr-functionsystem-v0.0.0.tar.gz
b61a6c8c2e4234144c0410085d3ddf47e00260479df39506d89037c509f925d1  output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
de824953efcdb0079fab2eb9e40711f5efb46e1e09cc14f907bf2de59d278607  output/metrics.tar.gz
```

Latest accepted upper-layer repack hashes (with unchanged upper-layer commands):

```text
ed194b3e66a49abbcfb464b0d84d9c6cf7601d4ec80f8657ccde47062dd14a39  output/openyuanrong-v0.0.1.tar.gz
3a63f3471cc006f81612d0298d9023ff9406308995866fa2f8b0872eb05e9683  output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

Latest accepted single-shot cpp ST evidence:

```text
deploy=/tmp/deploy/03140532
evidence=/tmp/deploy/03140532/driver/cpp_output.txt
[==========] Running 111 tests from 6 test cases.
[  PASSED  ] 111 tests.
```

## Decision summary

The final release claim is accepted at the **upper-layer lane** level and rejected at the **file-inventory identity** level.

Accepted claim:

```text
Rust FunctionSystem is accepted as a black-box replacement for the current official upper-layer
build / pack / install / single-shot ST lane, without changing the upper-layer commands,
artifact names, or proof-lane replacement procedure.
```

Rejected claims:

```text
Rust FunctionSystem is not claimed to be byte-for-byte identical to the C++ package.
Rust FunctionSystem is not claimed to be file-inventory identical for arbitrary external package consumers.
Rust FunctionSystem is not claimed to have full uncovered production-control-plane parity outside the bounded proofs.
```

## Drift category decisions

| Drift category | Current state | Decision | Reason |
| --- | --- | --- | --- |
| Top-level build/pack commands | `./run.sh build -j 8`, `./run.sh pack` unchanged | **Keep as accepted** | Current lane already proves compatibility without upper-layer changes |
| Upper-layer repack command | `bash scripts/package_yuanrong.sh -v v0.0.1` unchanged | **Keep as accepted** | Current lane repack succeeds unchanged |
| Single-shot cpp ST | `111/111 PASS` on latest proof deploy | **Keep as accepted** | This is the governing black-box acceptance lane |
| Artifact names | `yr-functionsystem-v0.0.0.tar.gz`, `openyuanrong_functionsystem-0.0.0-...whl`, `metrics.tar.gz` unchanged | **Keep as accepted** | Matches required delivery surface |
| Installed layout | Tar root/install layout remains compatible at active install path | **Keep as accepted** | No upper-layer adaptation required |
| Missing `libcrypto.so.3` / `libssl.so.3` | Present in clean C++ tar, absent in Rust tar | **Explicitly exclude from current black-box claim** | Hard rule forbids ABI-risky OpenSSL 3 shims/symlinks; current accepted lane does not require them |
| Missing `libyaml_tool.so` | Present in clean C++ tar, absent in Rust tar | **Explicitly exclude from current black-box claim** | Hard rule forbids fake shim without proven consumer/behavior contract |
| Rust-only extra files (including `functionsystem/bin/meta_store`) | Rust package is a compatible superset, not a minimal clone | **Keep as compatible superset** | Current accepted lane tolerates the extras; no proof that shrinking inventory is required |
| Wheel identity metadata | Name/version/tag/top-level package stay compatible | **Keep as accepted** | No wheel-identity drift on current lane |
| Wheel/tar payload inventory | Direct comparison still shows C++-minus-Rust and Rust-minus-C++ drift | **Do not claim identity** | Direct comparison disproves inventory identity today |

## Production-control-plane release boundary

The branch now has bounded proofs for:

1. resource-group fail-fast boundary,
2. snapshot query/list protobuf route closure,
3. metastore lease-backup recovery/watch closure,
4. IAM legacy token route/header/JWT-shape closure,
5. scheduler `NotExist` JSON selector completeness plus `labels.zone` failure-domain fallback proof.

The branch does **not** yet prove full parity for:

1. master-side resource-group manager state machine and bundle/sync/recover behavior,
2. snapshot delete/restore/watch/full stored-metadata semantics,
3. metastore requested-ID custom RPC, keepalive stream, and broader watch/revision shaping,
4. IAM AK/SK encrypted-body parity, refresh-route parity, external IdP parity,
5. scheduler weighted affinity/anti-affinity, taints/tolerations, migration/preemption breadth, domain-group control parity, underlayer scheduler manager parity,
6. file-inventory identity for arbitrary external package consumers.

These are not hidden risks; they are explicit release-scope boundaries.

## Final policy

1. **Do not** add OpenSSL 3 symlinks or substitute ABI-risky shims.
2. **Do not** add a fake `libyaml_tool.so` only to satisfy a file listing.
3. **Do not** remove Rust-only package entries unless a real consumer requires minimal identity and the removal is source-backed and re-proved.
4. **Do not** market current proof as blanket “all C++ parity complete”.
5. **Do** market the replacement claim at the proven upper-layer command/package/ST lane level only.

## Final release statement

```text
Release decision: GO for the proven upper-layer black-box lane, with explicit package-inventory
and uncovered control-plane boundaries retained in the release claim.
```

That means:

- acceptable to replace official C++ FunctionSystem in the currently proved build/pack/install/single-shot-ST path,
- not acceptable to claim byte identity, inventory identity, or full uncovered production-control-plane parity beyond the bounded proofs above.
