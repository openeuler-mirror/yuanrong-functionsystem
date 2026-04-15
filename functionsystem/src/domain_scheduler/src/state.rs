use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use tokio::sync::Mutex as AsyncMutex;

use crate::abnormal_processor::AbnormalProcessor;
use crate::config::{DomainSchedulerConfig, ElectionMode};
use crate::heartbeat_observer::HeartbeatObserver;
use crate::nodes::LocalNodeManager;
use crate::resource_view::ResourceView;
use crate::scheduler::SchedulingEngine;
use crate::worker_status::WorkerStatusPropagator;
use yr_metastore_client::MetaStoreClient;

pub struct DomainSchedulerState {
    pub config: Arc<DomainSchedulerConfig>,
    pub resource_view: Arc<ResourceView>,
    pub nodes: Arc<LocalNodeManager>,
    pub scheduler: Arc<SchedulingEngine>,
    is_leader: Arc<AtomicBool>,
    pub metastore: Option<Arc<AsyncMutex<MetaStoreClient>>>,
    pub abnormal: Arc<AbnormalProcessor>,
    pub heartbeat_observer: Arc<HeartbeatObserver>,
    pub worker_propagator: Arc<WorkerStatusPropagator>,
}

impl DomainSchedulerState {
    pub fn new(
        config: Arc<DomainSchedulerConfig>,
        resource_view: Arc<ResourceView>,
        nodes: Arc<LocalNodeManager>,
        scheduler: Arc<SchedulingEngine>,
        metastore: Option<Arc<AsyncMutex<MetaStoreClient>>>,
    ) -> Arc<Self> {
        let is_leader = Arc::new(AtomicBool::new(matches!(
            config.election_mode,
            ElectionMode::Standalone
        )));
        let abnormal = Arc::new(AbnormalProcessor::new());
        let staleness_ms = (config.pull_resource_interval_ms as i64).saturating_mul(3).max(1500);
        let heartbeat_observer = Arc::new(HeartbeatObserver::new(staleness_ms, abnormal.clone()));
        Arc::new(Self {
            config,
            resource_view,
            nodes,
            scheduler,
            is_leader,
            metastore,
            abnormal,
            heartbeat_observer,
            worker_propagator: Arc::new(WorkerStatusPropagator::new()),
        })
    }

    pub fn require_leader(&self) -> bool {
        self.is_leader.load(Ordering::SeqCst)
    }

    pub fn set_leader(&self, v: bool) {
        self.is_leader.store(v, Ordering::SeqCst);
    }

    pub fn is_leader(&self) -> bool {
        self.require_leader()
    }

    pub fn group_meta_prefix(&self) -> String {
        format!("{}/meta", self.config.domain_key_base())
    }
}
