//! CLI flag compatibility smoke tests for domain scheduler (`CliArgs`).

use clap::{CommandFactory, Parser};
use yr_domain_scheduler::config::{CliArgs, ElectionMode};

fn long_help() -> String {
    format!("{}", CliArgs::command().render_long_help())
}

#[test]
fn default_values_match_expectations() {
    let args = CliArgs::try_parse_from(["yr-domain-scheduler"]).expect("parse defaults");
    assert_eq!(args.host, "0.0.0.0");
    assert_eq!(args.port, 8401);
    assert_eq!(args.http_port, 8481);
    assert_eq!(args.global_scheduler_address, "");
    assert!(args.etcd_endpoints.is_empty());
    assert_eq!(args.etcd_table_prefix, "");
    assert_eq!(args.node_id, "domain-0");
    assert_eq!(args.election_mode, ElectionMode::Standalone);
    assert!(!args.enable_preemption);
    assert_eq!(args.max_priority, 100);
    assert_eq!(args.pull_resource_interval_ms, 5000);
}

#[test]
fn key_flags_parse_correctly() {
    let args = CliArgs::try_parse_from([
        "yr-domain-scheduler",
        "--host",
        "10.0.0.1",
        "--port",
        "9401",
        "--http-port",
        "9481",
        "--global-scheduler-address",
        "127.0.0.1:8400",
        "--etcd-endpoints",
        "127.0.0.1:2379",
        "--etcd-table-prefix",
        "/yr",
        "--node-id",
        "domain-7",
        "--election-mode",
        "k8s",
        "--enable-preemption",
        "--max-priority",
        "50",
        "--pull-resource-interval-ms",
        "3000",
    ])
    .expect("parse explicit flags");

    assert_eq!(args.host, "10.0.0.1");
    assert_eq!(args.port, 9401);
    assert_eq!(args.http_port, 9481);
    assert_eq!(args.global_scheduler_address, "127.0.0.1:8400");
    assert_eq!(args.etcd_endpoints, vec!["127.0.0.1:2379".to_string()]);
    assert_eq!(args.etcd_table_prefix, "/yr");
    assert_eq!(args.node_id, "domain-7");
    assert_eq!(args.election_mode, ElectionMode::K8s);
    assert!(args.enable_preemption);
    assert_eq!(args.max_priority, 50);
    assert_eq!(args.pull_resource_interval_ms, 3000);
}

#[test]
fn port_defaults_match_cpp_conventions() {
    let args = CliArgs::try_parse_from(["yr-domain-scheduler"]).expect("parse defaults");
    assert_eq!(args.port, 8401, "domain scheduler gRPC (8400 master, 8401 domain)");
    assert_eq!(args.http_port, 8481);
}

#[test]
fn help_documents_operational_and_conditional_requirements() {
    let help = long_help();
    for needle in [
        "--host",
        "--port",
        "--http-port",
        "--global-scheduler-address",
        "--etcd-endpoints",
        "--election-mode",
        "--node-id",
    ] {
        assert!(
            help.contains(needle),
            "help should mention `{needle}` (etcd required when election is etcd/k8s — validated after parse)"
        );
    }
}
