//! Bridges aggregated proxy resources into the scheduling strategy (`ScheduleDecisionManager` / resource-view path in C++).

use std::sync::Arc;

use yr_common::schedule::ScheduleStrategy;

use crate::resource_agg::ResourceAggregator;
use crate::schedule_manager::ScheduleManager;
use crate::topology::TopologyManager;

/// Wraps [`ScheduleManager`] and pushes [`ResourceViewInfo`] updates when topology changes.
pub struct ScheduleDecisionManager {
    schedule_mgr: Arc<ScheduleManager>,
}

impl ScheduleDecisionManager {
    pub fn new(schedule_mgr: Arc<ScheduleManager>) -> Arc<Self> {
        Arc::new(Self { schedule_mgr })
    }

    /// Refresh scheduler resource view from all registered proxy nodes (C++ resource aggregator → scheduler).
    pub fn apply_topology_resources(&self, topology: &TopologyManager) {
        let info = ResourceAggregator::resource_view_for_scheduler(topology);
        let mut s = self
            .schedule_mgr
            .scheduler()
            .strategy
            .lock()
            .expect("poisoned scheduler mutex");
        s.handle_resource_info_update(info);
    }
}
