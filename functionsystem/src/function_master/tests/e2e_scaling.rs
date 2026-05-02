//! Scenario 7 — elastic scaling: proxy registration, placement, capacity growth, eviction.

mod common;

use std::sync::atomic::Ordering;
use std::sync::Arc;

use serde_json::json;
use yr_master::config::{AssignmentStrategy, ElectionMode, MasterConfig};
use yr_master::topology::TopologyManager;
use yr_proto::internal::ScheduleRequest;

use common::test_master_state;

fn topo_config(strategy: AssignmentStrategy, max_locals: u32) -> Arc<MasterConfig> {
    Arc::new(MasterConfig {
        host: "0.0.0.0".into(),
        port: 8400,
        http_port: 8480,
        etcd_endpoints: vec![],
        etcd_table_prefix: String::new(),
        cluster_id: "e2e-scale".into(),
        election_mode: ElectionMode::Standalone,
        max_locals_per_domain: max_locals,
        max_domain_sched_per_domain: 1000,
        schedule_retry_sec: 10,
        domain_schedule_timeout_ms: 5000,
        enable_meta_store: false,
        meta_store_address: String::new(),
        meta_store_port: 2389,
        assignment_strategy: strategy,
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
        enable_horizontal_scale: true,
        pool_config_path: String::new(),
        domain_heartbeat_timeout: 6000,
        system_tenant_id: "0".into(),
        services_path: "/".into(),
        lib_path: "/".into(),
        function_meta_path: "/tmp/fm".into(),
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

#[tokio::test]
async fn e2e_scaling_register_proxies_and_verify_topology() {
    let state = test_master_state();
    state
        .topology
        .register_local(
            "proxy-1".into(),
            "10.0.1.1:9001".into(),
            r#"{"capacity":{"cpu":4}}"#.into(),
            None,
            "{}".into(),
        )
        .await;
    state
        .topology
        .register_local(
            "proxy-2".into(),
            "10.0.1.2:9001".into(),
            r#"{"capacity":{"cpu":4}}"#.into(),
            None,
            "{}".into(),
        )
        .await;
    assert_eq!(state.topology.agent_count(), 2);
    let snap: serde_json::Value = serde_json::from_str(&state.topology.topology_json()).unwrap();
    let locals = snap["locals"].as_array().unwrap();
    assert_eq!(locals.len(), 2);
    let ids: Vec<&str> = locals
        .iter()
        .map(|v| v["node_id"].as_str().unwrap())
        .collect();
    assert!(ids.contains(&"proxy-1"));
    assert!(ids.contains(&"proxy-2"));
}

#[tokio::test]
async fn e2e_scaling_schedule_enqueues_when_proxies_present() {
    let state = test_master_state();
    state
        .topology
        .register_local(
            "proxy-a".into(),
            "10.0.2.1:1".into(),
            "{}".into(),
            None,
            "{}".into(),
        )
        .await;
    let r = state
        .clone()
        .do_schedule(ScheduleRequest {
            request_id: "sched-scale-1".into(),
            ..Default::default()
        })
        .await;
    assert!(!r.success);
    let q = state.scheduling_queue.lock();
    assert!(q.contains(&"sched-scale-1".to_string()));
}

#[tokio::test]
async fn e2e_scaling_add_proxy_rebalances_round_robin_domains() {
    let cfg = topo_config(AssignmentStrategy::RoundRobin, 2);
    let tm = TopologyManager::new(cfg, None);
    let (_, n1) = tm
        .register_local("rr-1".into(), "h1".into(), "{}".into(), None, "{}".into())
        .await;
    let (_, n2) = tm
        .register_local("rr-2".into(), "h2".into(), "{}".into(), None, "{}".into())
        .await;
    assert_eq!(n1.domain_id, n2.domain_id);
    let (_, n3) = tm
        .register_local("rr-3".into(), "h3".into(), "{}".into(), None, "{}".into())
        .await;
    assert_ne!(n3.domain_id, n1.domain_id);
    assert!(n3.domain_id.starts_with("slot-"));
}

#[tokio::test]
async fn e2e_scaling_evict_proxy_removes_node_instances_remain_queryable() {
    let state = test_master_state();
    state
        .topology
        .register_local(
            "proxy-drop".into(),
            "10.0.3.1:1".into(),
            "{}".into(),
            None,
            "{}".into(),
        )
        .await;
    state.instances.upsert_instance(
        "/instances/on-drop",
        json!({
            "id": "on-drop",
            "tenant": "t1",
            "node_id": "proxy-drop",
            "function_proxy_id": "proxy-drop",
            "state": "RUNNING",
        }),
    );
    assert!(state.topology.evict("proxy-drop").await);
    assert_eq!(state.topology.agent_count(), 0);
    assert_eq!(state.instances.count(), 1);
    assert!(state.instances.try_forward_or_kill("on-drop"));
    state
        .is_leader
        .store(false, std::sync::atomic::Ordering::SeqCst);
    assert!(!state.instances.try_forward_or_kill("on-drop"));
}

#[tokio::test]
async fn e2e_scaling_follower_master_blocks_schedule_after_eviction_context() {
    let state = test_master_state();
    state
        .topology
        .register_local(
            "px".into(),
            "10.0.4.1:1".into(),
            "{}".into(),
            None,
            "{}".into(),
        )
        .await;
    state.is_leader.store(false, Ordering::SeqCst);
    let r = state
        .clone()
        .do_schedule(ScheduleRequest {
            request_id: "no-leader".into(),
            ..Default::default()
        })
        .await;
    assert!(!r.success);
    assert_eq!(r.error_code, 9);
}
