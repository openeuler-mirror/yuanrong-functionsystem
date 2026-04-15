//! Contract tests for domain scheduler config and key naming (in-memory, no etcd/gRPC).

use yr_common::etcd_keys::YR_DOMAIN_SCHEDULER_PREFIX;
use yr_domain_scheduler::config::{DomainSchedulerConfig, ElectionMode};

fn sample_config() -> DomainSchedulerConfig {
    DomainSchedulerConfig {
        host: "127.0.0.1".into(),
        port: 8401,
        http_port: 8481,
        global_scheduler_address: String::new(),
        etcd_endpoints: vec!["http://127.0.0.1:2379".into()],
        etcd_table_prefix: String::new(),
        node_id: "domain-0".into(),
        election_mode: ElectionMode::Standalone,
        enable_preemption: false,
        max_priority: 100,
        pull_resource_interval_ms: 5000,
        instance_id: "test-instance".into(),
    }
}

#[test]
fn validate_etcd_required_for_etcd_election_mode() {
    let mut cfg = sample_config();
    cfg.election_mode = ElectionMode::Etcd;
    cfg.etcd_endpoints.clear();

    let err = cfg.validate().unwrap_err();
    assert!(
        err.to_string().contains("etcd_endpoints"),
        "unexpected error: {err}"
    );
}

#[test]
fn validate_etcd_required_for_k8s_election_mode() {
    let mut cfg = sample_config();
    cfg.election_mode = ElectionMode::K8s;
    cfg.etcd_endpoints.clear();

    let err = cfg.validate().unwrap_err();
    assert!(
        err.to_string().contains("etcd_endpoints"),
        "unexpected error: {err}"
    );
}

#[test]
fn validate_etcd_not_required_for_standalone_even_when_empty() {
    let mut cfg = sample_config();
    cfg.election_mode = ElectionMode::Standalone;
    cfg.etcd_endpoints.clear();
    cfg.validate().expect("standalone allows empty etcd_endpoints");
}

#[test]
fn validate_etcd_election_succeeds_when_endpoints_present() {
    let mut cfg = sample_config();
    cfg.election_mode = ElectionMode::Etcd;
    cfg.etcd_endpoints = vec!["http://etcd:2379".into()];
    cfg.validate().expect("etcd mode with endpoints should validate");
}

#[test]
fn validate_max_priority_must_be_at_least_one() {
    let mut cfg = sample_config();
    cfg.max_priority = 0;
    let err = cfg.validate().unwrap_err();
    assert!(
        err.to_string().contains("max_priority"),
        "unexpected error: {err}"
    );

    let mut cfg = sample_config();
    cfg.max_priority = -1;
    let err = cfg.validate().unwrap_err();
    assert!(
        err.to_string().contains("max_priority"),
        "unexpected error: {err}"
    );
}

#[test]
fn validate_max_priority_one_is_accepted() {
    let mut cfg = sample_config();
    cfg.max_priority = 1;
    cfg.validate().expect("max_priority == 1 should validate");
}

#[test]
fn election_name_is_deterministic_for_same_config() {
    let cfg = sample_config();
    let a = cfg.election_name();
    let b = cfg.election_name();
    assert_eq!(a, b);
}

#[test]
fn election_name_matches_domain_base_and_node_id() {
    let mut cfg = sample_config();
    cfg.etcd_table_prefix.clear();
    cfg.node_id = "ds-1".into();
    let base = cfg.domain_key_base();
    let expected = format!("{base}/leader_election/ds-1");
    assert_eq!(cfg.election_name(), expected.as_bytes());
}

#[test]
fn grpc_listen_addr_formats_host_and_port() {
    let mut cfg = sample_config();
    cfg.host = "0.0.0.0".into();
    cfg.port = 9401;
    assert_eq!(cfg.grpc_listen_addr(), "0.0.0.0:9401");
    assert_eq!(cfg.advertise_grpc_addr(), cfg.grpc_listen_addr());
}

#[test]
fn domain_key_base_empty_table_prefix_is_logical_domain_prefix_only() {
    let mut cfg = sample_config();
    cfg.etcd_table_prefix.clear();
    assert_eq!(cfg.domain_key_base(), YR_DOMAIN_SCHEDULER_PREFIX);
}

#[test]
fn domain_key_base_includes_table_prefix_without_double_slash() {
    let mut cfg = sample_config();
    cfg.etcd_table_prefix = "/tenant_a".into();
    assert_eq!(
        cfg.domain_key_base(),
        format!("/tenant_a{YR_DOMAIN_SCHEDULER_PREFIX}")
    );
}

#[test]
fn domain_key_base_trims_trailing_slash_on_table_prefix() {
    let mut cfg = sample_config();
    cfg.etcd_table_prefix = "/tenant_a///".into();
    assert_eq!(
        cfg.domain_key_base(),
        format!("/tenant_a{YR_DOMAIN_SCHEDULER_PREFIX}")
    );
}
