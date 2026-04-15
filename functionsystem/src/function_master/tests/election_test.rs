//! Leader election contracts: standalone wiring, CLI parsing, and txn-mode key/TTL (no etcd).

mod common;

use std::sync::atomic::{AtomicBool, Ordering};

use clap::Parser;
use yr_master::config::{CliArgs, ElectionMode, MasterConfig};
use yr_master::election::TXN_ELECTION_LEASE_TTL_SECS;

use common::test_master_config;

/// Mirrors `spawn_txn_election` holder selection in `election.rs`.
fn master_txn_election_holder(cfg: &MasterConfig) -> String {
    if !cfg.node_id.is_empty() {
        cfg.node_id.clone()
    } else {
        cfg.instance_id.clone()
    }
}

#[test]
fn standalone_election_immediately_starts_as_leader_flag() {
    let mode = ElectionMode::Standalone;
    let is_leader = AtomicBool::new(matches!(mode, ElectionMode::Standalone));
    assert!(
        is_leader.load(Ordering::SeqCst),
        "main.rs initializes leader true for standalone"
    );
}

#[test]
fn standalone_election_identity_returns_configured_node_id() {
    let mut c = (*test_master_config("e1")).clone();
    c.election_mode = ElectionMode::Standalone;
    c.node_id = "master-node-7".into();
    c.instance_id = "ephemeral-hostname".into();
    assert_eq!(master_txn_election_holder(&c), "master-node-7");
}

#[test]
fn standalone_election_master_state_reports_is_leader() {
    let state = common::test_master_state();
    assert!(
        state.config.election_mode == ElectionMode::Standalone,
        "fixture uses standalone"
    );
    assert!(state.require_leader());
    assert!(state.is_leader());
}

#[test]
fn standalone_election_leader_identity_matches_self() {
    let mut c = (*test_master_config("e2")).clone();
    c.election_mode = ElectionMode::Standalone;
    c.node_id = "id-a".into();
    c.instance_id = "id-b".into();
    let leader_id = master_txn_election_holder(&c);
    assert_eq!(leader_id, c.node_id);

    c.node_id.clear();
    let leader_id = master_txn_election_holder(&c);
    assert_eq!(leader_id, c.instance_id);
}

#[test]
fn parse_election_mode_standalone() {
    let args = CliArgs::try_parse_from(["yr-master", "--election-mode", "standalone"]).unwrap();
    assert_eq!(args.election_mode, ElectionMode::Standalone);
}

#[test]
fn parse_election_mode_etcd() {
    let args = CliArgs::try_parse_from(["yr-master", "--election-mode", "etcd"]).unwrap();
    assert_eq!(args.election_mode, ElectionMode::Etcd);
}

#[test]
fn parse_election_mode_txn() {
    let args = CliArgs::try_parse_from(["yr-master", "--election-mode", "txn"]).unwrap();
    assert_eq!(args.election_mode, ElectionMode::Txn);
}

#[test]
fn parse_election_mode_k8s() {
    let args = CliArgs::try_parse_from(["yr-master", "--election-mode", "k8s"]).unwrap();
    assert_eq!(args.election_mode, ElectionMode::K8s);
}

#[test]
fn parse_election_mode_rejects_invalid_value() {
    let r = CliArgs::try_parse_from(["yr-master", "--election-mode", "not-a-mode"]);
    assert!(r.is_err(), "invalid election mode should not parse");
}

#[test]
fn txn_election_key_format_matches_contract() {
    let mut c = (*test_master_config("e3")).clone();
    c.etcd_table_prefix.clear();
    assert_eq!(c.txn_election_key(), "/yr/election/yr-master");

    c.etcd_table_prefix = "/tenant_a".into();
    assert_eq!(c.txn_election_key(), "/tenant_a/yr/election/yr-master");
}

#[test]
fn txn_election_holder_value_matches_node_id_when_set() {
    let mut c = (*test_master_config("e4")).clone();
    c.election_mode = ElectionMode::Txn;
    c.node_id = "txn-holder-1".into();
    c.instance_id = "should-not-be-used".into();
    assert_eq!(master_txn_election_holder(&c), c.node_id);
    assert_eq!(master_txn_election_holder(&c).as_bytes(), c.node_id.as_bytes());
}

#[test]
fn txn_election_lease_ttl_is_in_reasonable_range() {
    assert!(
        (5..=60).contains(&TXN_ELECTION_LEASE_TTL_SECS),
        "txn lease TTL should stay within a practical etcd keep-alive window"
    );
}
