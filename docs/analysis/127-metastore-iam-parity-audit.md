# MetaStore / IAM Code Parity Audit

Date: 2026-04-28
Branch: `rust-rewrite`
Scope: Rust `functionsystem/src/meta_store/**` and `functionsystem/src/iam_server/**` against C++ 0.8 `meta_store/**` and `iam_server/**`.

## Summary

Rust meta_store is an in-process etcd-compatible gRPC implementation with KV/watch/lease/snapshot components. Rust IAM implements token and AK/SK flows with tests. This is useful for the Rust black-box lane, but C++ IAM and metastore integration includes policy/credential synchronization and official IAM route semantics that need endpoint-level A/B before claiming full parity.

## Source evidence

| Area | C++ evidence | Rust evidence | Finding |
| --- | --- | --- | --- |
| MetaStore server | C++ `meta_store/server` and client build targets; deployment uses `meta_service` Go app too | Rust `meta_store/src/lib.rs` includes etcdserverpb/authpb/mvccpb and modules `kv_store`, `watch_service`, `lease_service`, `backup`, `snapshot_file` | Rust provides etcd-compatible surface, but exact server feature parity needs etcd API probes. |
| MetaStore backup/persistence | C++ master flags include meta store backup flush concurrency/batch and persistence | Rust meta_store has `backup.rs`, `snapshot_file.rs`, config, lease/watch services | Needs durability/failure A/B. |
| IAM server routes | C++ `iam_server/iam/**`, flags, driver | Rust `iam_server/src/routes.rs`, `token.rs`, `aksk.rs`, `user_manager.rs`, tests for routes/e2e auth | Rust has strong local tests, but exact route/status/body compatibility needs C++ A/B. |
| Token format | C++ token implementation is part of IAM service and proxy InternalIAM integration | Rust `TokenManager` mints `payload_b64.sig_hex` HMAC tokens and stores current/old tokens in metastore keys | Token format may differ; black-box compatibility depends on route behavior, not internal byte format. |
| Proxy IAM consumption | C++ function_proxy has `InternalIAM`, token/credential sync actors, policy handler, authorize proxy | Rust IAM server exists, but Rust proxy authorization is pass-through in inspected path | IAM server alone is insufficient without proxy enforcement. |
| Policy authorization | C++ `function_proxy/common/iam/policy_handler.cpp` and IAM content parse allow/deny rules | Rust IAM route/user/token tests do not prove equivalent policy engine in proxy | Policy parity is open. |

## Findings

### META-001: etcd API compatibility needs a focused probe matrix

Rust meta_store includes KV, watch, lease, backup, and snapshot modules and generated etcd protos. Current ST exercises only the subset needed by function system. C++/Go meta_service behavior may differ for edge cases: revisions, compaction, watch cancellation, leases, auth, backup, and error codes.

Classification: `Needs test` / `P1` because metastore correctness affects all components.

### META-002: persistence/backup semantics need failure tests

C++ master flags expose meta store persistence and backup flush settings. Rust has backup and snapshot files, but crash/restart durability and batch semantics are not proven against C++.

Classification: `Needs test` / `P1` if persistent mode is release scope.

### IAM-001: route compatibility is locally tested but not A/B-proven

Rust IAM has extensive route tests for tokens, AK/SK, tenants, users, disabled modes, and rejection paths. The exact C++ HTTP route names, status codes, headers, and JSON bodies still need generated A/B comparison.

Classification: `Needs test` / `P2`, `P1` for IAM-enabled release.

### IAM-002: token internals may intentionally differ

Rust token format is HMAC `payload_b64.sig_hex`. This can be black-box acceptable if only Rust IAM validates tokens. It is not byte-compatible with any C++ token format unless proven.

Classification: `Release-policy boundary` / `P2`.

### IAM-003: IAM server does not close proxy authorization parity

C++ proxy consumes IAM via `InternalIAM`, token/credential sync actors, and policy handler. Rust proxy create/invoke authorization remains pass-through/unproven. This is tracked as `PROXY-002`, but it must be considered part of the IAM release story.

Classification: `Needs implementation` / `P1`.

## Strong areas

- Rust IAM has meaningful unit/e2e tests for token, AK/SK, user/tenant CRUD, and rejection paths.
- Rust meta_store implements the main etcd-compatible primitives needed by current functionsystem ST.
- Etcd key constants were previously restored and unit-tested in `common/utils/src/etcd_keys.rs`.

## Next checks

1. Generate C++ vs Rust IAM endpoint matrix with method/path/header/body/status/JSON shape.
2. Run etcd KV/watch/lease/revision probes against clean C++ meta_service and Rust meta_store.
3. Crash/restart persistence test for Rust meta_store if persistent mode is release scope.
4. Treat proxy IAM enforcement (`PROXY-002`) as a prerequisite for IAM-enabled black-box release.
