//! Port-backed capacity view for concurrent runtimes (process “pool” sizing).

use crate::state::RuntimeManagerState;
use std::sync::Arc;

#[derive(Debug, Clone, serde::Serialize)]
pub struct ProcessPoolSummary {
    pub port_capacity: u32,
    pub ports_allocated: usize,
    pub runtimes_tracked: usize,
}

pub fn summarize(state: &Arc<RuntimeManagerState>) -> ProcessPoolSummary {
    ProcessPoolSummary {
        port_capacity: state.ports.capacity(),
        ports_allocated: state.ports.allocated_count(),
        runtimes_tracked: state.list_runtime_ids().len(),
    }
}
