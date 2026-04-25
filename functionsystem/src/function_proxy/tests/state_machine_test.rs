//! Instance lifecycle and local resource view (in-memory).

use std::collections::HashMap;

use yr_common::types::transition_allowed;
use yr_proxy::resource_view::{ResourceVector, ResourceView};
use yr_proxy::state_machine::{InstanceMetadata, InstanceState};

#[test]
fn valid_linear_transitions_new_to_running() {
    let mut meta = InstanceMetadata {
        id: "i1".into(),
        function_name: "f".into(),
        tenant: "t".into(),
        node_id: "n".into(),
        runtime_id: "r".into(),
        runtime_port: 0,
        state: InstanceState::New,
        created_at_ms: 0,
        updated_at_ms: 0,
        group_id: None,
        trace_id: String::new(),
        resources: HashMap::new(),
        etcd_kv_version: None,
        etcd_mod_revision: None,
    };

    assert!(meta.transition(InstanceState::Scheduling).is_ok());
    assert!(meta.transition(InstanceState::Creating).is_ok());
    assert!(meta.transition(InstanceState::Running).is_ok());
    assert_eq!(meta.state, InstanceState::Running);
}

#[test]
fn invalid_transition_rejected() {
    let mut meta = InstanceMetadata {
        id: "i2".into(),
        function_name: "f".into(),
        tenant: "t".into(),
        node_id: "n".into(),
        runtime_id: "r".into(),
        runtime_port: 0,
        state: InstanceState::New,
        created_at_ms: 0,
        updated_at_ms: 0,
        group_id: None,
        trace_id: String::new(),
        resources: HashMap::new(),
        etcd_kv_version: None,
        etcd_mod_revision: None,
    };

    assert!(meta.transition(InstanceState::Running).is_err());
    assert_eq!(meta.state, InstanceState::New);
}

#[test]
fn transition_table_from_yr_common() {
    assert!(transition_allowed(
        InstanceState::New,
        InstanceState::Scheduling
    ));
    assert!(transition_allowed(
        InstanceState::Scheduling,
        InstanceState::Creating
    ));
    assert!(transition_allowed(
        InstanceState::Creating,
        InstanceState::Running
    ));
    assert!(!transition_allowed(
        InstanceState::New,
        InstanceState::Creating
    ));
}

#[tokio::test]
async fn resource_view_reserve_pending_pre_deducts_capacity() {
    let cap = ResourceVector {
        cpu: 2.0,
        memory: 1024.0,
        npu: 0.0,
    };
    let view = ResourceView::new(cap);
    let req = HashMap::from([("cpu".into(), 1.5)]);

    assert!(view.reserve_pending(&req));
    let snap: serde_json::Value = serde_json::from_str(&view.snapshot_json()).unwrap();
    let pending = snap["pending"].as_object().unwrap();
    assert!((pending["cpu"].as_f64().unwrap() - 1.5).abs() < 1e-9);

    view.release_pending(&req);
    let snap2: serde_json::Value = serde_json::from_str(&view.snapshot_json()).unwrap();
    let pending2 = snap2["pending"].as_object().unwrap();
    assert!((pending2["cpu"].as_f64().unwrap()).abs() < 1e-9);
}
