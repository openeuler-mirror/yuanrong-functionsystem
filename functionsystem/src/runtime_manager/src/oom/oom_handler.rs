//! cgroup memory limit refresh helpers and user-space OOM kill loop wiring.

use crate::agent::AgentClient;
use crate::state::RuntimeManagerState;
use std::path::Path;
use std::sync::Arc;

/// Re-apply cgroup v2 `memory.max` from a limit in MiB (same formula as spawn-time isolate).
pub fn refresh_cgroup_memory_limit(cgroup: &Path, mem_mb: f64) -> std::io::Result<()> {
    super::cgroup::write_memory_max_from_mb(cgroup, mem_mb)
}

pub fn spawn_user_space_oom_supervision(state: Arc<RuntimeManagerState>, agent: Arc<AgentClient>) {
    tokio::spawn(super::monitor::supervision_loop(state, agent));
}
