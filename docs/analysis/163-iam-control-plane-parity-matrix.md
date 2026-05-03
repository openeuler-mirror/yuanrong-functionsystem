# IAM Control-Plane Parity Matrix

Date: 2026-05-03
Branch: `rust-rewrite`
Scope: Subgoal D from `docs/analysis/161-production-control-plane-parity-ai-task.md`

## Goal

Compare the C++ 0.8 IAM server and its clean-C++ proxy client contract against the current Rust `iam_server`, then choose the smallest production-meaningful closure for this slice.

This matrix separates:

1. **legacy wire-contract gaps** that can break a clean C++ caller against the Rust IAM server right now,
2. **deeper auth-provider / persistence / byte-format gaps** that should stay explicitly bounded unless a larger slice is justified.

## C++ references inspected first

```text
0.8.0/src/yuanrong-functionsystem/functionsystem/src/iam_server/iam/iam_actor/iam_actor.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/iam_server/iam/internal_iam/internal_iam.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/iam_server/iam/internal_iam/token_content.h
0.8.0/src/yuanrong-functionsystem/functionsystem/src/iam_server/iam/internal_iam/token_manager_actor.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/iam_server/iam/internal_iam/aksk_manager_actor.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_proxy/common/iam/iam_client.cpp
0.8.0/src/yuanrong-functionsystem/functionsystem/src/function_proxy/common/iam/internal_token_manager_actor.cpp
```

## Rust references inspected

```text
functionsystem/src/iam_server/src/routes.rs
functionsystem/src/iam_server/src/token.rs
functionsystem/src/iam_server/src/token_store.rs
functionsystem/src/iam_server/src/aksk.rs
functionsystem/src/iam_server/src/state.rs
functionsystem/src/iam_server/tests/routes_test.rs
functionsystem/src/iam_server/tests/e2e_auth.rs
functionsystem/src/iam_server/tests/token_test.rs
functionsystem/src/function_proxy/src/iam_policy.rs
```

## High-level finding

Rust already had a usable embedded IAM service for the currently accepted ST lane, but it was not yet a clean black-box replacement for the **legacy IAM HTTP contract** that the C++ proxy/client stack expects.

This slice closes the smallest production-meaningful part of that gap:

1. Rust now exposes `/iam-server/v1/token/{auth,require,abandon}` aliases for the legacy token routes that clean C++ callers actually hit, and also exposes the credential-path aliases for later follow-up work.
2. Rust legacy token GET routes now speak the C++-style **status + headers** contract (`X-Auth`, `X-Salt`, `X-Tenant-ID`, `X-Expired-Time-Span`, `X-Role`) instead of Rust-only JSON.
3. Rust legacy token failures now use the C++-aligned `400/403/500` split for the closed token-route cases in this slice.
4. Rust-issued tokens now use the same 3-segment JWT-shaped wire format as the current C++ implementation.

The larger AK/SK response-body encryption format, external IdP integrations, and deeper watched-cache / actor semantics remain explicit boundaries.

## Matrix

| Behavior | C++ 0.8 behavior | Current Rust state | Evidence | Slice decision |
| --- | --- | --- | --- | --- |
| Legacy route prefix | Clean C++ proxy calls `/iam-server/v1/token/*` and `/iam-server/v1/credential/*` | Closed in bounded form for the token contract in this slice: Rust now registers `/iam-server/v1/token/{auth,require,abandon}` plus credential-path aliases, but credential body compatibility is still open | C++ `function_proxy/common/iam/iam_client.cpp:26-31`; Rust `iam_server/src/routes.rs` | Closed in this slice for the token-route surface |
| Token require success wire shape | `GET /v1/token/require`; success = `200`, token returned in `X-Auth`, salt in `X-Salt`, expiry in `X-Expired-Time-Span` | Closed in bounded form: Rust legacy token require now returns headers instead of Rust-only JSON | C++ `iam_actor.cpp:122-170`, `iam_client.cpp:141-151`; Rust `routes.rs`; tests `legacy_prefixed_token_routes_match_header_contract` | Closed in this slice |
| Token auth success wire shape | `GET /v1/token/auth`; success = `200`, tenant and expiry returned in headers, optional `X-Role` header | Closed in bounded form: Rust legacy token auth now returns the C++-style header contract | C++ `iam_actor.cpp:82-119`; Rust `routes.rs`; tests `legacy_prefixed_token_routes_match_header_contract` | Closed in this slice |
| Token abandon success wire shape | `GET /v1/token/abandon`; success/failure is status-only | Closed in bounded form: Rust legacy token abandon now uses status/plain-body semantics instead of Rust-only JSON | C++ `iam_actor.cpp:173-194`; Rust `routes.rs`; tests `legacy_prefixed_token_routes_match_header_contract` | Closed in this slice |
| Token route disabled-mode status | Legacy request filter returns `400` when IAM is disabled or launched in the wrong credential mode | Closed for the audited disabled-path case in this slice: Rust legacy token routes now return `400` for IAM-disabled / wrong-credential-mode request-filter failures | C++ `iam_actor.cpp:302-329`; Rust `routes.rs`; tests `legacy_prefixed_token_require_returns_bad_request_when_iam_disabled` | Closed in this slice |
| Token verify failure status | Invalid or expired token returns `403`; internal/wait-init errors return `500` | Closed for the audited invalid-token path in this slice: Rust malformed/invalid/expired/not-present token failures now map to `403` on the legacy token auth route | C++ `iam_actor.cpp:99-107`; Rust `routes.rs`; tests `legacy_prefixed_token_auth_returns_forbidden_for_invalid_token`, `e2e_auth_rejects_invalid_token_on_token_auth_route` | Closed in this slice |
| Token byte shape | JWT-style `header.payload.signature`, with base64url signature of HMAC hex | Closed in bounded form: Rust now mints and verifies the same 3-segment JWT-style token shape, while still accepting old 2-segment Rust tokens during transition | C++ `token_content.h:148-319`; Rust `iam_server/src/token.rs`; test `mint_token_has_cxx_jwt_shape` | Closed in this slice |
| Token storage semantics | C++ persists token/salt JSON for watched caches and backup flows | Rust persists token+claims JSON sufficient for Rust verify/rotation, but not the same payload | C++ `token_manager_actor.cpp`, `token_transfer.cpp`; Rust `iam_server/src/token.rs:27-31`, `133-160` | Keep Rust-private storage format as a bounded implementation detail if wire contract is closed |
| AK/SK legacy route prefix | Clean C++ proxy calls `/iam-server/v1/credential/*` | Closed in bounded form: Rust now registers `/iam-server/v1/credential/*` aliases too, but body-shape parity is still open | C++ `iam_client.cpp:29-31`; Rust `routes.rs`; test `legacy_prefixed_credential_require_route_exists` | Closed in this slice |
| AK/SK success body format | C++ returns JSON body shaped as encrypted `EncAKSKContent` | Rust returns plain JSON `{tenant_id, access_key, secret_key}` on legacy routes | C++ `iam_actor.cpp:197-299`, `iam_client.cpp:71-80`; Rust `routes.rs:252-332` | Explicit boundary for now |
| External IdP routes | C++ has provider-backed token exchange/login/auth-url logic | Rust exposes placeholder exchange/login/url routes using simple stand-ins | C++ broader IAM server tree; Rust `routes.rs:345-529` | Explicit boundary for now |
| REST-style `/v1/tokens`, `/v1/aksk`, `/v1/users`, `/v1/tenants`, `/v1/roles` | Not part of clean C++ legacy IAM client contract | Rust adds them as convenience/admin APIs | Rust `routes.rs:532-997` | Keep as Rust-only additive surface; do not claim C++ parity for these endpoints |
| Proxy IAM policy | Separate proxy-local authorization policy parsing/allow/deny rules | Rust already has dedicated policy parity tests and proof | Existing proof `docs/analysis/134-proxy-iam-policy-parity-proof.md`; Rust `function_proxy/src/iam_policy.rs` | Already closed enough; not this slice |

## What current Rust tests now prove

- health route aliases and header validation:
  - `iam_server/tests/routes_test.rs`
- legacy prefixed token require/auth/abandon now work with C++-style headers:
  - `legacy_prefixed_token_routes_match_header_contract`
- legacy prefixed invalid token verify now returns `403`:
  - `legacy_prefixed_token_auth_returns_forbidden_for_invalid_token`
- legacy prefixed credential route alias exists:
  - `legacy_prefixed_credential_require_route_exists`
- legacy disabled IAM request filter now returns `400`:
  - `legacy_prefixed_token_require_returns_bad_request_when_iam_disabled`
- placeholder external auth routes validate basic body/query shape:
  - `iam_server/tests/routes_test.rs`
- REST-style token/aksk/user/tenant CRUD with embedded metastore:
  - `iam_server/tests/e2e_auth.rs`
- token mint/verify round-trip, tamper rejection, expiry rejection, and 3-segment JWT shape:
  - `iam_server/tests/token_test.rs`
- proxy IAM policy allow/deny/whitelist behavior:
  - `function_proxy/tests/iam_policy_test.rs`

## What current Rust does **not** yet prove

- legacy AK/SK encrypted response JSON parity
- legacy refresh-route parity (not part of the closed C++ token-route surface in this slice)
- external IdP provider parity
- follower/leader forwarding semantics of the C++ token actor
- full watched-cache / replace-token actor parity vs the C++ IAM internals

## Smallest release-scope closure candidates

### Worth closing in this slice

1. **Legacy token-route wire compatibility.**
   - Add `/iam-server/v1/token/*` aliases.
   - Return the success data the same way C++ callers read it: headers first, not JSON bodies.
   - Match the legacy status-code split closely enough for clean C++ callers.
2. **Legacy credential-route prefix compatibility.**
   - Add `/iam-server/v1/credential/*` aliases so clean C++ callers can reach Rust IAM.
3. **JWT-style token shape.**
   - Make Rust-issued tokens 3 segments like C++ JWT output.
   - Keep storage internals flexible as long as route and verify behavior stay compatible.

### Explicitly larger follow-up work

1. legacy AK/SK encrypted-body parity,
2. external IdP integration parity (`exchange`, `code-exchange`, `login`, `auth/url`),
3. deeper watched-cache / replace-token actor semantics beyond what current Rust needs.

## Recommended slice outcome

This slice closes the **legacy token-route compatibility gap** that a clean C++ caller would immediately hit against the Rust IAM server, while leaving AK/SK encrypted-body parity, external IdP behavior, and deeper internal actor/storage semantics explicitly bounded in the proof doc and backlog.
