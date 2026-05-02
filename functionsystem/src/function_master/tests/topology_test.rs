//! In-memory topology manager behavior (no etcd).

use std::sync::Arc;

use yr_common::etcd_keys::{with_prefix, SCHEDULER_TOPOLOGY};
use yr_master::config::{AssignmentStrategy, ElectionMode, MasterConfig};
use yr_master::topology::TopologyManager;
use yr_proto::resources::ResourceUnit as ProtoResourceUnit;

fn test_config(strategy: AssignmentStrategy, max_per_domain: u32) -> Arc<MasterConfig> {
    Arc::new(MasterConfig {
        host: "0.0.0.0".into(),
        port: 8400,
        http_port: 8480,
        etcd_endpoints: vec![],
        etcd_table_prefix: String::new(),
        cluster_id: "test-cluster".into(),
        election_mode: ElectionMode::Standalone,
        max_locals_per_domain: max_per_domain,
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

#[tokio::test]
async fn register_node_assigns_domain_and_persists_record() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    let tm = TopologyManager::new(cfg.clone(), None);
    let (addr, rec) = tm
        .register_local(
            "node-1".into(),
            "10.0.0.1:1".into(),
            r#"{"capacity":{"cpu":4},"used":{"cpu":0}}"#.into(),
            None,
            "{}".into(),
        )
        .await;
    assert_eq!(addr, cfg.default_domain_address);
    assert_eq!(rec.node_id, "node-1");
    assert_eq!(rec.address, "10.0.0.1:1");
    assert_eq!(rec.domain_address, cfg.default_domain_address);
    assert_eq!(tm.agent_count(), 1);
}

#[tokio::test]
async fn round_robin_spills_to_new_domain_when_full() {
    // C++ enforces `MIN_SCHED_PER_DOMAIN_NODE = 2`; `SchedTree` clamps `max_locals` to at least 2.
    let cfg = test_config(AssignmentStrategy::RoundRobin, 2);
    let tm = TopologyManager::new(cfg, None);
    let (_, a) = tm
        .register_local("n-a".into(), "a:1".into(), "{}".into(), None, "{}".into())
        .await;
    let (_, b) = tm
        .register_local("n-b".into(), "b:1".into(), "{}".into(), None, "{}".into())
        .await;
    assert_eq!(a.domain_id, b.domain_id);
    let (_, c) = tm
        .register_local("n-c".into(), "c:1".into(), "{}".into(), None, "{}".into())
        .await;
    assert_ne!(c.domain_id, a.domain_id);
    assert!(c.domain_id.starts_with("slot-"));
}

#[tokio::test]
async fn least_loaded_prefers_domain_with_fewer_locals() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 2);
    let tm = TopologyManager::new(cfg.clone(), None);
    let (_, first) = tm
        .register_local("p1".into(), "h1".into(), "{}".into(), None, "{}".into())
        .await;
    let (_, second) = tm
        .register_local("p2".into(), "h2".into(), "{}".into(), None, "{}".into())
        .await;
    assert_eq!(first.domain_id, second.domain_id);

    tm.evict("p1").await;
    let (_, third) = tm
        .register_local("p3".into(), "h3".into(), "{}".into(), None, "{}".into())
        .await;
    assert_eq!(third.domain_id, second.domain_id);
}

#[tokio::test]
async fn evict_removes_node() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    let tm = TopologyManager::new(cfg, None);
    tm.register_local("z1".into(), "z:1".into(), "{}".into(), None, "{}".into())
        .await;
    assert!(tm.evict("z1").await);
    assert_eq!(tm.agent_count(), 0);
    assert!(!tm.evict("z1").await);
}

#[tokio::test]
async fn query_agents_json_filter() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    let tm = TopologyManager::new(cfg, None);
    tm.register_local(
        "alpha-1".into(),
        "10.0.0.2".into(),
        "{}".into(),
        None,
        "{}".into(),
    )
    .await;
    tm.register_local(
        "beta-2".into(),
        "10.0.0.3".into(),
        "{}".into(),
        None,
        "{}".into(),
    )
    .await;
    let json = tm.list_agents_json("alpha");
    assert!(json.contains("alpha-1"));
    assert!(!json.contains("beta-2"));
}

#[tokio::test]
async fn resource_summary_aggregates_node_count_and_samples() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    let tm = TopologyManager::new(cfg, None);
    tm.register_local(
        "n1".into(),
        "h1".into(),
        r#"{"cpu":2}"#.into(),
        None,
        "{}".into(),
    )
    .await;
    tm.register_local(
        "n2".into(),
        "h2".into(),
        r#"{"cpu":4}"#.into(),
        None,
        "{}".into(),
    )
    .await;
    let v = tm.resource_summary_json();
    assert_eq!(v["node_count"], serde_json::json!(2));
    let nodes = v["nodes"].as_array().unwrap();
    assert_eq!(nodes.len(), 2);
}

#[tokio::test]
async fn agent_count_tracks_registrations() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    let tm = TopologyManager::new(cfg, None);
    assert_eq!(tm.agent_count(), 0);
    tm.register_local("a".into(), "1".into(), "{}".into(), None, "{}".into())
        .await;
    tm.register_local("b".into(), "2".into(), "{}".into(), None, "{}".into())
        .await;
    assert_eq!(tm.agent_count(), 2);
}

#[tokio::test]
async fn root_domain_present_after_local_registration() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    let tm = TopologyManager::new(cfg.clone(), None);
    tm.register_local("leaf".into(), "addr".into(), "{}".into(), None, "{}".into())
        .await;
    let root = tm.root_domain();
    assert!(root.is_some());
    let (_name, addr) = root.unwrap();
    assert_eq!(addr, cfg.default_domain_address);
}

#[tokio::test]
async fn update_resources_succeeds_for_known_agent() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    let tm = TopologyManager::new(cfg, None);
    tm.register_local(
        "agent".into(),
        "h".into(),
        r#"{"a":1}"#.into(),
        None,
        "{}".into(),
    )
    .await;
    assert!(
        tm.update_resources("agent", r#"{"a":2}"#.into(), None)
            .await
    );
}

#[tokio::test]
async fn update_resources_fails_for_unknown_agent() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    let tm = TopologyManager::new(cfg, None);
    assert!(!tm.update_resources("missing", "{}".into(), None).await);
}

#[tokio::test]
async fn list_agents_json_empty_filter_returns_all_sorted() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    let tm = TopologyManager::new(cfg, None);
    tm.register_local("z".into(), "1".into(), "{}".into(), None, "{}".into())
        .await;
    tm.register_local("a".into(), "2".into(), "{}".into(), None, "{}".into())
        .await;
    let json = tm.list_agents_json("");
    assert!(json.starts_with('['));
    let pos_a = json.find("\"a\"").unwrap();
    let pos_z = json.find("\"z\"").unwrap();
    assert!(pos_a < pos_z);
}

#[test]
fn topology_logical_key_matches_scheduler_topology_constant() {
    assert_eq!(MasterConfig::topology_logical_key(), SCHEDULER_TOPOLOGY);
}

#[test]
fn topology_key_with_empty_table_prefix_is_logical_key() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    assert_eq!(cfg.topology_key(), SCHEDULER_TOPOLOGY);
}

#[test]
fn topology_key_prefixes_table_id_without_double_slash() {
    let mut cfg = (*test_config(AssignmentStrategy::LeastLoaded, 64)).clone();
    cfg.etcd_table_prefix = "/mytbl".into();
    assert_eq!(cfg.topology_key(), "/mytbl/scheduler/topology");
    cfg.etcd_table_prefix = "/mytbl/".into();
    assert_eq!(cfg.topology_key(), "/mytbl/scheduler/topology");
}

#[tokio::test]
async fn domain_id_slot_naming_on_first_registration() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    let tm = TopologyManager::new(cfg, None);
    let (_, rec) = tm
        .register_local("n1".into(), "h1".into(), "{}".into(), None, "{}".into())
        .await;
    assert!(rec.domain_id.starts_with("slot-"));
}

#[tokio::test]
async fn register_local_preserves_resource_json_for_aggregation() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    let tm = TopologyManager::new(cfg, None);
    let r = r#"{"capacity":{"cpu":8}}"#;
    let (_, rec) = tm
        .register_local("agent-1".into(), "h".into(), r.into(), None, "{}".into())
        .await;
    assert_eq!(rec.resource_json, r);
    let summary = tm.resource_summary_json();
    assert_eq!(summary["node_count"], serde_json::json!(1));
    let nodes = summary["nodes"].as_array().unwrap();
    assert_eq!(nodes[0]["resource_json"], serde_json::json!(r));
}

#[tokio::test]
async fn register_local_preserves_agent_info_json() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    let tm = TopologyManager::new(cfg, None);
    let info = r#"{"labels":{"zone":"a"}}"#;
    let (_, rec) = tm
        .register_local("x".into(), "h".into(), "{}".into(), None, info.into())
        .await;
    assert_eq!(rec.agent_info_json, info);
}

#[tokio::test]
async fn register_local_without_resource_unit_preserves_existing_authoritative_unit() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    let tm = TopologyManager::new(cfg, None);
    let (_, first) = tm
        .register_local(
            "node-u".into(),
            "h".into(),
            "{}".into(),
            Some(ProtoResourceUnit {
                id: "node-u".into(),
                ..Default::default()
            }),
            "{}".into(),
        )
        .await;
    assert!(!first.resource_unit_b64.is_empty());

    let (_, second) = tm
        .register_local("node-u".into(), "h".into(), "{}".into(), None, "{}".into())
        .await;
    assert_eq!(second.resource_unit_b64, first.resource_unit_b64);
}

#[tokio::test]
async fn update_resources_changes_resource_summary_sample() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    let tm = TopologyManager::new(cfg, None);
    tm.register_local(
        "n".into(),
        "h".into(),
        r#"{"v":1}"#.into(),
        None,
        "{}".into(),
    )
    .await;
    assert!(tm.update_resources("n", r#"{"v":2}"#.into(), None).await);
    let summary = tm.resource_summary_json();
    let nodes = summary["nodes"].as_array().unwrap();
    assert!(nodes.iter().any(|x| x["resource_json"] == "{\"v\":2}"));
}

#[tokio::test]
async fn topology_json_exposes_domain_and_address_per_agent() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    let tm = TopologyManager::new(cfg.clone(), None);
    tm.register_local(
        "leaf".into(),
        "10.0.0.5:1".into(),
        "{}".into(),
        None,
        "{}".into(),
    )
    .await;
    let v: serde_json::Value = serde_json::from_str(&tm.topology_json()).unwrap();
    let locals = v["locals"].as_array().unwrap();
    assert_eq!(locals.len(), 1);
    assert_eq!(locals[0]["node_id"], "leaf");
    assert_eq!(locals[0]["domain_address"], cfg.default_domain_address);
    assert!(locals[0]["domain_id"]
        .as_str()
        .unwrap()
        .starts_with("slot-"));
}

#[test]
fn persisted_topology_key_matches_with_prefix_helper() {
    let logical = MasterConfig::topology_logical_key();
    assert_eq!(with_prefix("", logical), logical);
    assert_eq!(with_prefix("/p", logical), "/p/scheduler/topology");
}

#[tokio::test]
async fn resource_summary_node_count_drops_after_evict() {
    let cfg = test_config(AssignmentStrategy::LeastLoaded, 64);
    let tm = TopologyManager::new(cfg, None);
    tm.register_local("keep".into(), "h1".into(), "{}".into(), None, "{}".into())
        .await;
    tm.register_local("gone".into(), "h2".into(), "{}".into(), None, "{}".into())
        .await;
    assert_eq!(
        tm.resource_summary_json()["node_count"],
        serde_json::json!(2)
    );
    assert!(tm.evict("gone").await);
    assert_eq!(
        tm.resource_summary_json()["node_count"],
        serde_json::json!(1)
    );
}
