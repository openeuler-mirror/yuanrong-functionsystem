# Release Artifact Audit

Date: 2026-04-26
Branch: `rust-rewrite`
Container: `yr-e2e-master`
Proof root: `/workspace/proof_source_replace_0_8`
Audit output: `/tmp/release_audit_26075526`

## Goal

Verify that Rust `yuanrong-functionsystem` can be delivered as a black-box source replacement without changing upper-layer artifact names, package handoff points, or installed functionsystem layout.

This audit is layout/name compatibility, not byte-for-byte equivalence.

## Artifact names

Rust functionsystem output:

```text
/workspace/proof_source_replace_0_8/src/yuanrong-functionsystem/output/yr-functionsystem-v0.0.0.tar.gz
/workspace/proof_source_replace_0_8/src/yuanrong-functionsystem/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
/workspace/proof_source_replace_0_8/src/yuanrong-functionsystem/output/metrics.tar.gz
```

Upper-layer `yuanrong` output after `bash scripts/package_yuanrong.sh -v v0.0.1`:

```text
/workspace/proof_source_replace_0_8/src/yuanrong/output/openyuanrong-v0.0.1.tar.gz
/workspace/proof_source_replace_0_8/src/yuanrong/output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
/workspace/proof_source_replace_0_8/src/yuanrong/output/yr-functionsystem-v0.0.0.tar.gz
```

The `openyuanrong-0.7.0.dev0` wheel name is existing upper-layer packaging behavior in this proof lane. Current acceptance does not require byte-for-byte version-string normalization.

## Hashes

```text
a9bdbcf074dd88ddac3cca8615a04bcf211b114cc07e9908fa281f46cece1e2b  yr-functionsystem-v0.0.0.tar.gz
778633d176ca5af8278a9cc7df734d6cce8f90f73b8218c31ed81ca002783fd2  openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
97a217ecdd8077d90031011e5bea2b3a677984a15fd7e0eda6ca873bf25aa5a0  metrics.tar.gz

d493cefbfc8541f42eb2d30db9328874de0e459e5930abce139a152d085b988b  openyuanrong-v0.0.1.tar.gz
1686c4963e152dd284882352931aaef14af0e0370cd0196e259737ebe4447cd6  openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
a9bdbcf074dd88ddac3cca8615a04bcf211b114cc07e9908fa281f46cece1e2b  yr-functionsystem-v0.0.0.tar.gz
```

The `yr-functionsystem-v0.0.0.tar.gz` hash is identical in the functionsystem output and the upper-layer `yuanrong/output` handoff location.

After rebuilding the upper-layer runtime/package with the official `-G` collective profile and restoring the Rust
functionsystem artifact, the functionsystem handoff hash stayed unchanged while the aggregate openYuanrong package
hashes changed as expected:

```text
Audit output: /tmp/release_audit_after_gloo_26090450

a9bdbcf074dd88ddac3cca8615a04bcf211b114cc07e9908fa281f46cece1e2b  yr-functionsystem-v0.0.0.tar.gz
778633d176ca5af8278a9cc7df734d6cce8f90f73b8218c31ed81ca002783fd2  openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
97a217ecdd8077d90031011e5bea2b3a677984a15fd7e0eda6ca873bf25aa5a0  metrics.tar.gz

1daebe68f7b776e31b28b13036676d2500f61660da0ef42fa986e915e13dc3ea  openyuanrong-v0.0.1.tar.gz
3a63f3471cc006f81612d0298d9023ff9406308995866fa2f8b0872eb05e9683  openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
a9bdbcf074dd88ddac3cca8615a04bcf211b114cc07e9908fa281f46cece1e2b  yr-functionsystem-v0.0.0.tar.gz
```

## Functionsystem tar layout

Top-level directory:

```text
functionsystem/
```

Expected binaries present:

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

Embedded layout inside `openyuanrong-v0.0.1.tar.gz`:

```text
openyuanrong/functionsystem/bin/domain_scheduler
openyuanrong/functionsystem/bin/function_agent
openyuanrong/functionsystem/bin/function_master
openyuanrong/functionsystem/bin/function_proxy
openyuanrong/functionsystem/bin/iam_server
openyuanrong/functionsystem/bin/meta_service
openyuanrong/functionsystem/bin/meta_store
openyuanrong/functionsystem/bin/runtime_manager
openyuanrong/functionsystem/bin/yr
```

## Audit commands

```bash
ROOT=/workspace/proof_source_replace_0_8
OUT=/tmp/release_audit_26075526
rm -rf "$OUT" && mkdir -p "$OUT"

cd "$ROOT/src/yuanrong-functionsystem/output"
sha256sum yr-functionsystem-v0.0.0.tar.gz \
  openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl \
  metrics.tar.gz > "$OUT/functionsystem.sha256"
tar -tzf yr-functionsystem-v0.0.0.tar.gz | sort > "$OUT/yr-functionsystem.list"
tar -tzf metrics.tar.gz | sort > "$OUT/metrics.list"
python3 - <<'PY' > "$OUT/fs-wheel.list"
import zipfile
p = "openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl"
with zipfile.ZipFile(p) as z:
    for n in sorted(z.namelist()):
        print(n)
PY

cd "$ROOT/src/yuanrong/output"
sha256sum openyuanrong-v0.0.1.tar.gz \
  openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl \
  yr-functionsystem-v0.0.0.tar.gz > "$OUT/yuanrong.sha256"
tar -tzf openyuanrong-v0.0.1.tar.gz | sort > "$OUT/openyuanrong-tar.list"
python3 - <<'PY' > "$OUT/openyuanrong-wheel.list"
import zipfile
p = "openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl"
with zipfile.ZipFile(p) as z:
    for n in sorted(z.namelist()):
        print(n)
PY
```

## Current conclusion

The Rust functionsystem release package currently satisfies the source-replacement handoff contract for name and layout compatibility:

1. Rust emits the expected `yr-functionsystem-v0.0.0.tar.gz` handoff artifact.
2. Upper-layer `yuanrong` consumes the same tarball name without command changes.
3. The embedded functionsystem binary layout matches the expected installed structure.
4. The final filtered 104 ST acceptance passed from the repacked upper-layer package.

## C++ package list comparison

The clean official C++ package list was captured from:

```text
/workspace/clean_0_8/src/yuanrong-functionsystem/output/yr-functionsystem-v0.0.0.tar.gz
```

Comparison result:

```text
clean C++ entries: 160
Rust entries:      183
C++ minus Rust:    4
Rust minus C++:    27
```

C++ entries not present in the Rust package:

```text
functionsystem/lib/cmake/opentelemetry-cpp/
functionsystem/lib/libcrypto.so.3
functionsystem/lib/libssl.so.3
functionsystem/lib/libyaml_tool.so
```

Rust entries not present in the C++ package are mostly additional linked runtime libraries plus `meta_store`:

```text
functionsystem/bin/meta_store
functionsystem/lib/libabseil_dll.so
functionsystem/lib/libcjson.so
functionsystem/lib/libcjson.so.1.7.17
functionsystem/lib/libdatasystem_worker.so
functionsystem/lib/libgrpc_authorization_provider.so
functionsystem/lib/libgrpc_authorization_provider.so.1.65
functionsystem/lib/libgrpc_authorization_provider.so.1.65.4
functionsystem/lib/libgrpc_plugin_support.so
functionsystem/lib/libgrpc_plugin_support.so.1.65
functionsystem/lib/libgrpc_plugin_support.so.1.65.4
functionsystem/lib/libgrpc_unsecure.so
functionsystem/lib/libgrpc_unsecure.so.42
functionsystem/lib/libgrpc_unsecure.so.42.0.0
functionsystem/lib/libgrpcpp_channelz.so
functionsystem/lib/libgrpcpp_channelz.so.1.65
functionsystem/lib/libgrpcpp_channelz.so.1.65.4
functionsystem/lib/libiconv.so
functionsystem/lib/libiconv.so.2.6.0
functionsystem/lib/libpcre.so.1
functionsystem/lib/libpcre.so.1.2.13
functionsystem/lib/libprotobuf.so
functionsystem/lib/libprotoc.so
functionsystem/lib/libprotoc.so.25.5.0
functionsystem/lib/libxml2.so
functionsystem/lib/libxml2.so.2.9.12
functionsystem/sym/meta_store.sym
```

Interpretation:

1. The extra Rust-side libraries did not block the filtered 104 ST source-replacement proof.
2. Missing `libcrypto.so.3` and `libssl.so.3` are worth a dependency audit before release hardening, even though current ST passed.
3. Missing `libyaml_tool.so` and `opentelemetry-cpp` cmake metadata should be checked against downstream runtime/build consumers before declaring byte-for-byte delivery parity.

The comparison artifacts are stored in:

```text
/tmp/release_audit_26075526/clean-cpp-yr-functionsystem.list
/tmp/release_audit_26075526/cpp-minus-rust.list
/tmp/release_audit_26075526/rust-minus-cpp.list
```

## Remaining audit items

1. Decide whether `libcrypto.so.3`, `libssl.so.3`, `libyaml_tool.so`, and `opentelemetry-cpp` metadata must be restored in the Rust package for release parity.
2. Decide later whether version strings should become strict release criteria. Do not make this a blocker before full ST closure.
3. If release parity becomes strict, decide whether the aggregate openYuanrong package needs separate non-Gloo and
   Gloo-profile hashes.
