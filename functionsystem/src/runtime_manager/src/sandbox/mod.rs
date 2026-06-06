//! CONTAINER execution backend (sandbox / containerd) — port of the C++
//! `runtime_manager/executor/sandbox/*`. See
//! `docs/analysis/173-sandbox-executor-rust-design.md`.
//!
//! M1: the `RuntimeLauncher` gRPC client over UDS. Later milestones add the state
//! manager, request builder, checkpoint orchestrator, port manager, and the three
//! start paths.

pub mod launcher_client;
pub mod runtime_state_manager;
pub mod start_guard;

pub use launcher_client::{LauncherClient, CONTAINER_EP_ENV};
pub use runtime_state_manager::{RuntimeStateManager, SandboxInfo};
pub use start_guard::SandboxStartGuard;
