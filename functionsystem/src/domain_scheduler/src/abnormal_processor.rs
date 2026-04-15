//! Detect abnormal local schedulers and coordinate migration / global notification hooks.

use std::collections::VecDeque;
use std::sync::Arc;

use parking_lot::Mutex;
use tracing::{info, warn};

use crate::nodes::LocalNodeManager;
use crate::resource_view::ResourceView;

#[derive(Debug, Clone)]
pub struct MigrationTask {
    pub instance_id: String,
    pub from_node: String,
    pub reason: String,
}

/// Hook surface for notifying the global scheduler (e.g. agent eviction / rebalance).
pub trait GlobalSchedulerNotifier: Send + Sync {
    fn notify_abnormal_worker(&self, node_id: &str, detail: &str);
}

/// Handles abnormal local scheduler transitions (C++ abnormal processor subset).
pub struct AbnormalProcessor {
    migrations: Mutex<VecDeque<MigrationTask>>,
    global: Mutex<Option<Arc<dyn GlobalSchedulerNotifier>>>,
}

impl AbnormalProcessor {
    pub fn new() -> Self {
        Self {
            migrations: Mutex::new(VecDeque::new()),
            global: Mutex::new(None),
        }
    }

    pub fn set_global_notifier(&self, n: Option<Arc<dyn GlobalSchedulerNotifier>>) {
        *self.global.lock() = n;
    }

    /// Called when heartbeat observer detects prolonged silence from a local scheduler.
    pub fn on_heartbeat_timeout(
        &self,
        node_id: &str,
        _nodes: &LocalNodeManager,
        resource_view: &ResourceView,
    ) {
        warn!(%node_id, "abnormal processor: heartbeat timeout (node already marked unhealthy)");
        if let Some(g) = self.global.lock().as_ref() {
            g.notify_abnormal_worker(node_id, "heartbeat_timeout");
        }
        self.enqueue_migrations_for_node(node_id, resource_view);
    }

    fn enqueue_migrations_for_node(&self, node_id: &str, resource_view: &ResourceView) {
        // Without per-instance index, enqueue a synthetic task for operators / future wiring.
        let mut q = self.migrations.lock();
        q.push_back(MigrationTask {
            instance_id: String::new(),
            from_node: node_id.to_string(),
            reason: "node_abnormal".into(),
        });
        drop(q);
        info!(%node_id, nodes = ?resource_view.node_ids(), "migration queue: node failure recorded");
    }

    pub fn pop_migration(&self) -> Option<MigrationTask> {
        self.migrations.lock().pop_front()
    }

    pub fn migration_pending_count(&self) -> usize {
        self.migrations.lock().len()
    }
}

impl Default for AbnormalProcessor {
    fn default() -> Self {
        Self::new()
    }
}
