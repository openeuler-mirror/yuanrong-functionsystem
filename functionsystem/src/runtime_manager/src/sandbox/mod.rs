//! CONTAINER execution backend (sandbox / containerd) — port of the C++
//! `runtime_manager/executor/sandbox/*`. See
//! `docs/analysis/173-sandbox-executor-rust-design.md`.
//!
//! M1: the `RuntimeLauncher` gRPC client over UDS. Later milestones add the state
//! manager, request builder, checkpoint orchestrator, port manager, and the three
//! start paths.

pub mod checkpoint_orchestrator;
pub mod executor_select;
pub mod launcher_client;
pub mod param_extract;
pub mod request_builder;
pub mod runtime_state_manager;
pub mod sandbox_executor;
pub mod start_guard;

pub use checkpoint_orchestrator::{CheckpointOrchestrator, CkptFileManager};
pub use param_extract::{
    extract_from_config, extract_start, parse_sandbox_config, ExtractedStart, SandboxConfig,
};
pub use executor_select::{
    select_executor, select_start_path, ExecutorKind, StartPath, EXECUTOR_TYPE_CONTAINER,
    EXECUTOR_TYPE_RUNTIME,
};
pub use launcher_client::{LauncherClient, CONTAINER_EP_ENV};
pub use request_builder::{
    build_start_request, encode_port_mapping, parse_forward_ports, PortForward, RootfsSpec,
    SandboxStartParams,
};
pub use runtime_state_manager::{RuntimeStateManager, SandboxInfo};
pub use sandbox_executor::{SandboxExecutor, SandboxStarted};
pub use start_guard::SandboxStartGuard;
