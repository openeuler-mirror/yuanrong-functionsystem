//! Worker status propagation helpers (resource + health fan-out toward global scheduler).

use std::sync::Arc;

use tracing::debug;

use crate::nodes::LocalNodeManager;

/// Optional bridge when the domain scheduler should push condensed worker state upstream.
pub trait GlobalResourceSink: Send + Sync {
    fn push_worker_snapshot(&self, node_id: &str, resource_json: &str, status: &str);
}

pub struct WorkerStatusPropagator {
    sink: parking_lot::Mutex<Option<Arc<dyn GlobalResourceSink>>>,
}

impl WorkerStatusPropagator {
    pub fn new() -> Self {
        Self {
            sink: parking_lot::Mutex::new(None),
        }
    }

    pub fn set_sink(&self, sink: Option<Arc<dyn GlobalResourceSink>>) {
        *self.sink.lock() = sink;
    }

    /// Called after `LocalNodeManager::notify_worker_status` updates local state.
    pub fn on_worker_status(&self, node_id: &str, status: &str, reason: &str, nodes: &LocalNodeManager) {
        if let Some(s) = self.sink.lock().as_ref() {
            let summary = nodes.list_nodes_summary();
            debug!(%node_id, %status, ?summary, "worker status propagator: local view updated");
            if !reason.is_empty() {
                s.push_worker_snapshot(node_id, reason, status);
            }
        }
    }
}

impl Default for WorkerStatusPropagator {
    fn default() -> Self {
        Self::new()
    }
}
