# Rust Source Replacement Final 111/112 ST Proof

Date: 2026-04-26
Branch: `rust-rewrite`
Container: `yr-e2e-master`
Proof root: `/workspace/proof_source_replace_0_8`
Runtime profile: official upper-layer `yuanrong` build with `-G` Gloo enabled

## Goal

Prove the current practical end state for openYuanrong 0.8.0 Rust `yuanrong-functionsystem` source-level black-box replacement:

1. Upper-layer `yuanrong` build, package, install layout, and ST command remain unchanged.
2. Only the `yuanrong-functionsystem` implementation is replaced by the Rust repo.
3. Non-Rust source is not patched to make Rust pass.
4. Official single-shot ST is used for acceptance.

## Constitution

These constraints were enforced during this proof:

```text
Do not modify non-Rust source to make Rust pass.
Do not modify ST scripts or test cases.
Do not treat `test.sh -s -r` as acceptance; it is debug-only.
Build parallelism must not exceed -j8.
Use C++ control evidence before assigning a failure to Rust.
```

## Build and package evidence

The upper-layer runtime/package was rebuilt with the official collective profile:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong
bash build.sh -P -G -j 8
```

Build result:

```text
exit 0
Log: /workspace/proof_source_replace_0_8/logs/runtime_gloo_build_latest.log
```

The Rust functionsystem artifact was restored after the C++ control experiment and repackaged into the upper-layer output.

Rust functionsystem handoff hash:

```text
a9bdbcf074dd88ddac3cca8615a04bcf211b114cc07e9908fa281f46cece1e2b  yr-functionsystem-v0.0.0.tar.gz
```

Aggregate package hash used by the final ST run:

```text
1daebe68f7b776e31b28b13036676d2500f61660da0ef42fa986e915e13dc3ea  openyuanrong-v0.0.1.tar.gz
```

## Final single-shot ST command

Only the one case that also fails with the clean C++ functionsystem control is excluded:

```bash
ROOT=/workspace/proof_source_replace_0_8
cd "$ROOT/src/yuanrong/test/st"
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
```

Result:

```text
----------------------Success to run cpp st----------------------
[  PASSED  ] 111 tests.
```

Evidence:

```text
Deploy: /tmp/deploy/26095206
Full command log: /workspace/proof_source_replace_0_8/logs/source_replace_full_minus_invalid_group_after_gloo.log
GTest output: /tmp/deploy/26095206/driver/cpp_output.txt
```

## Why one test is excluded

`CollectiveTest.InvalidGroupNameTest` expects the second driver-side call to:

```cpp
YR::Collective::CreateCollectiveGroup(spec3, {ins.GetInstanceId()}, {0})
```

to throw:

```text
ErrCode 1001, message contains "already existed, please destroy it first"
```

Under the same `-G` runtime package, this failure reproduces with the clean official C++ functionsystem package:

```text
Deploy: /tmp/deploy/26090853
Log: /workspace/proof_source_replace_0_8/logs/cpp_fs_control_invalid_group_after_gloo.log
Failure: same duplicate CreateCollectiveGroup call throws nothing
```

Therefore this single remaining failure is not currently evidence of a Rust functionsystem gap. It should be handled as an upstream SDK/DS semantics question unless a stricter clean C++ control lane passes it.

## Effective closure status

```text
Filtered non-collective baseline: 104/104 pass
Collective with official -G runtime profile: 7/8 pass
Final single-shot ST excluding only C++-control-failing InvalidGroupNameTest: 111/111 pass
Full nominal suite: 111/112 pass, with the remaining 1 also failing under clean C++ functionsystem control
```

## Next ownership boundary

Do not patch Rust functionsystem for `InvalidGroupNameTest` unless a clean C++ control lane passes the same test under the same runtime/DS profile.

If that test must be closed upstream, investigate:

1. `YR::KVManager::Set(..., YR::ExistenceOpt::NX)` semantics.
2. Datasystem duplicate-key write behavior for `collective-group-*` keys.
3. Whether the ST expectation or SDK exception mapping is stale against the current DS/runtime implementation.
