# Final Release / Package Surface Audit

Date: 2026-05-03
Branch: `rust-rewrite`
Scope: Subgoal D from `docs/analysis/148-remaining-blackbox-parity-ai-task.md`

## Goal

Re-audit the final Rust FunctionSystem release/package surface against clean official C++ 0.8 artifacts after the latest parity closures, using current container-built artifacts and unchanged upper-layer packaging/ST commands.

This audit is about black-box replacement at artifact/layout/command level. It is **not** a byte-for-byte identity claim.

## Inputs

Clean C++ FunctionSystem artifacts:

```text
/workspace/clean_0_8/src/yuanrong-functionsystem/output/yr-functionsystem-v0.0.0.tar.gz
/workspace/clean_0_8/src/yuanrong-functionsystem/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
/workspace/clean_0_8/src/yuanrong-functionsystem/output/metrics.tar.gz
```

Current Rust FunctionSystem artifacts:

```text
/workspace/rust_current_fs/output/yr-functionsystem-v0.0.0.tar.gz
/workspace/rust_current_fs/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
/workspace/rust_current_fs/output/metrics.tar.gz
```

Current proof-lane repack outputs:

```text
/workspace/proof_source_replace_0_8/src/yuanrong/output/openyuanrong-v0.0.1.tar.gz
/workspace/proof_source_replace_0_8/src/yuanrong/output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

## Reused prior audits

- `docs/analysis/119-r4-package-layout-closure.md`
- `docs/analysis/120-r4-layout-st-proof.md`
- `docs/analysis/128-release-surface-parity-audit.md`
- `docs/analysis/133-cpp-rust-flag-behavior-inventory.md`

## Current findings

| Surface | Current evidence | Finding |
| --- | --- | --- |
| Top-level build/pack commands | current container run still uses `./run.sh build -j 8` and `./run.sh pack` | **Compatible** — upper-layer command shape unchanged |
| Proof-lane repackage command | current proof still uses unchanged `bash scripts/package_yuanrong.sh -v v0.0.1` | **Compatible** — no upper-layer patching required |
| Single-shot ST acceptance | current proof remains `111/111 PASS` at `/tmp/deploy/03020415` | **Compatible** for the accepted black-box lane |
| Tar root layout | current Rust tar still roots at `functionsystem/` with `bin/`, `config/`, `deploy/`, `lib/` | **Compatible** at installed-layout level |
| Bin names + executable bits | current Rust tar exposes executable `domain_scheduler`, `function_agent`, `function_master`, `function_proxy`, `iam_server`, `meta_service`, `runtime_manager`, `yr`; Rust also still ships extra `meta_store` | **Compatible superset** |
| Config/deploy files | current Rust tar still carries official config/deploy tree shapes such as `config/meta_service`, `config/metrics`, `deploy/function_system`, `deploy/third_party`, vendor etcd tools | **Compatible** on the active install path |
| `metrics.tar.gz` layout | clean C++ and current Rust `metrics.tar.gz` top layout match on current comparison | **Compatible** on package presence/top-level layout |
| Wheel metadata | current Rust and clean C++ wheel still match `Name`, `Version`, `Tag`, and `top_level.txt=yr` | **Compatible** at wheel identity level |
| Wheel contents | current Rust wheel inherits the same library/bin inventory drift as the tar package | **Compatible superset with explicit file-level boundaries** |
| Flags / CLI surface | no upper-layer command changes; current release still relies on the existing accepted flag inventory docs | **Compatible for current accepted flow; no new packaging-side drift introduced here** |

## Current tar inventory diff

Current comparison against clean C++:

```text
cpp entries:        160
rust entries:       184
cpp minus rust:       3
rust minus cpp:      27
```

### Current clean C++ entries missing from Rust

```text
functionsystem/lib/libcrypto.so.3
functionsystem/lib/libssl.so.3
functionsystem/lib/libyaml_tool.so
```

### Current Rust-only entries

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

## Important interpretation

### 1. `metrics.tar.gz` presence/layout is no longer just a policy guess

The current Rust build actually emits `metrics.tar.gz`, and its current top-level layout matches clean C++ on direct comparison. That closes the old “maybe missing metrics package” concern for the active build path.

### 2. `libyaml_tool.so` remains a file-level compatibility boundary

This remains the clearest intentional C++-minus-Rust helper library. Rust parses service YAML directly instead of using the C++ dlopen helper path.

### 3. `libcrypto.so.3` / `libssl.so.3` are new explicit file-level boundaries

The current clean C++ tar contains OpenSSL 3 soname entries that are not present in the current Rust tar. The accepted ST and upper-layer packaging flow still pass, so this is **not** currently proven as an active black-box runtime break. But it is a real package-surface difference that must be called out honestly for any broader “drop-in at file inventory level” claim.

### 4. Wheel identity is compatible even though wheel payload remains a superset

Current C++ and Rust wheels still agree on:

- package name
- version
- wheel tag
- top-level Python package name `yr`

The payload drift is inherited from the tar/lib inventory, not from wheel identity metadata.

## Current proof evidence reused

Container/proof-lane logs from the current slice:

- `/workspace/proof_source_replace_0_8/logs/control_plane_container_build.log`
- `/workspace/proof_source_replace_0_8/logs/control_plane_container_pack.log`
- `/workspace/proof_source_replace_0_8/logs/control_plane_package_yuanrong.log`
- `/workspace/proof_source_replace_0_8/logs/control_plane_functionsystem_hashes.txt`
- `/workspace/proof_source_replace_0_8/logs/control_plane_openyuanrong_hashes.txt`
- `/workspace/proof_source_replace_0_8/logs/control_plane_full_cpp_st_evidence.txt`

Current artifact hashes:

```text
1a88c792beaa298ddfb5299689e5646b1df22e5346cf1ceadcc266f626e01ad1  yr-functionsystem-v0.0.0.tar.gz
7d30666d8f887a1785dcc6f5586500d1cdcafa3fe9a8cd1ec84e4aa9bb5a28ed  openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
a89c34dac1377d9e57e130fb8469996130389a95493e554a07a3832068e0551d  metrics.tar.gz
f789aa734ae2b3defe5cffc57bef2544824ecfcfeaebae6144d8602176d453a7  openyuanrong-v0.0.1.tar.gz
3a63f3471cc006f81612d0298d9023ff9406308995866fa2f8b0872eb05e9683  openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

## Conclusion

For the currently accepted black-box lane, the release/package surface remains good enough:

1. top-level build/pack/install/test commands are unchanged
2. artifact names are unchanged
3. proof-lane repackaging is unchanged
4. current single-shot cpp ST remains `111/111 PASS`

What is still **not** honestly closed at file-inventory level is also explicit now:

1. missing clean C++ soname/helper entries:
   - `libcrypto.so.3`
   - `libssl.so.3`
   - `libyaml_tool.so`
2. 27 Rust-only entries, including extra `meta_store` release payload

So the final release claim should remain:

```text
Rust FunctionSystem is a black-box replacement for the current accepted upper-layer build/pack/install/ST lane,
but not a byte-for-byte or file-inventory-identical replacement for every external package consumer.
```
