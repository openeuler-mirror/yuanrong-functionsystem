//! `MasterConfig::validate` and CLI-to-config wiring.

mod common;

use std::sync::Arc;

use clap::Parser;
use yr_common::error::YrError;
use yr_master::config::{CliArgs, ElectionMode, MasterConfig};

use common::test_master_config;

fn base_config() -> MasterConfig {
    let c = test_master_config("c1");
    (*c).clone()
}

#[test]
fn validate_ok_standalone_without_etcd() {
    let mut c = base_config();
    c.election_mode = ElectionMode::Standalone;
    c.enable_meta_store = false;
    c.etcd_endpoints.clear();
    assert!(c.validate().is_ok());
}

#[test]
fn validate_rejects_meta_store_without_etcd_endpoints() {
    let mut c = base_config();
    c.enable_meta_store = true;
    c.etcd_endpoints.clear();
    let err = c.validate().unwrap_err();
    assert!(matches!(err, YrError::Config(_)));
}

#[test]
fn validate_ok_meta_store_with_etcd() {
    let mut c = base_config();
    c.enable_meta_store = true;
    c.etcd_endpoints = vec!["127.0.0.1:2379".into()];
    assert!(c.validate().is_ok());
}

#[test]
fn validate_rejects_etcd_election_without_endpoints() {
    let mut c = base_config();
    c.election_mode = ElectionMode::Etcd;
    c.etcd_endpoints.clear();
    assert!(c.validate().is_err());
}

#[test]
fn validate_rejects_txn_election_without_endpoints() {
    let mut c = base_config();
    c.election_mode = ElectionMode::Txn;
    c.etcd_endpoints.clear();
    assert!(c.validate().is_err());
}

#[test]
fn validate_rejects_k8s_election_without_endpoints() {
    let mut c = base_config();
    c.election_mode = ElectionMode::K8s;
    c.etcd_endpoints.clear();
    assert!(c.validate().is_err());
}

#[test]
fn validate_ok_etcd_election_with_endpoints() {
    let mut c = base_config();
    c.election_mode = ElectionMode::Etcd;
    c.etcd_endpoints = vec!["127.0.0.1:2379".into()];
    assert!(c.validate().is_ok());
}

#[test]
fn validate_ok_txn_election_with_endpoints() {
    let mut c = base_config();
    c.election_mode = ElectionMode::Txn;
    c.etcd_endpoints = vec!["127.0.0.1:2379".into()];
    assert!(c.validate().is_ok());
}

#[test]
fn validate_ok_k8s_election_with_endpoints() {
    let mut c = base_config();
    c.election_mode = ElectionMode::K8s;
    c.etcd_endpoints = vec!["127.0.0.1:2379".into()];
    assert!(c.validate().is_ok());
}

#[test]
fn from_cli_maps_enable_persistence() {
    let cli = CliArgs::try_parse_from(["yr-master", "--enable-persistence"]).unwrap();
    let cfg = MasterConfig::from_cli(cli).unwrap();
    assert!(cfg.enable_persistence);
}

#[test]
fn topology_and_instance_keys_use_etcd_prefix() {
    let mut c = base_config();
    c.etcd_table_prefix = "/yr".into();
    let c = Arc::new(c);
    assert!(c.topology_key().starts_with("/yr"));
    assert!(c.instance_prefix().starts_with("/yr"));
}

#[test]
fn txn_election_key_contains_logical_suffix() {
    let mut c = base_config();
    c.etcd_table_prefix = "/p".into();
    let c = Arc::new(c);
    assert!(c.txn_election_key().contains("yr/election/yr-master"));
}
