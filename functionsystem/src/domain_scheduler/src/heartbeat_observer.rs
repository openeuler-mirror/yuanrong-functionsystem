//! Heartbeat-driven health tracking; ties staleness checks to [`AbnormalProcessor`].

use std::sync::Arc;

use crate::abnormal_processor::AbnormalProcessor;
use crate::nodes::LocalNodeManager;
use crate::resource_view::ResourceView;

pub struct HeartbeatObserver {
    staleness_ms: i64,
    abnormal: Arc<AbnormalProcessor>,
}

impl HeartbeatObserver {
    pub fn new(staleness_ms: i64, abnormal: Arc<AbnormalProcessor>) -> Self {
        Self {
            staleness_ms,
            abnormal,
        }
    }

    /// Run from the domain housekeeping tick; forwards newly stale nodes to the abnormal processor.
    pub fn tick(&self, nodes: &LocalNodeManager, resource_view: &ResourceView) {
        let stale = nodes.check_heartbeat_staleness(self.staleness_ms);
        for nid in stale {
            self.abnormal
                .on_heartbeat_timeout(&nid, nodes, resource_view);
        }
    }
}
