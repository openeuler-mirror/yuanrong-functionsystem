# IAM Control-Plane Proof

Date: 2026-05-03
Branch: `rust-rewrite`
Scope: bounded Subgoal D closure from `docs/analysis/161-production-control-plane-parity-ai-task.md`

## Closed scope

This proof closes the smallest production-meaningful IAM wire gap found in
`docs/analysis/163-iam-control-plane-parity-matrix.md`:

1. legacy `/v1/token/*` routes now use the C++-style header/status contract,
2. `/iam-server/v1/token/{auth,require,abandon}` aliases now exist for clean C++ callers on the closed token-route surface,
3. Rust-issued tokens now use the same 3-segment JWT-style wire shape as C++.

It does **not** claim full IAM parity. The still-bounded areas are:

- legacy AK/SK encrypted response-body parity,
- legacy refresh-route parity,
- external IdP provider behavior (`exchange`, `code-exchange`, `login`, `auth/url`),
- deeper C++ follower/leader forwarding and watched-cache actor semantics.

## Files changed

```text
docs/analysis/129-rust-gap-backlog.md
docs/analysis/163-iam-control-plane-parity-matrix.md
docs/analysis/164-iam-control-plane-proof.md
functionsystem/src/iam_server/src/routes.rs
functionsystem/src/iam_server/src/token.rs
functionsystem/src/iam_server/src/token_store.rs
functionsystem/src/iam_server/tests/e2e_auth.rs
functionsystem/src/iam_server/tests/routes_test.rs
functionsystem/src/iam_server/tests/token_test.rs
```

## Host preflight

```text
cargo test -p yr-iam -- --nocapture
=> PASS
   - routes_test: 23 passed
   - e2e_auth: 8 passed
   - token_test: 10 passed, 5 ignored (direct etcd-only)

cargo check --workspace --lib --bins
=> PASS

git diff --check
=> PASS
```

New coverage added in this slice:

```text
legacy_prefixed_token_routes_match_header_contract
legacy_prefixed_token_auth_returns_forbidden_for_invalid_token
legacy_prefixed_credential_require_route_exists
legacy_prefixed_token_require_returns_bad_request_when_iam_disabled
mint_token_has_cxx_jwt_shape
```

## Container acceptance

Container: `yr-e2e-master`

Workspace: `/workspace/rust_current_fs`

Commands:

```text
export CARGO_BUILD_JOBS=8
cargo test -p yr-iam -- --nocapture
./run.sh build -j 8
./run.sh pack
```

Observed results:

```text
cargo test -p yr-iam -- --nocapture
=> PASS
   - routes_test: 23 passed
   - e2e_auth: 8 passed
   - token_test: 10 passed, 5 ignored

./run.sh build -j 8
=> Finished `release` profile [optimized] target(s) in 14.91s

./run.sh pack
=> built:
   /workspace/rust_current_fs/output/yr-functionsystem-v0.0.0.tar.gz
   /workspace/rust_current_fs/output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
   /workspace/rust_current_fs/output/metrics.tar.gz
```

Artifact hashes:

```text
62bcfa917fbc3b884bc90b150965d497aaecdc70756b61b71017dee2187f4e48  output/yr-functionsystem-v0.0.0.tar.gz
d1ddf1f2b7f5663383ab7415675555787caf7661884fc436d3a1c9a73df68635  output/openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl
d6d24eb1d790cd6f536626b2ab3611314db3154753bc8bedbaeae63ebd2041be  output/metrics.tar.gz
```

## Upper-layer proof lane

Proof lane: `/workspace/proof_source_replace_0_8`

Replaced only these FunctionSystem artifacts in `src/yuanrong/output`:

- `yr-functionsystem-v0.0.0.tar.gz`
- `openyuanrong_functionsystem-0.0.0-py3-none-manylinux_2_34_x86_64.whl`
- `metrics.tar.gz`

Repackaged with unchanged upper-layer script:

```text
cd /workspace/proof_source_replace_0_8/src/yuanrong
bash scripts/package_yuanrong.sh -v v0.0.1
=> output/openyuanrong-v0.0.1.tar.gz
=> output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

Repacked hashes:

```text
ac13581cec2df963ce1e20020aa9281a8a7e6542be9ca0269e36ce0658b8c223  output/openyuanrong-v0.0.1.tar.gz
3a63f3471cc006f81612d0298d9023ff9406308995866fa2f8b0872eb05e9683  output/openyuanrong-0.7.0.dev0-cp39-cp39-manylinux_2_34_x86_64.whl
```

## Single-shot ST

Command:

```bash
cd /workspace/proof_source_replace_0_8/src/yuanrong/test/st
bash test.sh -b -l cpp -f "*-CollectiveTest.InvalidGroupNameTest"
```

Deploy path:

```text
/tmp/deploy/03133559
```

Evidence file:

```text
/tmp/deploy/03133559/driver/cpp_output.txt
```

Observed evidence:

```text
[==========] Running 111 tests from 6 test cases.
[  PASSED  ] 111 tests.
```

## Remaining release-scope boundaries

1. `IAM-001` is only partially closed: legacy token `auth/require/abandon` wire compatibility is now proved, and credential-path aliases are exposed for follow-up work, but AK/SK encrypted-body, refresh-route, and external IdP routes are still unproven against C++.
2. `IAM-002` is only partially closed: the token wire shape is now JWT-style, but broader external shared-token compatibility is still only proved for the closed Rust/C++ IAM route surface in this slice.
