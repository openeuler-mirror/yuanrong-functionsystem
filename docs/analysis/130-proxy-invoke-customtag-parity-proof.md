# PROXY-001 Invoke CustomTag Parity Proof

Date: 2026-04-28
Branch: `rust-rewrite`
Gap: `PROXY-001` from `docs/analysis/129-rust-gap-backlog.md`.

## Problem

C++ 0.8 `function_proxy` copies invoke custom tags into the runtime call request:

```text
function_proxy/busproxy/invocation_handler/invocation_handler.cpp
InvokeRequestToCallRequest:
  *callRequest->mutable_createoptions() = request->invokeoptions().customtag();
```

Before this fix, Rust `InvocationHandler::invoke_to_call` copied function, args, trace, request id, sender id, return ids, and span id, but did not copy `InvokeRequest.invokeOptions.customTag` into `runtime_service.CallRequest.createOptions`.

## Rust change

File:

- `functionsystem/src/function_proxy/src/busproxy/invocation_handler.rs`

Change:

- `CallRequest.create_options` is now initialized from `invoke.invoke_options.as_ref().map(|opts| opts.custom_tag.clone()).unwrap_or_default()`.

This matches the C++ handoff behavior without changing non-Rust code or upper-layer scripts.

## Regression test

File:

- `functionsystem/src/function_proxy/tests/invocation_handler_test.rs`

Added test:

- `invoke_to_call_copies_invoke_custom_tags_to_call_create_options`

RED evidence before implementation:

```text
assertion `left == right` failed
  left: None
 right: Some("true")
test invoke_to_call_copies_invoke_custom_tags_to_call_create_options ... FAILED
```

GREEN evidence after implementation:

```text
running 1 test
test invoke_to_call_copies_invoke_custom_tags_to_call_create_options ... ok
```

## Verification

Commands run with the project constitution cap `CARGO_BUILD_JOBS=8`:

```bash
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test invocation_handler_test invoke_to_call_copies_invoke_custom_tags_to_call_create_options -- --nocapture
CARGO_BUILD_JOBS=8 cargo test -p yr-proxy --test invocation_handler_test -- --nocapture
CARGO_BUILD_JOBS=8 cargo check --workspace --lib --bins
```

Results:

```text
invocation_handler_test: 35 passed; 0 failed
cargo check --workspace --lib --bins: passed
```

Existing warnings remain pre-existing Rust warnings, not introduced by this change.

## Status

`PROXY-001` is closed for code-level parity and unit verification. It is still reasonable to let the next full source-replacement ST run cover it indirectly, but this specific field-copy contract is now directly locked by Rust unit coverage.
