//! Scenario 9 — master election: CLI modes, leader gating, leader→follower transitions.

mod common;

use std::sync::atomic::Ordering;

use clap::Parser;
use yr_master::config::{CliArgs, ElectionMode};
use yr_proto::internal::ScheduleRequest;

use common::test_master_state;

#[test]
fn e2e_election_parse_modes_standalone_etcd_txn_k8s() {
    for (flag, expected) in [
        ("standalone", ElectionMode::Standalone),
        ("etcd", ElectionMode::Etcd),
        ("txn", ElectionMode::Txn),
        ("k8s", ElectionMode::K8s),
    ] {
        let args = CliArgs::try_parse_from(["yr-master", "--election-mode", flag]).unwrap();
        assert_eq!(args.election_mode, expected, "mode {flag}");
    }
}

#[test]
fn e2e_election_parse_rejects_unknown_mode() {
    assert!(CliArgs::try_parse_from(["yr-master", "--election-mode", "nope"]).is_err());
}

#[tokio::test]
async fn e2e_election_leader_allows_scheduling_queue_mutations() {
    let state = test_master_state();
    assert!(state.require_leader());
    state
        .topology
        .register_local(
            "ag".into(),
            "10.0.0.1:1".into(),
            "{}".into(),
            None,
            "{}".into(),
        )
        .await;
    let r = state
        .clone()
        .do_schedule(ScheduleRequest {
            request_id: "lead-ok".into(),
            ..Default::default()
        })
        .await;
    assert!(
        !r.success,
        "schedule worker returns pending/failure without full runtime"
    );
    let q = state.scheduling_queue.lock();
    assert!(q.contains(&"lead-ok".to_string()));
}

#[tokio::test]
async fn e2e_election_follower_rejects_schedule_without_enqueue() {
    let state = test_master_state();
    state
        .topology
        .register_local(
            "ag2".into(),
            "10.0.0.2:1".into(),
            "{}".into(),
            None,
            "{}".into(),
        )
        .await;
    state.is_leader.store(false, Ordering::SeqCst);
    let r = state
        .clone()
        .do_schedule(ScheduleRequest {
            request_id: "slave-block".into(),
            ..Default::default()
        })
        .await;
    assert!(!r.success);
    assert_eq!(r.error_code, 9);
    let q = state.scheduling_queue.lock();
    assert!(!q.contains(&"slave-block".to_string()));
}

#[tokio::test]
async fn e2e_election_leader_to_follower_stops_instance_forward_placeholder() {
    let state = test_master_state();
    assert!(state.instances.try_forward_or_kill("any"));
    state.is_leader.store(false, Ordering::SeqCst);
    assert!(!state.instances.try_forward_or_kill("any"));
}

#[tokio::test]
async fn e2e_election_regain_leader_restores_forward_placeholder() {
    let state = test_master_state();
    state.is_leader.store(false, Ordering::SeqCst);
    assert!(!state.require_leader());
    state.set_leader(true);
    assert!(state.require_leader());
    assert!(state.instances.try_forward_or_kill("x"));
}
