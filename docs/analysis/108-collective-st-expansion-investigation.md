# Collective ST Expansion Investigation

Date: 2026-04-26
Branch: `rust-rewrite`
Container: `yr-e2e-master`
Proof root: `/workspace/proof_source_replace_0_8`

## Goal

Close the remaining 8 collective ST cases after the filtered 104 source-replacement suite is green.

Current collective cases:

```text
CollectiveTest.InvalidGroupNameTest
CollectiveTest.InitGroupInActorTest
CollectiveTest.CreateGroupInDriverTest
CollectiveTest.ReduceTest
CollectiveTest.SendRecvTest
CollectiveTest.AllGatherTest
CollectiveTest.BroadcastTest
CollectiveTest.ScatterTest
```

## Current result

Command:

```bash
ROOT=/workspace/proof_source_replace_0_8
COLLECTIVE="CollectiveTest.InvalidGroupNameTest:CollectiveTest.InitGroupInActorTest:CollectiveTest.CreateGroupInDriverTest:CollectiveTest.ReduceTest:CollectiveTest.SendRecvTest:CollectiveTest.AllGatherTest:CollectiveTest.BroadcastTest:CollectiveTest.ScatterTest"
cd "$ROOT/src/yuanrong/test/st"
bash test.sh -b -l cpp -f "$COLLECTIVE" 2>&1 | tee "$ROOT/logs/source_replace_collective_8_initial.log"
```

Result:

```text
8 failed / 0 passed / no timeout
Deploy: /tmp/deploy/26083700
Log: /workspace/proof_source_replace_0_8/logs/source_replace_collective_8_initial.log
```

Failure summary:

```text
CollectiveTest.InvalidGroupNameTest: expected duplicate group error 1001, but no exception was thrown.
CollectiveTest.InitGroupInActorTest: invalid collective group backend return obj id
CollectiveTest.CreateGroupInDriverTest: invalid collective group backend return obj id
CollectiveTest.ReduceTest: invalid collective group backend return obj id
CollectiveTest.SendRecvTest: invalid collective group backend return obj id
CollectiveTest.AllGatherTest: invalid collective group backend return obj id
CollectiveTest.BroadcastTest: invalid collective group backend return obj id
CollectiveTest.ScatterTest: invalid collective group backend return obj id
```

Representative runtime exception:

```text
ErrCode: 2002, ModuleCode: 10, ErrMsg: failed to invoke &CollectiveActor::Reduce,
exception: ErrCode: 1001, ModuleCode: 20, ErrMsg: invalid collective group backend return obj id is: <request-id-like>0000
```

## Evidence collected

The proof lane runtime build was not a Gloo-enabled runtime profile.

`build.sh -h` in upper-layer `yuanrong` shows the official option:

```text
-G enable gloo collective operations (default: disabled)
-U enable UCC collective operations (default: disabled)
```

`build.sh` wires `-G` into Bazel as:

```text
--define ENABLE_GLOO=${ENABLE_GLOO}
```

String scan of the failing deploy tree found no Gloo runtime service/library payload:

```bash
find /tmp/deploy/26083700 -type f \( -path '*/lib/*' -o -path '*/runtime/service/*' \) \
  | while read -r f; do strings "$f" 2>/dev/null | grep -qi gloo && echo "$f"; done
```

Output:

```text
<empty>
```

The failure therefore cannot yet be assigned to Rust proxy behavior. The current package is missing the official collective runtime build profile.

## Boundary decision

Building runtime with `-G` is an official upstream build-profile choice, not a non-Rust source patch. It is allowed for collective acceptance as long as:

1. No runtime/datasystem/yuanrong source files are patched to make Rust pass.
2. The same official `build.sh`/`package_yuanrong.sh` command family is used.
3. Build parallelism remains `-j8` or lower.
4. The final ST command remains the official single-shot `bash test.sh -b -l cpp` form.

## Gloo rebuild experiment

The upper-layer runtime/package was rebuilt in the proof lane with the official Gloo build profile. No non-Rust source
patches were made.

Command:

```bash
ROOT=/workspace/proof_source_replace_0_8
cd "$ROOT/src/yuanrong"
bash build.sh -P -G -j 8
```

Build result:

```text
exit 0
Log: /workspace/proof_source_replace_0_8/logs/runtime_gloo_build_latest.log
Evidence: build compiled api/cpp/src/collective/collective.cpp and gloo/*.cc
```

Then the 8 collective cases were rerun:

```bash
ROOT=/workspace/proof_source_replace_0_8
COLLECTIVE="CollectiveTest.InvalidGroupNameTest:CollectiveTest.InitGroupInActorTest:CollectiveTest.CreateGroupInDriverTest:CollectiveTest.ReduceTest:CollectiveTest.SendRecvTest:CollectiveTest.AllGatherTest:CollectiveTest.BroadcastTest:CollectiveTest.ScatterTest"
cd "$ROOT/src/yuanrong/test/st"
bash test.sh -b -l cpp -f "$COLLECTIVE"
```

Result:

```text
7 passed / 1 failed / no timeout
Deploy: /tmp/deploy/26090450
Log: /workspace/proof_source_replace_0_8/logs/source_replace_collective_8_after_gloo.log
```

New pass after `-G`:

```text
CollectiveTest.InitGroupInActorTest
CollectiveTest.CreateGroupInDriverTest
CollectiveTest.ReduceTest
CollectiveTest.SendRecvTest
CollectiveTest.AllGatherTest
CollectiveTest.BroadcastTest
CollectiveTest.ScatterTest
```

Remaining failure:

```text
CollectiveTest.InvalidGroupNameTest
```

The remaining failure is the duplicate `CreateCollectiveGroup(spec3, ...)` expectation:

```text
Expected: throw YR::Exception code 1001 containing "already existed, please destroy it first"
Actual:   throws nothing
```

## C++ functionsystem control check

To decide whether the remaining failure is Rust-owned, the proof lane was repacked with the clean official C++
functionsystem tar/whl while keeping the same `-G` runtime package. No source files were patched.

Control artifacts copied from:

```text
/workspace/clean_0_8/src/yuanrong-functionsystem/output/yr-functionsystem-v0.0.0.tar.gz
/workspace/clean_0_8/src/yuanrong-functionsystem/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
```

Control command:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "CollectiveTest.InvalidGroupNameTest"
```

Control result:

```text
0 passed / 1 failed
Deploy: /tmp/deploy/26090853
Log: /workspace/proof_source_replace_0_8/logs/cpp_fs_control_invalid_group_after_gloo.log
Failure: same duplicate CreateCollectiveGroup call throws nothing
```

After the control run, the Rust functionsystem artifacts were restored and repackaged:

```text
Restored Rust yr-functionsystem hash:
a9bdbcf074dd88ddac3cca8615a04bcf211b114cc07e9908fa281f46cece1e2b
```

## Current conclusion

The collective expansion now has two separate results:

1. `-G` is required for the collective runtime profile. It moves Rust source replacement from 0/8 to 7/8 collective
   cases passing.
2. The remaining `InvalidGroupNameTest` failure reproduces with the clean C++ functionsystem under the same `-G`
   runtime package, so it is not evidence of a Rust functionsystem gap.

Therefore the Rust-owned source-replacement status is effectively:

```text
Filtered non-collective ST: 104/104 pass
Collective under official -G runtime profile: 7/8 pass
Remaining collective duplicate-group check: also fails with clean C++ functionsystem control
```

## If the remaining duplicate-group failure must be closed

Do not patch ST or runtime. If someone still wants to close `InvalidGroupNameTest`, compare these paths first:

1. `YR::KVManager::Set(..., YR::ExistenceOpt::NX)` in `api/cpp/src/collective/collective.cpp`.
2. Datasystem write-mode semantics for duplicate `collective-group-*` keys.
3. Whether this is an upstream SDK/DS behavior mismatch rather than functionsystem behavior.
4. Only if the clean C++ functionsystem passes in a stricter control lane should Rust functionsystem be changed.
