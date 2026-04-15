//! User-space OOM policy and cgroup memory helpers (C++ metrics / healthcheck OOM path).

pub mod cgroup;
pub mod monitor;
pub mod oom_handler;

pub use oom_handler::spawn_user_space_oom_supervision;
