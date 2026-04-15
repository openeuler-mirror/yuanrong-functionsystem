//! Election behavior contracts: standalone leader flag, txn key shape, CLI parsing (no etcd).

use std::sync::Arc;

use clap::Parser;
use yr_domain_scheduler::config::{CliArgs, DomainSchedulerConfig, ElectionMode};
use yr_domain_scheduler::nodes::LocalNodeManager;
use yr_domain_scheduler::resource_view::ResourceView;
use yr_domain_scheduler::scheduler::SchedulingEngine;
use yr_domain_scheduler::DomainSchedulerState;

fn sample_config() -> DomainSchedulerConfig {
    DomainSchedulerConfig {
        host: "127.0.0.1".into(),
        port: 8401,
        http_port: 8481,
        global_scheduler_address: String::new(),
        etcd_endpoints: vec![],
        etcd_table_prefix: String::new(),
        node_id: "domain-0".into(),
        election_mode: ElectionMode::Standalone,
        enable_preemption: false,
        max_priority: 100,
        pull_resource_interval_ms: 5000,
        instance_id: "ds-instance-1".into(),
    }
}

#[test]
fn standalone_election_domain_scheduler_starts_as_leader() {
    let cfg = Arc::new(sample_config());
    assert_eq!(cfg.election_mode, ElectionMode::Standalone);
    let rv = Arc::new(ResourceView::new());
    let nodes = Arc::new(LocalNodeManager::new(rv.clone()));
    let scheduler = Arc::new(SchedulingEngine::new(cfg.clone(), rv.clone(), nodes.clone()));
    let state = DomainSchedulerState::new(cfg, rv, nodes, scheduler, None);
    assert!(state.require_leader());
    assert!(state.is_leader());
}

#[test]
fn txn_election_key_format_for_domain_scheduler() {
    let mut cfg = sample_config();
    cfg.election_mode = ElectionMode::Txn;
    cfg.etcd_table_prefix.clear();
    cfg.node_id = "slot-a".into();
    let base = cfg.domain_key_base();
    assert_eq!(cfg.txn_election_key(), format!("{base}/election/slot-a"));
}

#[test]
fn election_mode_cli_parsing_for_domain_scheduler() {
    for (flag, expected) in [
        ("standalone", ElectionMode::Standalone),
        ("etcd", ElectionMode::Etcd),
        ("txn", ElectionMode::Txn),
        ("k8s", ElectionMode::K8s),
    ] {
        let args = CliArgs::try_parse_from([
            "yr-domain-scheduler",
            "--election-mode",
            flag,
        ])
        .unwrap_or_else(|e| panic!("parse {flag}: {e}"));
        assert_eq!(args.election_mode, expected, "flag {flag}");
    }

    assert!(
        CliArgs::try_parse_from([
            "yr-domain-scheduler",
            "--election-mode",
            "invalid-mode",
        ])
        .is_err()
    );
}
