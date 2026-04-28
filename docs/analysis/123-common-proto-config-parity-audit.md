# Common / Proto / Config Code Parity Audit

Date: 2026-04-28
Branch: `rust-rewrite`
Scope: Rust `functionsystem/src/common/**` against C++ 0.8 `functionsystem/src/common/**`.

## Summary

Rust common code is good enough for the proven source-replacement ST lane, but C++ common contains broader platform behavior that Rust either does not implement or currently treats as parser/startup compatibility. The highest-impact common gaps are service metadata validation, SSL/LiteBus environment setup, NUMA utilities, and exact datasystem KV backend parity.

## Source evidence

| Area | C++ evidence | Rust evidence | Finding |
| --- | --- | --- | --- |
| Service JSON/YAML validation | `common/service_json/service_json.cpp` has `CheckServiceName`, `CheckKind`, `CheckRuntime`, `CheckCPUAndMemorySize`, `CheckEnv`, `CheckFunctionLayers`, `CheckHookHandlerRegularization`, and `CheckServiceInfos` | `functionsystem/src/common/utils/src/service_json.rs` defines serde data structs and `parse_service_info_json`; `functionsystem/src/function_proxy/src/instance_ctrl.rs` reads `services_path` using `serde_yaml::from_str` and extracts selected fields | Rust parses the metadata shape but does not yet mirror the full C++ validation gate. |
| Runtime/kind whitelist | C++ `service_json.cpp` validates kind/runtime enum membership before producing function metadata | Rust structs accept arbitrary strings; proxy resolves runtime from service YAML if present | Rust may accept malformed metadata that C++ rejects. |
| Env validation | C++ `CheckEnv` rejects system reserved fields and enforces total env length limits | Rust `parse_env` converts values to strings; `start_instance` merges service env, create arg env, delegate env, and create options | Rust env behavior is more permissive and may let callers override reserved runtime/system variables. |
| Layer validation | C++ validates `layerName:version`, max count, regex, and version range | Rust stores `layers: Vec<String>` in `FunctionConfig`; no equivalent validation found in inspected path | Layer metadata compatibility is parse-only unless later code validates it. |
| Hook handler validation | C++ validates handler format/length based on runtime | Rust has `FunctionHookHandlerConfig` but does not enforce the same regex/length gate in the inspected path | Invalid handlers may be accepted earlier in Rust than C++. |
| CLI legacy flags | C++ exposes broad common flags through `common_flags.cpp` and component flags | Rust `functionsystem/src/common/utils/src/cli_compat.rs` rewrites snake-case flags and ignores known legacy flags; `CommonConfig` defines many shared flags | Startup compatibility is strong, but ignored flags are not behavior parity. |
| SSL/LiteBus env | C++ `common/utils/ssl_config.cpp` resolves cert paths, validates files, and calls `LitebusSetSSLEnvsC` for `LITEBUS_SSL_*` | Rust `CommonConfig` and component ignored structs accept SSL flags; no equivalent LiteBus env initializer found in inspected Rust common/proxy path | SSL is a likely parse-compatible-only path unless another module initializes it. |
| NUMA binding | C++ `common/utils/numa_binding.cpp` implements CPU/memory bind and verification with libnuma | Rust has group bind proto restoration and first-hop scheduler extension mapping; no Rust common NUMA bind utility found in inspected path | Full NUMA placement/binding parity is not proven. |
| Datasystem KV | C++ `common/kv_client/kv_client.cpp` wraps `datasystem::KVClient::Init/Get/Set/Del` | Rust `functionsystem/src/common/data_client/src/kv.rs` defines an HTTP-style `/kv/v1` client and batch loops | Exact DS backend semantics are different/unproven. |
| YAML helper library | C++ package includes service/yaml tooling such as `common/yaml_tool` and historically `libyaml_tool.so` | Rust uses serde YAML directly and package intentionally does not restore `libyaml_tool.so` | Release-policy boundary unless byte-for-byte helper compatibility is required. |

## Detailed findings

### COMMON-001: service metadata validation is weaker in Rust

C++ treats service metadata as a validation boundary. `service_json.cpp` checks service/function names, kind, runtime, resource ranges, env limits/reserved fields, layer references, and hook handler syntax before accepting metadata.

Rust currently models the JSON/YAML shape in `service_json.rs` and direct YAML extraction in `instance_ctrl.rs`. This is enough for current ST services, but it is not behavior-equivalent for invalid metadata inputs.

Impact: a Rust deployment could accept a function/service definition that clean C++ rejects, or pass malformed metadata farther downstream where failures are harder to diagnose.

Classification: `Needs implementation` / `P1`.

### COMMON-002: libyaml_tool is a release-policy boundary

C++ has `common/yaml_tool` and the package historically exposes `libyaml_tool.so`. Rust does not use that helper for its current service parsing and the source-replacement ST passes without it.

Impact: no current ST failure, but external consumers that link/load `libyaml_tool.so` would observe a package surface difference.

Classification: `Release-policy boundary` / `P3`.

### COMMON-003: SSL/LiteBus cert behavior is parse-compatible, not proven behavior-compatible

C++ `ssl_config.cpp` resolves cert paths, validates root/cert/key files, and sets LiteBus SSL environment variables. Rust common/proxy accepts `ssl_*` and etcd SSL flags, but the inspected code does not show equivalent LiteBus SSL env setup.

Impact: deployments requiring secure LiteBus/metrics/etc may start but silently run without the same TLS behavior.

Classification: `Needs test` and likely `Needs implementation` / `P1` if SSL mode is in release scope.

### COMMON-004: NUMA binding utilities are absent/partial in Rust

C++ implements concrete CPU and memory NUMA binding helpers. Rust restores proto fields and propagates group bind metadata first-hop, but no equivalent Rust NUMA bind utility was found in the inspected common/proxy path.

Impact: group bind/NUMA fields may be accepted and forwarded without enforcing CPU/memory locality.

Classification: `First-hop compatible` / `P1` if NUMA placement is release scope, otherwise `P2`.

### COMMON-005: Rust KV backend differs from C++ datasystem::KVClient

C++ uses the datasystem SDK client. Rust has an HTTP KV adapter with base path `/kv/v1`. That may be sufficient for Rust-owned paths that use the adapter, but it is not proven equivalent for status codes, auth, connection behavior, binary payload handling, batch atomicity, or retry semantics.

Impact: state/cache paths can pass current ST yet differ under DS auth, failure, or large-value conditions.

Classification: `Needs test` / `P2`, upgraded to `P1` if DS-backed state/cache exactness is required.

### COMMON-006: ignored legacy flags need behavior labels

Rust `cli_compat.rs` intentionally accepts and ignores many C++ flags. This was necessary for black-box startup compatibility, and `docs/analysis/116-binary-flag-parity-gate.md` proves parser acceptance. However, parser acceptance alone must not be interpreted as behavior parity.

Impact: future operators may enable an accepted flag and assume C++ behavior exists.

Classification: `Parse-compatible` / `P2`.

## Suggested next checks

1. Add a Rust metadata-validation parity test set using C++ invalid/valid service examples as fixtures.
2. Search all Rust modules for actual SSL/LiteBus env setup before deciding implementation scope.
3. Compare DS KV behavior under missing key, large binary value, auth enabled, and DS outage.
4. Decide whether `libyaml_tool.so` is an explicit out-of-scope compatibility surface.
