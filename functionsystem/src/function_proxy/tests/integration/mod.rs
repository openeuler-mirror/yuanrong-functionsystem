//! Shared helpers for functionsystem integration tests.

mod master_proxy_flow;
mod proxy_agent_flow;
mod proxy_runtime_flow;
mod e2e_scenario;

pub use crate::common::{make_proxy_config, new_bus};

use std::collections::VecDeque;
use std::sync::atomic::AtomicBool;
use std::sync::Arc;

use yr_master::config::{AssignmentStrategy, ElectionMode, MasterConfig};
use yr_master::domain_activator::DomainActivator;
use yr_master::domain_sched_mgr::DomainSchedMgr;
use yr_master::instances::InstanceManager;
use yr_master::local_sched_mgr::LocalSchedMgr;
use yr_master::node_manager::NodeManager;
use yr_master::schedule_decision::ScheduleDecisionManager;
use yr_master::schedule_manager::ScheduleManager;
use yr_master::scheduler::MasterState;
use yr_master::snapshot::SnapshotManager;
use yr_master::system_func_loader::SystemFunctionLoader;
use yr_master::topology::TopologyManager;

/// In-memory master graph + HTTP-visible state (no etcd), matching yr-master test helpers.
pub fn test_master_state() -> Arc<MasterState> {
    let cfg = Arc::new(MasterConfig {
        host: "0.0.0.0".into(),
        port: 8400,
        http_port: 8480,
        etcd_endpoints: vec![],
        etcd_table_prefix: String::new(),
        cluster_id: "test-cluster".into(),
        election_mode: ElectionMode::Standalone,
        max_locals_per_domain: 64,
        max_domain_sched_per_domain: 1000,
        schedule_retry_sec: 10,
        domain_schedule_timeout_ms: 5000,
        enable_meta_store: false,
        meta_store_address: String::new(),
        meta_store_port: 2389,
        assignment_strategy: AssignmentStrategy::LeastLoaded,
        default_domain_address: "127.0.0.1:8401".into(),
        node_id: "test-master".into(),
        enable_persistence: false,
        runtime_recover_enable: false,
        is_schedule_tolerate_abnormal: true,
        decrypt_algorithm: "NO_CRYPTO".into(),
        schedule_plugins: String::new(),
        migrate_enable: false,
        grace_period_seconds: 25,
        health_monitor_max_failure: 5,
        health_monitor_retry_interval: 3000,
        enable_horizontal_scale: false,
        pool_config_path: String::new(),
        domain_heartbeat_timeout: 6000,
        system_tenant_id: "0".into(),
        services_path: "/".into(),
        lib_path: "/".into(),
        function_meta_path: "/tmp/function-metas".into(),
        enable_sync_sys_func: false,
        meta_store_mode: "local".into(),
        meta_store_max_flush_concurrency: 100,
        meta_store_max_flush_batch_size: 50,
        aggregated_schedule_strategy: "no_aggregate".into(),
        sched_max_priority: 16,
        instance_id: "test-master".into(),
    });

    let is_leader = Arc::new(AtomicBool::new(true));
    let topo = Arc::new(TopologyManager::new(cfg.clone(), None));
    let snapshots = SnapshotManager::new();
    let instances = Arc::new(InstanceManager::new(
        is_leader.clone(),
        snapshots.clone(),
        None,
    ));
    let dsm = Arc::new(DomainSchedMgr::new(
        std::time::Duration::from_secs(5),
        std::time::Duration::from_secs(10),
    ));
    let lsm = Arc::new(LocalSchedMgr::new(std::time::Duration::from_secs(30), 3));
    let da = Arc::new(DomainActivator::new(topo.sched_tree()));
    let sl = Arc::new(SystemFunctionLoader::new(cfg.clone()));
    let queue = Arc::new(parking_lot::Mutex::new(VecDeque::new()));

    let schedule_mgr = ScheduleManager::new(cfg.as_ref());
    let schedule_decision = ScheduleDecisionManager::new(schedule_mgr.clone());
    let node_manager = NodeManager::new();

    let state = MasterState::new(
        cfg,
        is_leader,
        topo,
        instances,
        dsm,
        lsm,
        da,
        sl,
        queue,
        snapshots,
        schedule_mgr.clone(),
        schedule_decision.clone(),
        node_manager,
    );
    if let Ok(h) = tokio::runtime::Handle::try_current() {
        schedule_mgr.wire_domain_performers(
            h,
            state.domain_sched_mgr.clone(),
            state.topology.clone(),
            state.is_leader.clone(),
        );
        schedule_decision.apply_topology_resources(&state.topology);
    }
    state.rebuild_domain_routes();
    state
}
