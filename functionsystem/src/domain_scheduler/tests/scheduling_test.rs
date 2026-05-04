//! Resource view and scheduling engine unit tests (no network).

use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;

use base64::Engine;
use prost::Message;
use yr_domain_scheduler::config::{DomainSchedulerConfig, ElectionMode};
use yr_domain_scheduler::nodes::LocalNodeManager;
use yr_domain_scheduler::resource_view::ResourceView;
use yr_domain_scheduler::scheduler::SchedulingEngine;
use yr_domain_scheduler::scheduler_framework::{default_plugin_register, NodeInfo};
use yr_domain_scheduler::scheduler_framework::{ScheduleContext, SchedulerFramework};
use yr_proto::internal::ScheduleRequest;
use yr_proto::resources::value::{Counter, Scalar};
use yr_proto::resources::{Resource, ResourceUnit as ProtoResourceUnit, Resources};

fn sample_domain_config() -> Arc<DomainSchedulerConfig> {
    Arc::new(DomainSchedulerConfig {
        host: "127.0.0.1".into(),
        port: 8401,
        http_port: 8481,
        global_scheduler_address: String::new(),
        etcd_endpoints: vec![],
        etcd_table_prefix: String::new(),
        node_id: "domain-test".into(),
        election_mode: ElectionMode::Standalone,
        enable_preemption: false,
        max_priority: 100,
        pull_resource_interval_ms: 5000,
        ssl_enable: false,
        metrics_ssl_enable: false,
        ssl_base_path: String::new(),
        ssl_root_file: String::new(),
        ssl_cert_file: String::new(),
        ssl_key_file: String::new(),
        instance_id: "inst".into(),
    })
}

#[test]
fn resource_view_try_reserve_succeeds_when_capacity_available() {
    let view = ResourceView::new();
    view.upsert_node_resources(
        "n1",
        HashMap::from([("cpu".into(), 4.0)]),
        HashMap::from([("cpu".into(), 1.0)]),
    );
    let need = HashMap::from([("cpu".into(), 2.0)]);
    assert!(view.try_reserve("n1", "r1", &need, Duration::from_secs(60)));
}

#[test]
fn resource_view_try_reserve_fails_when_full() {
    let view = ResourceView::new();
    view.upsert_node_resources(
        "n1",
        HashMap::from([("cpu".into(), 2.0)]),
        HashMap::from([("cpu".into(), 1.9)]),
    );
    let need = HashMap::from([("cpu".into(), 0.5)]);
    assert!(!view.try_reserve("n1", "r1", &need, Duration::from_secs(60)));
}

#[test]
fn resource_view_commit_and_release() {
    let view = ResourceView::new();
    view.upsert_node_resources(
        "n1",
        HashMap::from([("cpu".into(), 10.0)]),
        HashMap::from([("cpu".into(), 0.0)]),
    );
    let need = HashMap::from([("cpu".into(), 3.0)]);
    assert!(view.try_reserve("n1", "job-a", &need, Duration::from_secs(60)));
    view.commit_reservation("n1", "job-a");
    let u = view.snapshot_unit("n1").expect("unit");
    assert!((u.used.get("cpu").copied().unwrap_or(0.0) - 3.0).abs() < 1e-6);

    assert!(view.try_reserve("n1", "job-b", &need, Duration::from_secs(60)));
    view.release_reservation("n1", "job-b");
    let u2 = view.snapshot_unit("n1").expect("unit");
    assert!((u2.used.get("cpu").copied().unwrap_or(0.0) - 3.0).abs() < 1e-6);
}

#[test]
fn scheduling_engine_select_node_least_loaded_free_score() {
    let view = Arc::new(ResourceView::new());
    let nodes = Arc::new(LocalNodeManager::new(view.clone()));

    let heavy_json = r#"{"capacity":{"cpu":10.0},"used":{"cpu":8.0}}"#;
    let light_json = r#"{"capacity":{"cpu":10.0},"used":{"cpu":1.0}}"#;
    nodes.upsert_local("heavy".into(), "h:1".into(), heavy_json, None);
    nodes.upsert_local("light".into(), "l:1".into(), light_json, None);

    let engine = SchedulingEngine::new(sample_domain_config(), view, nodes);
    let mut req = ScheduleRequest::default();
    req.request_id = "sched-1".into();
    req.required_resources = HashMap::from([("cpu".into(), 0.5)]);

    let picked = engine.select_node(&req, false).expect("some node");
    assert_eq!(picked.node_id, "light");
}

#[test]
fn scheduling_engine_no_fit_returns_none() {
    let view = Arc::new(ResourceView::new());
    let nodes = Arc::new(LocalNodeManager::new(view.clone()));
    nodes.upsert_local(
        "tiny".into(),
        "t:1".into(),
        r#"{"capacity":{"cpu":1.0},"used":{"cpu":0.99}}"#,
        None,
    );

    let engine = SchedulingEngine::new(sample_domain_config(), view, nodes);
    let mut req = ScheduleRequest::default();
    req.request_id = "sched-2".into();
    req.required_resources = HashMap::from([("cpu".into(), 10.0)]);

    assert!(engine.select_node(&req, false).is_none());
}

#[test]
fn label_on_request_filters_nodes() {
    let view = Arc::new(ResourceView::new());
    let nodes = Arc::new(LocalNodeManager::new(view.clone()));
    let ok_json = r#"{"capacity":{"cpu":4.0},"used":{"cpu":0.0},"labels":{"rack":"r1"}}"#;
    let wrong_json = r#"{"capacity":{"cpu":4.0},"used":{"cpu":0.0},"labels":{"rack":"r2"}}"#;
    nodes.upsert_local("a".into(), "a:1".into(), ok_json, None);
    nodes.upsert_local("b".into(), "b:1".into(), wrong_json, None);

    let engine = SchedulingEngine::new(sample_domain_config(), view, nodes);
    let mut req = ScheduleRequest::default();
    req.request_id = "l1".into();
    req.required_resources = HashMap::from([("cpu".into(), 0.5)]);
    req.labels.insert("rack".into(), "r1".into());

    let picked = engine.select_node(&req, false).expect("node");
    assert_eq!(picked.node_id, "a");
}

fn encode_resource_unit_with_label(label_key: &str, label_value: &str) -> String {
    let unit = ProtoResourceUnit {
        id: format!("node-{label_value}"),
        capacity: Some(Resources {
            resources: HashMap::from([(
                "cpu".into(),
                Resource {
                    name: "cpu".into(),
                    scalar: Some(Scalar {
                        value: 4.0,
                        limit: 0.0,
                    }),
                    ..Default::default()
                },
            )]),
        }),
        allocatable: Some(Resources {
            resources: HashMap::from([(
                "cpu".into(),
                Resource {
                    name: "cpu".into(),
                    scalar: Some(Scalar {
                        value: 4.0,
                        limit: 0.0,
                    }),
                    ..Default::default()
                },
            )]),
        }),
        actual_use: Some(Resources {
            resources: HashMap::from([(
                "cpu".into(),
                Resource {
                    name: "cpu".into(),
                    scalar: Some(Scalar {
                        value: 0.0,
                        limit: 0.0,
                    }),
                    ..Default::default()
                },
            )]),
        }),
        node_labels: HashMap::from([(
            label_key.into(),
            Counter {
                items: HashMap::from([(label_value.into(), 1_u64)]),
            },
        )]),
        ..Default::default()
    };
    base64::engine::general_purpose::STANDARD.encode(unit.encode_to_vec())
}

#[test]
fn label_on_request_uses_authoritative_resource_unit_labels_when_json_is_lossy() {
    let view = Arc::new(ResourceView::new());
    let nodes = Arc::new(LocalNodeManager::new(view.clone()));
    let resource_json = r#"{"labels":{"rack":"wrong"},"node":{"labels":{"rack":"r1"}}}"#;
    let ok_unit = encode_resource_unit_with_label("rack", "r1");
    let wrong_unit = encode_resource_unit_with_label("rack", "r2");
    nodes.upsert_local("a".into(), "a:1".into(), resource_json, Some(&ok_unit));
    nodes.upsert_local("b".into(), "b:1".into(), resource_json, Some(&wrong_unit));

    let engine = SchedulingEngine::new(sample_domain_config(), view, nodes);
    let mut req = ScheduleRequest::default();
    req.request_id = "l2".into();
    req.required_resources = HashMap::from([("cpu".into(), 0.5)]);
    req.labels.insert("rack".into(), "r1".into());

    let picked = engine.select_node(&req, false).expect("node");
    assert_eq!(picked.node_id, "a");
}

#[test]
fn scheduler_framework_resource_selector_match_labels() {
    let view = ResourceView::new();
    view.upsert_node_resources(
        "n1",
        HashMap::from([("cpu".into(), 4.0)]),
        HashMap::from([("cpu".into(), 0.0)]),
    );
    view.upsert_node_resources(
        "n2",
        HashMap::from([("cpu".into(), 4.0)]),
        HashMap::from([("cpu".into(), 0.0)]),
    );
    let reg = default_plugin_register();
    let fw = SchedulerFramework::from_register(&reg);
    let mut req = ScheduleRequest::default();
    req.required_resources = HashMap::from([("cpu".into(), 1.0)]);
    req.extension.insert(
        "resource_selector".into(),
        r#"{"matchLabels":{"zone":"z1"}}"#.into(),
    );
    let meta = yr_domain_scheduler::function_meta::parse_function_schedule_meta(&req);
    let ctx = ScheduleContext {
        resource_view: &view,
        exclude_node_id: None,
        function_meta: Some(&meta),
    };
    let nodes = vec![
        NodeInfo {
            node_id: "n1".into(),
            address: "x".into(),
            labels: HashMap::from([("zone".into(), "z1".into())]),
            failure_domain: None,
        },
        NodeInfo {
            node_id: "n2".into(),
            address: "y".into(),
            labels: HashMap::from([("zone".into(), "z2".into())]),
            failure_domain: None,
        },
    ];
    let best = fw
        .select_best(&ctx, &req, &nodes)
        .expect("selector should pick z1");
    assert_eq!(best.node_id, "n1");
}

#[test]
fn scheduler_framework_resource_selector_match_expression_not_exist() {
    let view = ResourceView::new();
    view.upsert_node_resources(
        "n1",
        HashMap::from([("cpu".into(), 4.0)]),
        HashMap::from([("cpu".into(), 0.0)]),
    );
    view.upsert_node_resources(
        "n2",
        HashMap::from([("cpu".into(), 4.0)]),
        HashMap::from([("cpu".into(), 0.0)]),
    );
    let reg = default_plugin_register();
    let fw = SchedulerFramework::from_register(&reg);
    let mut req = ScheduleRequest::default();
    req.required_resources = HashMap::from([("cpu".into(), 1.0)]);
    req.extension.insert(
        "resource_selector".into(),
        r#"{"matchExpressions":[{"key":"dedicated","operator":"NotExist"}]}"#.into(),
    );
    let meta = yr_domain_scheduler::function_meta::parse_function_schedule_meta(&req);
    let ctx = ScheduleContext {
        resource_view: &view,
        exclude_node_id: None,
        function_meta: Some(&meta),
    };
    let nodes = vec![
        NodeInfo {
            node_id: "n1".into(),
            address: "x".into(),
            labels: HashMap::from([("zone".into(), "z1".into())]),
            failure_domain: None,
        },
        NodeInfo {
            node_id: "n2".into(),
            address: "y".into(),
            labels: HashMap::from([
                ("zone".into(), "z1".into()),
                ("dedicated".into(), "gpu".into()),
            ]),
            failure_domain: None,
        },
    ];
    let best = fw
        .select_best(&ctx, &req, &nodes)
        .expect("selector should reject nodes with dedicated label");
    assert_eq!(best.node_id, "n1");
}

#[test]
fn scheduling_engine_failure_domains_match_zone_fallback_from_resource_json() {
    let view = Arc::new(ResourceView::new());
    let nodes = Arc::new(LocalNodeManager::new(view.clone()));
    nodes.upsert_local(
        "az1-node".into(),
        "a:1".into(),
        r#"{"capacity":{"cpu":4.0},"used":{"cpu":0.0},"labels":{"rack":"r1","zone":"az1"}}"#,
        None,
    );
    nodes.upsert_local(
        "az2-node".into(),
        "b:1".into(),
        r#"{"capacity":{"cpu":4.0},"used":{"cpu":3.0},"labels":{"rack":"r1","zone":"az2"}}"#,
        None,
    );

    let engine = SchedulingEngine::new(sample_domain_config(), view, nodes);
    let mut baseline = ScheduleRequest::default();
    baseline.request_id = "fd-0".into();
    baseline.required_resources = HashMap::from([("cpu".into(), 1.0)]);
    let baseline_picked = engine
        .select_node(&baseline, false)
        .expect("without failure-domain filter the freer node should win");
    assert_eq!(baseline_picked.node_id, "az1-node");

    let mut req = ScheduleRequest::default();
    req.request_id = "fd-1".into();
    req.required_resources = HashMap::from([("cpu".into(), 1.0)]);
    req.extension.insert(
        "scheduling".into(),
        r#"{"failure_domains":["az2"]}"#.into(),
    );

    let picked = engine
        .select_node(&req, false)
        .expect("matching failure domain should be selected");
    assert_eq!(picked.node_id, "az2-node");
}

#[test]
fn schedule_recorder_retains_entries() {
    let view = Arc::new(ResourceView::new());
    let nodes = Arc::new(LocalNodeManager::new(view.clone()));
    nodes.upsert_local(
        "only".into(),
        "o:1".into(),
        r#"{"capacity":{"cpu":2.0},"used":{"cpu":0.0}}"#,
        None,
    );
    let engine = SchedulingEngine::new(sample_domain_config(), view, nodes);
    let mut req = ScheduleRequest::default();
    req.request_id = "r1".into();
    req.function_name = "fn1".into();
    req.required_resources = HashMap::from([("cpu".into(), 100.0)]);
    assert!(engine.select_node(&req, false).is_none());
    let snap = engine.recorder.snapshot_json();
    assert!(snap.is_array());
    let arr = snap.as_array().unwrap();
    assert!(!arr.is_empty());
}
