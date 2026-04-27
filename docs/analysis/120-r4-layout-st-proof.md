# R4 Layout ST Proof

Date: 2026-04-27
Branch: `rust-rewrite`
Commit under test: `5795648 Tighten Rust package layout for blackbox release`
Container: `yr-e2e-master`
Proof root: `/workspace/proof_source_replace_0_8`
Deploy: `/tmp/deploy/27150146`

## Goal

Verify that the R4 release-layout tightening did not regress the current openYuanrong 0.8 Rust FunctionSystem black-box source-replacement target.

The acceptance lane remains the official upper-layer `yuanrong` single-shot ST flow. `test.sh -s -r` is not used as acceptance.

## Constitution followed

```text
Only Rust yuanrong-functionsystem source changed.
Do not patch upper-layer yuanrong, runtime, datasystem, ST scripts, or clean C++ control.
Build parallelism must not exceed -j8.
Official ST acceptance is single-shot test.sh -b -l cpp.
Exclude only CollectiveTest.InvalidGroupNameTest because the same -G profile clean C++ control fails it too.
Do not guess on failures; compare with C++ control before assigning Rust ownership.
```

## R4 packaging delta under test

R4 is packaging-only:

1. `scripts/executor/builder/build_rust.py`
   - Restores the packaged empty C++ metadata directory `functionsystem/lib/cmake/opentelemetry-cpp/`.
2. `scripts/executor/tasks/pack_task.py`
   - Excludes Rust-only release-gate helper scripts from `functionsystem/tools/`.
   - Keeps official runtime tooling such as `cluster_manager.py`.

No upper-layer `yuanrong`, runtime, datasystem, ST script, or C++ control source was changed for this proof.

## Build and package proof

Rust FunctionSystem was built and packed in:

```text
/workspace/rust_current_fs
```

Commands:

```bash
export CARGO_BUILD_JOBS=8
./run.sh build -j 8
./run.sh pack
```

Then the R4 artifacts were copied into the source-replacement proof lane and upper-layer `yuanrong` was repackaged through the unchanged packaging command:

```bash
ROOT=/workspace/proof_source_replace_0_8
SRC=/workspace/rust_current_fs/output
cd "$ROOT/src/yuanrong"
cp -f "$SRC/yr-functionsystem-v0.0.0.tar.gz" "$ROOT/src/yuanrong/output/yr-functionsystem-v0.0.0.tar.gz"
cp -f "$SRC/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl" \
  "$ROOT/src/yuanrong/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl"
bash scripts/package_yuanrong.sh -v v0.0.1
```

Package command log:

```text
/workspace/proof_source_replace_0_8/logs/package_yuanrong_r4_layout.log
```

Artifact hashes used by this proof:

```text
672159428522f0a1f40d04ddd08559368a5bbf090587909dd5be93d615af6738  yr-functionsystem-v0.0.0.tar.gz
b091d2e952c98dac84463e8521135a24a7afc0f89e035987ee9cdd77ae449458  openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
141db484a0cf5436dfc23ddeed08b4aef288f63d408ba1526e46dc50d6bcd8b9  openyuanrong-v0.0.1.tar.gz
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
[==========] Running 111 tests from 6 test cases.
[==========] 111 tests from 6 test cases ran. (291615 ms total)
[  PASSED  ] 111 tests.
YOU HAVE 20 DISABLED TESTS
```

Evidence files:

```text
/workspace/proof_source_replace_0_8/logs/source_replace_full_minus_invalid_group_r4_layout.log
/tmp/deploy/27150146/driver/cpp_output.txt
/workspace/proof_source_replace_0_8/logs/r4_artifacts.sha256
/workspace/proof_source_replace_0_8/logs/package_yuanrong_r4_layout.log
```

## Package comparison status

The R4 layout comparison is documented in `docs/analysis/119-r4-package-layout-closure.md`:

```text
cpp entries:        160
rust entries:       186
cpp minus rust:       1
rust minus cpp:      27
```

Only remaining C++-minus-Rust entry:

```text
functionsystem/lib/libyaml_tool.so
```

This is a release-policy boundary, not current ST evidence of a Rust runtime behavior gap. Rust parses service YAML directly with `serde_yaml`; it does not use the C++ `service_json.cpp` dlopen path that loads `libyaml_tool.so`.

## Conclusion

R4 package-layout tightening did not regress the current source-replacement acceptance baseline:

```text
Rust FunctionSystem source replacement remains 111/111 PASS for the current accepted suite.
```

The remaining nominal full-suite gap is still `CollectiveTest.InvalidGroupNameTest`, which also failed with clean C++ FunctionSystem under the same `-G` runtime profile. Do not treat it as Rust-owned unless a same-profile clean C++ control passes.
