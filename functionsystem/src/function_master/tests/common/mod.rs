//! Shared helpers for yr-master integration tests.
#![allow(dead_code)]

use std::collections::VecDeque;
use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use std::time::Duration;

use yr_master::config::{AssignmentStrategy, ElectionMode, MasterConfig};
use yr_master::domain_activator::DomainActivator;
use yr_master::domain_sched_mgr::DomainSchedMgr;
use yr_master::instances::InstanceManager;
use yr_master::local_sched_mgr::LocalSchedMgr;
use yr_master::node_manager::NodeManager;
use yr_master::sched_tree::SchedTree;
use yr_master::schedule_decision::ScheduleDecisionManager;
use yr_master::schedule_manager::ScheduleManager;
use yr_master::scheduler::MasterState;
use yr_master::snapshot::SnapshotManager;
use yr_master::system_func_loader::SystemFunctionLoader;
use yr_master::topology::TopologyManager;

pub fn test_master_state() -> Arc<MasterState> {
    let cfg = Arc::new(MasterConfig {
        host: "0.0.0.0".into(),
        port: 8400,
        http_port: 8480,
        etcd_endpoints: vec![],
        etcd_table_prefix: String::new(),
        cluster_id: "test".into(),
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
        function_meta_path: "/home/sn/function-metas".into(),
        enable_sync_sys_func: false,
        meta_store_mode: "local".into(),
        meta_store_max_flush_concurrency: 100,
        meta_store_max_flush_batch_size: 50,
        ssl_enable: String::new(),
        metrics_ssl_enable: String::new(),
        ssl_base_path: String::new(),
        ssl_root_file: String::new(),
        ssl_cert_file: String::new(),
        ssl_key_file: String::new(),
        aggregated_schedule_strategy: "no_aggregate".into(),
        sched_max_priority: 16,
        instance_id: "test-master".into(),
    });

    let is_leader = Arc::new(AtomicBool::new(true));
    let tree = Arc::new(SchedTree::new(
        cfg.max_locals_per_domain as usize,
        cfg.max_domain_sched_per_domain as usize,
    ));
    let topo = Arc::new(TopologyManager::new(cfg.clone(), None));
    let snapshots = SnapshotManager::new();
    let instances = Arc::new(InstanceManager::new(
        is_leader.clone(),
        snapshots.clone(),
        None,
    ));
    let dsm = Arc::new(DomainSchedMgr::new(
        Duration::from_secs(5),
        Duration::from_secs(10),
    ));
    let lsm = Arc::new(LocalSchedMgr::new(Duration::from_secs(30), 3));
    let da = Arc::new(DomainActivator::new(tree));
    let sl = Arc::new(SystemFunctionLoader::new(cfg.clone()));
    let queue = Arc::new(parking_lot::Mutex::new(VecDeque::new()));

    let schedule_mgr = ScheduleManager::new(&cfg);
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

pub fn test_master_config(cluster_id: &str) -> Arc<MasterConfig> {
    Arc::new(MasterConfig {
        host: "0.0.0.0".into(),
        port: 8400,
        http_port: 8480,
        etcd_endpoints: vec![],
        etcd_table_prefix: String::new(),
        cluster_id: cluster_id.into(),
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
        node_id: String::new(),
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
        function_meta_path: "/home/sn/function-metas".into(),
        enable_sync_sys_func: false,
        meta_store_mode: "local".into(),
        meta_store_max_flush_concurrency: 100,
        meta_store_max_flush_batch_size: 50,
        ssl_enable: String::new(),
        metrics_ssl_enable: String::new(),
        ssl_base_path: String::new(),
        ssl_root_file: String::new(),
        ssl_cert_file: String::new(),
        ssl_key_file: String::new(),
        aggregated_schedule_strategy: "no_aggregate".into(),
        sched_max_priority: 16,
        instance_id: "test-master".into(),
    })
}
