//! `MasterState` scheduling queue and `do_schedule` constraints.

mod common;

use std::sync::atomic::Ordering;

use yr_master::scheduler::MasterState;
use yr_proto::internal::{GroupScheduleRequest, ScheduleRequest};

use common::test_master_state;

#[tokio::test]
async fn do_group_schedule_not_leader_returns_error() {
    let state = test_master_state();
    state.is_leader.store(false, Ordering::SeqCst);
    let r = state
        .do_group_schedule(GroupScheduleRequest {
            group_id: "g1".into(),
            ..Default::default()
        })
        .await;
    assert!(!r.success);
    assert_eq!(r.error_code, 9);
}

#[tokio::test]
async fn do_schedule_not_leader_does_not_enqueue() {
    let state = test_master_state();
    state.is_leader.store(false, Ordering::SeqCst);
    let r = state
        .do_schedule(ScheduleRequest {
            request_id: "n1".into(),
            ..Default::default()
        })
        .await;
    assert!(!r.success);
    assert_eq!(r.error_code, 9);
    let q = state.scheduling_queue.lock();
    assert!(q.is_empty());
}

#[tokio::test]
async fn do_schedule_enqueues_request_id_when_leader() {
    let state = test_master_state();
    let r = state
        .do_schedule(ScheduleRequest {
            request_id: "e1".into(),
            ..Default::default()
        })
        .await;
    assert!(!r.success);
    let q = state.scheduling_queue.lock();
    assert_eq!(q.len(), 1);
    assert_eq!(q[0], "e1");
}

#[tokio::test]
async fn scheduling_queue_fifo_order() {
    let state = test_master_state();
    for i in 0..3 {
        let _ = state
            .do_schedule(ScheduleRequest {
                request_id: format!("rid-{i}"),
                ..Default::default()
            })
            .await;
    }
    let q = state.scheduling_queue.lock();
    let v: Vec<_> = q.iter().cloned().collect();
    assert_eq!(v, vec!["rid-0", "rid-1", "rid-2"]);
}

#[tokio::test]
async fn scheduling_queue_capacity_truncates_oldest() {
    let state = test_master_state();
    for i in 0..260 {
        let _ = state
            .do_schedule(ScheduleRequest {
                request_id: format!("x{i}"),
                ..Default::default()
            })
            .await;
    }
    let q = state.scheduling_queue.lock();
    assert_eq!(q.len(), 256);
    assert_eq!(q.front().map(String::as_str), Some("x4"));
    assert_eq!(q.back().map(String::as_str), Some("x259"));
}

#[tokio::test]
async fn do_schedule_root_missing_returns_error_code_5() {
    let state = test_master_state();
    let r = state
        .do_schedule(ScheduleRequest {
            request_id: "m1".into(),
            ..Default::default()
        })
        .await;
    assert!(!r.success);
    assert_eq!(r.error_code, 5);
    assert!(r.message.contains("root domain"));
}

#[tokio::test]
async fn do_schedule_with_registered_agent_still_fails_forward_but_enqueues() {
    let state = test_master_state();
    state
        .topology
        .register_local("ag".into(), "10.0.0.1:1".into(), "{}".into(), "{}".into())
        .await;
    let r = state
        .do_schedule(ScheduleRequest {
            request_id: "fwd".into(),
            ..Default::default()
        })
        .await;
    assert!(!r.success);
    let q = state.scheduling_queue.lock();
    assert!(q.contains(&"fwd".to_string()));
}

#[tokio::test]
async fn is_leader_alias_matches_require_leader() {
    let state = test_master_state();
    assert_eq!(state.is_leader(), state.require_leader());
    state.is_leader.store(false, Ordering::SeqCst);
    assert_eq!(state.is_leader(), false);
}

#[tokio::test]
async fn require_leader_reflects_atomic() {
    let state = test_master_state();
    assert!(state.require_leader());
    state.is_leader.store(false, Ordering::SeqCst);
    assert!(!state.require_leader());
}

fn assert_send_sync<T: Send + Sync>() {}

#[test]
fn master_state_is_send_sync() {
    assert_send_sync::<MasterState>();
}
