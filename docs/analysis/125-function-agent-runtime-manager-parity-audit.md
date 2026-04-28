# Function Agent / Runtime Manager Code Parity Audit

Date: 2026-04-28
Branch: `rust-rewrite`
Scope: Rust `functionsystem/src/function_agent/**` and `functionsystem/src/runtime_manager/**` against C++ 0.8 `function_agent/**` and `runtime_manager/**`.

## Summary

Rust function_agent/runtime_manager implements the main ST path: deploy local/copy/shared/S3-like code, start runtime processes, assign ports, set runtime env, capture logs, report resources, and stop/snapshot runtimes. C++ contains a broader operational subsystem: deployer variants, plugin/virtualenv managers, debug server manager, checkpoint file manager, detailed metrics collectors, std monitor, OOM/memory-limit callbacks, and richer process supervision. Current ST does not cover most of that surface.

## Source evidence

| Area | C++ evidence | Rust evidence | Finding |
| --- | --- | --- | --- |
| Function deployers | C++ `function_agent/code_deployer/{local,copy,remote,s3,shared_dir,working_dir}_deployer.*` | Rust `function_agent/src/deployer.rs` supports `Local`, `Copy`, `S3`, `SharedDir`, `Unknown`, checksum stripping, tar.gz extraction | Main deploy modes exist, but C++ remote/working-dir/OBS/plugin exact behavior is not proven. |
| Plugin/virtualenv manager | C++ `function_agent/plugin/**` and `runtime_manager/virtual_env_manager/**` | Rust has `runtime_manager/src/venv.rs` and simple env hints | Rust has lightweight venv hints, not C++ plugin manager parity. |
| Runtime process start | C++ `runtime_manager/executor/**`, `config/command_builder.*`, `utils/volume_mount.*` | Rust `runtime_manager/src/executor.rs` picks runtime executable, builds Python/C++ command, sets env, logs, bind mounts, cgroups | Main process start path is implemented and ST verified; exact command builder breadth needs more A/B. |
| Debug server | C++ `runtime_manager/debug/debug_server_mgr_actor.cpp` supports cpp/python debug server lifecycle | No equivalent Rust debug server manager found in inspected runtime_manager files | Debug instance behavior is likely unsupported/partial. |
| Checkpoint file manager | C++ `runtime_manager/ckpt/**` | Rust state persistence is proxy-side; no equivalent runtime_manager checkpoint file manager found | Runtime-manager checkpoint file semantics not proven. |
| Health/reap/status | C++ `runtime_manager/healthcheck/healthcheck_actor.cpp` reaps processes, reports status to function_agent, extracts exception/std logs, handles OOM status | Rust has `health_check.rs`, `instance_health.rs`, `runtime_ops.rs`, and state maps | Rust covers core health/status but exact exit classification/log extraction behavior is broader in C++. |
| Metrics/resource collectors | C++ `runtime_manager/metrics/collector/**` covers CPU, memory, disk, XPU, NUMA, resource labels, proc/system collectors | Rust `runtime_manager/src/metrics.rs` plus function_agent resource reporting exists | Rust reporting is likely sufficient for ST, not full metrics collector parity. |
| OOM/memory control | C++ metrics/healthcheck path has runtime OOM monitor callbacks and OOM status reporting | Rust has `runtime_manager/src/oom/**`, cgroup/rlimit support, and config flags | Rust has implementation, but C++ behavior under OOM needs direct A/B tests. |
| Log/std monitor | C++ `runtime_manager/log/**` and `std_monitor/**` | Rust `log_manager.rs` rotates stdout/stderr files | Basic log capture exists; user log rolling/std monitor exact semantics not proven. |
| Merge mode | C++ deployment commonly starts agent/runtime manager together under install scripts | Rust `function_agent` supports `--enable-merge-process` and embedded RM tests | This path is ST verified; standalone RM breadth still needs release tests. |

## Findings

### AGENT-001: deployer mode breadth is not fully proven

Rust deployer covers local/copy/S3/shared-dir and includes checksum/tar extraction. C++ has more deployer classes and plugin integration. The ST source-replacement lane likely exercises only local/copy-style paths.

Classification: `Needs test` / `P2`, upgrade to `P1` if remote/OBS/plugin deploy is release scope.

### AGENT-002: plugin and virtualenv manager parity is weak

C++ has a dedicated plugin subsystem in `function_agent/plugin/**` and runtime virtual env manager actors. Rust has simpler venv hints and process env setup.

Classification: `Needs implementation` / `P1` if plugin/virtualenv management is required, otherwise `P2` documented unsupported.

### RUNTIME-001: debug server lifecycle is missing or unproven

C++ debug server manager supports cpp/python debug server creation/destruction and adds debug info to runtime instance info. No comparable Rust subsystem was found in the inspected runtime_manager tree.

Classification: `Needs implementation` / `P2`; `P1` if debug instances are part of release acceptance.

### RUNTIME-002: runtime command builder parity needs A/B tests

Rust `start_runtime_process` manually builds Python and C++ commands. It has important black-box fixes such as C++ `arg0("cppruntime")`. C++ command building is split across `config/command_builder.*`, executor, volume mount, env, and std redirector logic.

Classification: `Needs test` / `P1`. Add A/B command/env snapshots for Python and C++ runtime start.

### RUNTIME-003: metrics/NUMA/XPU collectors are much broader in C++

C++ collector tree includes CPU, memory, disk, external system, XPU, NUMA, resource labels, and proc/system collectors. Rust has resource reporting and metrics snapshots, but full collector parity is not proven.

Classification: `Needs test` / `P2`, `P1` if scheduler placement depends on these metrics.

### RUNTIME-004: OOM and exit-status semantics need direct comparison

Rust has OOM/cgroup modules and rlimit setup. C++ explicitly reports runtime memory-exceed-limit status, extracts exception/std logs, and coordinates healthcheck callbacks. Behavior can differ even if processes are killed correctly.

Classification: `Needs test` / `P1` for production reliability.

### RUNTIME-005: checkpoint/debug/std-monitor ancillary managers are not closed

C++ runtime_manager has `ckpt`, `debug`, `std_monitor`, `virtual_env_manager`, and richer `log` managers. Rust has partial equivalents for logs/env/state, but no source-level proof of exact manager parity.

Classification: `Needs implementation` or `Release-policy boundary` / `P2` depending on release scope.

## Strong areas

- Merge-process function_agent + runtime_manager path is covered by tests and current ST.
- Rust runtime start/stop/status/snapshot gRPC operations are implemented in `runtime_ops.rs` and covered by unit/integration tests.
- C++ runtime process name compatibility (`cppruntime`) was explicitly handled in Rust executor.

## Next checks

1. Capture C++ and Rust runtime start command/env/log-path for Python and C++ ST functions.
2. Build deployer A/B tests for local, copy, shared-dir, S3/OBS URL, and working-dir modes.
3. Decide plugin/debug/virtualenv manager release scope.
4. Run OOM/memory-limit A/B probes only after documenting safe resource limits.
