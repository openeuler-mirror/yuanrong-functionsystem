//! RM-local instance registry view: health probes and resource rows (C++ `runtimeInstanceInfoMap_` style).

use crate::instance_health;
use crate::metrics::{InstanceMetric, MetricsCollector};
use crate::state::{RunningProcess, RuntimeManagerState};
use std::sync::Arc;
use std::time::Duration;

#[derive(Debug, Clone)]
pub struct InstanceHealthRow {
    pub runtime_id: String,
    pub instance_id: String,
    pub pid: i32,
    pub label: String,
}

#[derive(Debug, Clone)]
pub struct InstanceResourceRow {
    pub instance_id: String,
    pub runtime_id: String,
    pub pid: i32,
    pub rss_kb: u64,
    pub port: u16,
    pub cgroup_memory_bytes: Option<(u64, Option<u64>)>,
}

/// Local tracking and introspection for runtimes owned by this process.
pub struct InstanceManager {
    state: Arc<RuntimeManagerState>,
}

impl InstanceManager {
    pub fn new(state: Arc<RuntimeManagerState>) -> Self {
        Self { state }
    }

    pub fn list_local(&self) -> Vec<RunningProcess> {
        self.state
            .list_runtime_ids()
            .into_iter()
            .filter_map(|rid| self.state.get_by_runtime(&rid))
            .collect()
    }

    pub async fn health_snapshot(&self) -> Vec<InstanceHealthRow> {
        let default_grace =
            Duration::from_secs(self.state.config.manager_startup_probe_secs.max(1));
        let mut out = Vec::new();
        for rid in self.state.list_runtime_ids() {
            let Some(p) = self.state.get_by_runtime(&rid) else {
                continue;
            };
            let label = instance_health::evaluate(
                p.pid,
                &p.health_spec,
                p.started_at,
                default_grace,
            )
            .await;
            out.push(InstanceHealthRow {
                runtime_id: rid,
                instance_id: p.instance_id,
                pid: p.pid,
                label: label.to_string(),
            });
        }
        out
    }

    pub fn collect_resource_usage(&self) -> Vec<InstanceResourceRow> {
        let mut col = MetricsCollector::new();
        let snap = col.collect(&self.state);
        snap.instances
            .into_iter()
            .map(|i: InstanceMetric| {
                let cgroup_mem = self
                    .state
                    .get_by_runtime(&i.runtime_id)
                    .as_ref()
                    .and_then(|p| p.cgroup_path.as_ref())
                    .and_then(|cg| crate::oom::cgroup::read_cgroup_memory(cg));
                InstanceResourceRow {
                    instance_id: i.instance_id,
                    runtime_id: i.runtime_id,
                    pid: i.pid,
                    rss_kb: i.rss_kb,
                    port: i.port,
                    cgroup_memory_bytes: cgroup_mem,
                }
            })
            .collect()
    }
}
