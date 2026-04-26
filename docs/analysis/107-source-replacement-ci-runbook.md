# Source Replacement CI Runbook

Date: 2026-04-26
Branch: `rust-rewrite`
Scope: openYuanrong 0.8.0 with Rust `yuanrong-functionsystem` as a source-level black-box replacement.

## Constitution

These rules are acceptance-critical. Do not relax them in order to make a run green.

1. Replace only the `yuanrong-functionsystem` source implementation with the Rust repo.
2. Do not patch upper-layer `yuanrong`, runtime, datasystem, ST scripts, or C++ tests to adapt to Rust.
3. Keep upper-layer build, pack, install layout, and ST commands unchanged.
4. Build parallelism must not exceed `-j8`.
5. Official acceptance is the single-shot ST command. The two-step `test.sh -s -r` flow is debug-only.
6. When a failure appears, inspect logs and the official C++ behavior before changing Rust. Do not guess.
7. Collective ST is tracked separately until the runtime package is built with the official `-G` Gloo profile.

## Known proof lane

Host workspace:

```text
/home/lzc/workspace/code/yr_rust
```

Rust source repo:

```text
/home/lzc/workspace/code/yr_rust/yuanrong-functionsystem
```

Container:

```text
yr-e2e-master
```

Container proof root:

```text
/workspace/proof_source_replace_0_8
```

Important proof logs:

```text
/workspace/proof_source_replace_0_8/logs/source_replace_filtered_cpp_st_scoped_sequence.log
/workspace/proof_source_replace_0_8/logs/source_replace_collective_8_initial.log
```

## Sync Rust source into the proof lane

Run from the host. This copies the Rust implementation into the official 0.8.0 proof tree without changing the upper-layer `yuanrong` source tree.

```bash
HOST_REPO=/home/lzc/workspace/code/yr_rust/yuanrong-functionsystem
CONTAINER=yr-e2e-master
ROOT=/workspace/proof_source_replace_0_8

docker exec "$CONTAINER" bash -lc "mkdir -p '$ROOT/src'"
tar -C "$HOST_REPO" --exclude .git --exclude target --exclude output -cf - . \
  | docker exec -i "$CONTAINER" bash -lc "rm -rf '$ROOT/src/yuanrong-functionsystem' && mkdir -p '$ROOT/src/yuanrong-functionsystem' && tar -C '$ROOT/src/yuanrong-functionsystem' -xf -"
```

## Build and package Rust functionsystem

Run inside the container. Keep `-j8` or lower.

```bash
ROOT=/workspace/proof_source_replace_0_8
cd "$ROOT/src/yuanrong-functionsystem"
./run.sh build -j 8
./run.sh pack
```

Expected functionsystem artifacts:

```text
$ROOT/src/yuanrong-functionsystem/output/yr-functionsystem-v0.0.0.tar.gz
$ROOT/src/yuanrong-functionsystem/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
$ROOT/src/yuanrong-functionsystem/output/metrics.tar.gz
```

Expected functionsystem binary layout inside `yr-functionsystem-v0.0.0.tar.gz`:

```text
functionsystem/bin/domain_scheduler
functionsystem/bin/function_agent
functionsystem/bin/function_master
functionsystem/bin/function_proxy
functionsystem/bin/iam_server
functionsystem/bin/meta_service
functionsystem/bin/meta_store
functionsystem/bin/runtime_manager
functionsystem/bin/yr
```

## Repack upper-layer openYuanrong without changing commands

Run inside the container.

```bash
ROOT=/workspace/proof_source_replace_0_8
cd "$ROOT/src/yuanrong"

cp -f "$ROOT/src/yuanrong-functionsystem/output/yr-functionsystem-v0.0.0.tar.gz" \
  "$ROOT/src/yuanrong/output/yr-functionsystem-v0.0.0.tar.gz"
cp -f "$ROOT/src/yuanrong-functionsystem/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl" \
  "$ROOT/src/yuanrong/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl"

bash scripts/package_yuanrong.sh -v v0.0.1
```

Expected top-level artifacts:

```text
$ROOT/src/yuanrong/output/openyuanrong-v0.0.1.tar.gz
$ROOT/src/yuanrong/output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
$ROOT/src/yuanrong/output/yr-functionsystem-v0.0.0.tar.gz
```

The version string mismatch is upstream packaging behavior in this proof lane. It is not used as a byte-for-byte acceptance condition in this stage.

## Official filtered acceptance

The current green source-replacement target is all official C++ ST except the 8 collective tests.

```bash
ROOT=/workspace/proof_source_replace_0_8
FILTER="*-CollectiveTest.InvalidGroupNameTest:CollectiveTest.InitGroupInActorTest:CollectiveTest.CreateGroupInDriverTest:CollectiveTest.ReduceTest:CollectiveTest.SendRecvTest:CollectiveTest.AllGatherTest:CollectiveTest.BroadcastTest:CollectiveTest.ScatterTest"
cd "$ROOT/src/yuanrong/test/st"
bash test.sh -b -l cpp -f "$FILTER"
```

Green evidence from 2026-04-26:

```text
Result: ----------------------Success to run cpp st----------------------
Deploy: /tmp/deploy/26075526
Log: /workspace/proof_source_replace_0_8/logs/source_replace_filtered_cpp_st_scoped_sequence.log
```

## Debug-only start flow

This flow is useful only when manually inspecting deployment environment variables. It is not acceptance.

```bash
bash test.sh -s -r
# export the printed env only for that deployment
```

Do not follow it with `bash test.sh -b -l cpp` as an acceptance run. The latter deploys a new cluster and does not reuse the reserved one.

## Collective expansion command

After the runtime package is rebuilt with official Gloo support (`bash build.sh ... -G ...` in the upper-layer `yuanrong` repo), run only the 8 collective cases first:

```bash
ROOT=/workspace/proof_source_replace_0_8
COLLECTIVE="CollectiveTest.InvalidGroupNameTest:CollectiveTest.InitGroupInActorTest:CollectiveTest.CreateGroupInDriverTest:CollectiveTest.ReduceTest:CollectiveTest.SendRecvTest:CollectiveTest.AllGatherTest:CollectiveTest.BroadcastTest:CollectiveTest.ScatterTest"
cd "$ROOT/src/yuanrong/test/st"
bash test.sh -b -l cpp -f "$COLLECTIVE"
```

If these pass under the Gloo runtime profile, expand final acceptance from filtered 104 to the full 112 C++ ST suite. If they still fail, inspect the C++ collective control behavior and Rust forwarding logs before changing Rust.

Current 2026-04-26 status:

```text
Runtime/package rebuilt with: bash build.sh -P -G -j 8
Rust functionsystem collective result: 7/8 passed, only CollectiveTest.InvalidGroupNameTest failed.
C++ functionsystem control under the same -G runtime: CollectiveTest.InvalidGroupNameTest also failed the same way.
Conclusion: the remaining collective duplicate-group check is not currently Rust-owned.
Rust-owned effective ST coverage: 104/104 filtered non-collective + 7/7 collective cases that pass the -G control profile.
```

## Minimal diagnostics

```bash
# Check container state
docker ps --filter name=yr-e2e-master --format 'table {{.Names}}\t{{.Status}}\t{{.Image}}'

# Check final proof success
docker exec yr-e2e-master bash -lc 'grep -n "Success to run cpp st\|Failed to run cpp st" /workspace/proof_source_replace_0_8/logs/source_replace_filtered_cpp_st_scoped_sequence.log'

# Check collective failures
docker exec yr-e2e-master bash -lc 'grep -n "\[  FAILED  \]\|invalid collective group backend\|already existed" /workspace/proof_source_replace_0_8/logs/source_replace_collective_8_initial.log'

# Check runtime Gloo strings in a deploy tree
docker exec yr-e2e-master bash -lc 'find /tmp/deploy/26083700 -type f \( -path "*/lib/*" -o -path "*/runtime/service/*" \) | while read -r f; do strings "$f" 2>/dev/null | grep -qi gloo && echo "$f"; done'
```
