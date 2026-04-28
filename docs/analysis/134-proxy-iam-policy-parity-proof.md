# PROXY-002 IAM Policy Parity Proof

Date: 2026-04-28
Branch: `rust-rewrite`
Scope: Rust `function_proxy` only

## Constitution

- C++ 0.8.0 remains the behavioral reference.
- Only Rust `yuanrong-functionsystem` was modified.
- Upper-layer `yuanrong`, runtime, datasystem, ST scripts, and clean C++ control were not modified.
- Build/test parallelism stayed at `CARGO_BUILD_JOBS=8`.

## C++ Reference Extracted

Source files inspected in clean C++ control:

- `function_proxy/common/iam/policy_content.{h,cpp}`
- `function_proxy/common/iam/policy_handler.{h,cpp}`
- `function_proxy/common/iam/internal_iam.cpp`
- `function_proxy/common/iam/authorize_proxy.cpp`
- `function_proxy/busproxy/invocation_handler/invocation_handler.cpp`
- `function_proxy/busproxy/instance_proxy/request_dispatcher.cpp`
- `function_proxy/local_scheduler/instance_control/instance_ctrl_actor.cpp`
- `function_proxy/common/constants.h`
- `common/constants/constants.h`

C++ policy schema:

```json
{
  "tenant_group": {
    "group_name": {
      "tenant_id": ["function-a", "function-b"]
    }
  },
  "white_list": {
    "function_name": ["tenant_id"]
  },
  "policy": {
    "allow": {
      "invoke": {
        "caller_group": { "callee_group": ["*", "=", "white_list", "function_name"] }
      },
      "create": {
        "caller_group": { "callee_group": ["*", "=", "white_list", "function_name"] }
      },
      "kill": {}
    },
    "deny": {
      "tenant_list": ["blocked_tenant"]
    }
  }
}
```

C++ authorization order:

1. Validate `callerTenantID`, `calleeTenantID`, and `callMethod` are non-empty.
2. Resolve caller/callee tenant groups from `tenant_group`; unknown tenants fall back to `external`.
3. Deny immediately if caller tenant is in `policy.deny.tenant_list`.
4. Lookup allow rule by call method, caller group, and callee group.
5. Evaluate function rule list in C++ order:
   - `white_list` plus matching function whitelist entry checks caller tenant membership.
   - `*` allows all functions.
   - `=` allows only same-tenant calls.
   - exact function name allows that function.
6. Otherwise deny.

## Rust Changes

Implemented Rust-local policy authorization in:

- `functionsystem/src/function_proxy/src/iam_policy.rs`
  - Parses the C++ policy JSON shape.
  - Preserves constants: `create`, `invoke`, `kill`, `white_list`, `tenantId`.
  - Implements the C++ allow/deny algorithm, including unknown-tenant `external` fallback.
  - Lazily loads the policy once from `--iam_policy_file` when `--enable_iam` is set.
- `functionsystem/src/function_proxy/src/busproxy/mod.rs`
  - Stores `IamAuthorizer` inside `BusProxyCoordinator`.
  - Adds create authorization for actor-origin `CreateReq` when the caller stream maps to a parent instance.
  - Adds invoke authorization using caller and callee instance tenant metadata.
- `functionsystem/src/function_proxy/src/busproxy/invocation_handler.rs`
  - Rejects denied creates before `register_pending_instance` / scheduling.
  - Rejects denied invokes before dispatching to runtime or pending dispatcher.
  - Returns C++-compatible `ERR_AUTHORIZE_FAILED` and message `authorize failed`.
- Tests:
  - `functionsystem/src/function_proxy/tests/iam_policy_test.rs`
  - `functionsystem/src/function_proxy/tests/invocation_handler_test.rs`

## Intentional Boundary

The Rust `yr.internal.ScheduleRequest` path has no parent-instance field, while C++ create authorization only applies after parent normalization and skips bootstrap/head-function creates. Therefore this patch enforces create IAM on the actor/driver `CreateReq` data-plane path where the parent instance can be identified from the caller stream. It does not invent a non-C++ parent model for parentless scheduler requests.

The token/AK/SK sync managers and external IAM HTTP server byte compatibility remain separate backlog items; this patch closes proxy-local policy authorization for create/invoke decisions.

## TDD Evidence

RED observed first:

```text
cargo test -p yr-proxy --test iam_policy_test -- --nocapture
error[E0432]: unresolved import `yr_proxy::iam_policy`
```

GREEN verification:

```text
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test iam_policy_test -- --nocapture
3 passed; 0 failed
```

```text
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test invocation_handler_test create_req_denied_by_iam_policy_returns_authorize_failed_before_scheduling -- --nocapture
1 passed; 0 failed
```

```text
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test invocation_handler_test invoke_req_denied_by_iam_policy_returns_authorize_failed_before_runtime_send -- --nocapture
1 passed; 0 failed
```

Regression verification:

```text
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test invocation_handler_test -- --nocapture
37 passed; 0 failed
```

```text
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test flag_compat_smoke -- --nocapture
5 passed; 0 failed
```

```text
CARGO_BUILD_JOBS=8 cargo check --workspace --lib --bins
Finished dev profile successfully
```

Known pre-existing warnings remain: duplicate bin target warnings, `schedule_reporter` unused import, dead fields in busproxy structs, and unused helper items.

## Result

`PROXY-002` is closed for proxy-local policy authorization on create/invoke data-plane paths. Rust no longer silently accepts IAM mode without enforcing the parsed policy for these routes.
