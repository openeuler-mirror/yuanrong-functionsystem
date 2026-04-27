# R3 State Store ST Proof

Date: 2026-04-27
Branch: `rust-rewrite`
Commit under test: `7512da5 Persist Rust proxy checkpoint state across memory loss`
Container: `yr-e2e-master`
Proof root: `/workspace/proof_source_replace_0_8`
Deploy: `/tmp/deploy/27144507`

## Goal

Verify that the R3 state persistence parity change does not regress the proven Rust source-replacement ST baseline.

The acceptance lane remains the official upper-layer `yuanrong` single-shot ST flow. `test.sh -s -r` was not used as acceptance.

## Constitution followed

```text
Only Rust yuanrong-functionsystem source changed.
Do not patch upper-layer yuanrong, runtime, datasystem, ST scripts, or clean C++ control.
Build parallelism must not exceed -j8.
Official ST acceptance is single-shot test.sh -b -l cpp.
Exclude only CollectiveTest.InvalidGroupNameTest because the same -G profile clean C++ control fails it too.
```

## Build / package proof

R3 functionsystem was built and packed in:

```text
/workspace/rust_current_fs
```

Commands:

```bash
export CARGO_BUILD_JOBS=8
./run.sh build -j 8
./run.sh pack
```

Results:

```text
Build function-system successfully in 39.84 seconds
output/yr-functionsystem-v0.0.0.tar.gz
output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
output/metrics.tar.gz
```

The latest Rust functionsystem artifacts were copied into the proof lane and upper-layer `yuanrong` was repacked without changing commands:

```bash
ROOT=/workspace/proof_source_replace_0_8
SRC=/workspace/rust_current_fs/output
cd "$ROOT/src/yuanrong"
cp -f "$SRC/yr-functionsystem-v0.0.0.tar.gz" "$ROOT/src/yuanrong/output/yr-functionsystem-v0.0.0.tar.gz"
cp -f "$SRC/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl" \
  "$ROOT/src/yuanrong/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl"
bash scripts/package_yuanrong.sh -v v0.0.1
```

Artifact hashes after repack:

```text
ec929d5823baa1f7c44b77dca6dc3e561c7e553cc9feb5ac4c11691e3a158d7b  yr-functionsystem-v0.0.0.tar.gz
8c3643c11e09bcc56e6eec668bef3bca75bb1de3630d9b9f88ab9c0bc53ee377  openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
6304c886241fdef94b43d835e0bb5c76da9398e3e7b30a303bd30ed71bfd4536  openyuanrong-v0.0.1.tar.gz
3a63f3471cc006f81612d0298d9023ff9406308995866fa2f8b0872eb05e9683  openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

## ST command

```bash
ROOT=/workspace/proof_source_replace_0_8
cd "$ROOT/src/yuanrong/test/st"
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
```

## Result

```text
----------------------Success to run cpp st----------------------
[==========] 111 tests from 6 test cases ran. (231527 ms total)
[  PASSED  ] 111 tests.
YOU HAVE 20 DISABLED TESTS
The cmd .../yrapicpp --gtest_filter=*-CollectiveTest.InvalidGroupNameTest executed cost 232 seconds
```

Evidence files:

```text
/workspace/proof_source_replace_0_8/logs/source_replace_full_minus_invalid_group_r3_state_store.log
/tmp/deploy/27144507/driver/cpp_output.txt
/workspace/proof_source_replace_0_8/logs/r3_artifacts.sha256
/workspace/proof_source_replace_0_8/logs/package_yuanrong_r3_state_store.log
```

## Conclusion

R3 state persistence parity did not regress the current source-replacement acceptance baseline:

```text
Rust functionsystem source replacement remains 111/111 PASS for the current accepted suite.
```

The remaining nominal full-suite gap is still `CollectiveTest.InvalidGroupNameTest`, which also failed with clean C++ functionsystem under the same `-G` runtime profile. Do not treat it as Rust-owned unless a same-profile clean C++ control passes.

## Still open

- R4 package layout/minimality policy and implementation audit.
- Optional process-kill proxy restart state persistence ST.
- Optional exact DS cache backend parity if release owners require internal backend equivalence rather than black-box durability.
