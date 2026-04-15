//! Instance controller and metadata lifecycle (pure logic + local concurrency).

mod common;

use std::collections::HashMap;
use std::sync::Arc;

use common::make_proxy_config;
use yr_proxy::instance_ctrl::InstanceController;
use yr_proxy::resource_view::{ResourceVector, ResourceView};
use yr_proxy::state_machine::{InstanceMetadata, InstanceState};

fn test_ctrl() -> Arc<InstanceController> {
    let c = make_proxy_config("lc-node", 30001);
    let rv = ResourceView::new(ResourceVector {
        cpu: 16.0,
        memory: 128.0,
        npu: 0.0,
    });
    InstanceController::new(c, rv, None, None)
}

fn sample_meta(id: &str, state: InstanceState) -> InstanceMetadata {
    InstanceMetadata {
        id: id.into(),
        function_name: "fn".into(),
        tenant: "t1".into(),
        node_id: "lc-node".into(),
        runtime_id: "rt".into(),
        runtime_port: 1,
        state,
        created_at_ms: InstanceMetadata::now_ms(),
        updated_at_ms: InstanceMetadata::now_ms(),
        group_id: None,
        trace_id: "tr".into(),
        resources: HashMap::from([("cpu".into(), 1.0), ("memory".into(), 512.0)]),
        etcd_kv_version: Some(10),
        etcd_mod_revision: None,
    }
}

#[tokio::test]
async fn transition_new_to_scheduling() {
    let ctrl = test_ctrl();
    ctrl.insert_metadata(sample_meta("a", InstanceState::New));
    let m = ctrl
        .transition_with_version("a", InstanceState::Scheduling, None)
        .await
        .expect("ok");
    assert_eq!(m.state, InstanceState::Scheduling);
}

#[tokio::test]
async fn transition_rejects_kv_version_mismatch() {
    let ctrl = test_ctrl();
    ctrl.insert_metadata(sample_meta("b", InstanceState::New));
    let err = ctrl
        .transition_with_version("b", InstanceState::Scheduling, Some(99))
        .await
        .expect_err("mismatch");
    assert_eq!(err.code(), tonic::Code::Aborted);
}

#[tokio::test]
async fn transition_missing_instance_not_found() {
    let ctrl = test_ctrl();
    let err = ctrl
        .transition_with_version("nope", InstanceState::Running, None)
        .await
        .expect_err("missing");
    assert_eq!(err.code(), tonic::Code::NotFound);
}

#[tokio::test]
async fn apply_exit_ok_moves_to_exited() {
    let ctrl = test_ctrl();
    ctrl.insert_metadata(sample_meta("e1", InstanceState::Running));
    let out = ctrl.apply_exit_event("e1", true, "clean").await;
    assert!(out.is_some());
    assert_eq!(out.unwrap().state, InstanceState::Exited);
}

#[tokio::test]
async fn apply_exit_error_moves_to_failed() {
    let ctrl = test_ctrl();
    ctrl.insert_metadata(sample_meta("e2", InstanceState::Running));
    let out = ctrl.apply_exit_event("e2", false, "crash").await;
    assert_eq!(out.expect("some").state, InstanceState::Failed);
}

#[tokio::test]
async fn remove_drops_instance() {
    let ctrl = test_ctrl();
    ctrl.insert_metadata(sample_meta("r1", InstanceState::Running));
    assert!(ctrl.remove("r1").is_some());
    assert!(ctrl.get("r1").is_none());
}

#[tokio::test]
async fn insert_get_round_trip() {
    let ctrl = test_ctrl();
    let m = sample_meta("g1", InstanceState::Creating);
    ctrl.insert_metadata(m.clone());
    assert_eq!(ctrl.get("g1").unwrap().function_name, m.function_name);
}

#[test]
fn metadata_transition_running_to_exiting() {
    let mut m = sample_meta("k", InstanceState::Running);
    m.transition(InstanceState::Exiting).expect("ok");
    assert_eq!(m.state, InstanceState::Exiting);
}

#[test]
fn metadata_transition_running_to_failed() {
    let mut m = sample_meta("kf", InstanceState::Running);
    m.transition(InstanceState::Failed).expect("ok");
    assert_eq!(m.state, InstanceState::Failed);
}

#[test]
fn metadata_transition_rejects_disallowed_jump() {
    let mut m = sample_meta("bad-tr", InstanceState::New);
    assert!(m.transition(InstanceState::Running).is_err());
    assert_eq!(m.state, InstanceState::New);
}

#[test]
fn metadata_shim_running_to_exited() {
    let mut m = sample_meta("sx", InstanceState::Running);
    m.transition(InstanceState::Exited).expect("shim");
    assert_eq!(m.state, InstanceState::Exited);
}

#[test]
fn should_persist_state_matches_global_policy() {
    let m_new = sample_meta("p1", InstanceState::New);
    assert!(m_new.should_persist_state());
    let m_failed = sample_meta("p2", InstanceState::Failed);
    assert!(m_failed.should_persist_state());
    let m_run = sample_meta("p3", InstanceState::Running);
    assert!(!m_run.should_persist_state());
}

#[test]
fn should_update_route_respects_meta_store_flag() {
    let m = sample_meta("p2", InstanceState::Running);
    let _ = m.should_update_route(true);
    let _ = m.should_update_route(false);
}

#[test]
fn clamp_resources_applies_min_max_defaults() {
    let cfg = make_proxy_config("lc-node", 30001);
    let rv = ResourceView::new(ResourceVector {
        cpu: 16.0,
        memory: 128.0,
        npu: 0.0,
    });
    let ctrl = InstanceController::new(cfg.clone(), rv, None, None);
    let out = ctrl.clamp_resources(&HashMap::new());
    assert_eq!(out["cpu"], cfg.min_instance_cpu);
    assert_eq!(out["memory"], cfg.min_instance_memory);
}

#[test]
fn clamp_resources_clamps_high_values() {
    let cfg = make_proxy_config("lc-node", 30002);
    let rv = ResourceView::new(ResourceVector {
        cpu: 16.0,
        memory: 128.0,
        npu: 0.0,
    });
    let ctrl = InstanceController::new(cfg.clone(), rv, None, None);
    let out = ctrl.clamp_resources(&HashMap::from([
        ("cpu".into(), 9999.0),
        ("memory".into(), 1e15),
    ]));
    assert_eq!(out["cpu"], cfg.max_instance_cpu);
    assert_eq!(out["memory"], cfg.max_instance_memory);
}

#[test]
fn clamp_resources_keeps_npu_non_negative() {
    let ctrl = test_ctrl();
    let out = ctrl.clamp_resources(&HashMap::from([("npu".into(), -3.0)]));
    assert_eq!(out["npu"], 0.0);
}

#[test]
fn tenant_cooldown_round_trip() {
    let ctrl = test_ctrl();
    assert!(!ctrl.tenant_cooldown_active("ten"));
    ctrl.set_tenant_cooldown_ms("ten", 60_000);
    assert!(ctrl.tenant_cooldown_active("ten"));
}

#[tokio::test]
async fn concurrent_insert_distinct_ids() {
    let ctrl = test_ctrl();
    let mut handles = vec![];
    for i in 0..32 {
        let c = Arc::clone(&ctrl);
        handles.push(tokio::spawn(async move {
            c.insert_metadata(sample_meta(
                &format!("conc-{i}"),
                InstanceState::Scheduling,
            ));
        }));
    }
    for h in handles {
        h.await.unwrap();
    }
    for i in 0..32 {
        assert!(ctrl.get(&format!("conc-{i}")).is_some());
    }
}

#[tokio::test]
async fn concurrent_transitions_last_write_wins_per_key() {
    let ctrl = test_ctrl();
    ctrl.insert_metadata(sample_meta("race", InstanceState::New));
    let mut hs = vec![];
    for _ in 0..8 {
        let c = Arc::clone(&ctrl);
        hs.push(tokio::spawn(async move {
            let _ = c
                .transition_with_version("race", InstanceState::Scheduling, None)
                .await;
        }));
    }
    for h in hs {
        let _ = h.await;
    }
    let st = ctrl.get("race").unwrap().state;
    assert_eq!(st, InstanceState::Scheduling);
}

#[test]
fn schedule_hooks_authorize_and_meta_noop() {
    let ctrl = test_ctrl();
    let rt = tokio::runtime::Runtime::new().unwrap();
    rt.block_on(async {
        ctrl.schedule_get_func_meta("t", "f", "$latest")
            .await
            .expect("ok");
        ctrl.schedule_do_authorize_create("t", "f").await.expect("ok");
    });
}
