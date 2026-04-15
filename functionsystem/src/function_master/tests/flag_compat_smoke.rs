//! CLI flag compatibility smoke tests (C++ / ops conventions vs `CliArgs` defaults).

use clap::{CommandFactory, Parser};
use yr_master::config::{AssignmentStrategy, CliArgs, ElectionMode};

fn long_help() -> String {
    format!("{}", CliArgs::command().render_long_help())
}

#[test]
fn default_values_match_expectations() {
    let args = CliArgs::try_parse_from(["yr-master"]).expect("parse defaults");
    assert_eq!(args.host, "0.0.0.0");
    assert_eq!(args.port, 8400);
    assert_eq!(args.http_port, 8480);
    assert!(args.etcd_endpoints.is_empty());
    assert_eq!(args.etcd_table_prefix, "");
    assert_eq!(args.cluster_id, "default");
    assert_eq!(args.election_mode, ElectionMode::Standalone);
    assert_eq!(args.max_locals_per_domain, 64);
    assert_eq!(args.max_domain_sched_per_domain, 1000);
    assert_eq!(args.schedule_retry_sec, 10);
    assert_eq!(args.domain_schedule_timeout_ms, 5000);
    assert!(args.enable_meta_store);
    assert_eq!(args.meta_store_address, "");
    assert_eq!(args.meta_store_port, 2389);
    assert_eq!(args.assignment_strategy, AssignmentStrategy::LeastLoaded);
    assert_eq!(args.default_domain_address, "127.0.0.1:8401");
}

#[test]
fn new_cpp_compat_flags_defaults() {
    let args = CliArgs::try_parse_from(["yr-master"]).expect("parse defaults");
    assert_eq!(args.node_id, "");
    assert!(!args.enable_persistence);
    assert!(!args.runtime_recover_enable);
    assert!(args.is_schedule_tolerate_abnormal);
    assert_eq!(args.decrypt_algorithm, "NO_CRYPTO");
    assert_eq!(args.schedule_plugins, "");
    assert!(!args.migrate_enable);
    assert_eq!(args.grace_period_seconds, 25);
    assert_eq!(args.health_monitor_max_failure, 5);
    assert_eq!(args.health_monitor_retry_interval, 3000);
    assert!(!args.enable_horizontal_scale);
    assert_eq!(args.pool_config_path, "");
    assert_eq!(args.domain_heartbeat_timeout, 6000);
    assert_eq!(args.system_tenant_id, "0");
    assert_eq!(args.services_path, "/");
    assert_eq!(args.lib_path, "/");
    assert_eq!(args.function_meta_path, "/home/sn/function-metas");
    assert!(!args.enable_sync_sys_func);
    assert_eq!(args.meta_store_mode, "local");
    assert_eq!(args.meta_store_max_flush_concurrency, 100);
    assert_eq!(args.meta_store_max_flush_batch_size, 50);
}

#[test]
fn new_cpp_compat_flags_parse() {
    let args = CliArgs::try_parse_from([
        "yr-master",
        "--node-id",
        "master-1",
        "--enable-persistence",
        "--runtime-recover-enable",
        "--is-schedule-tolerate-abnormal",
        "false",
        "--decrypt-algorithm",
        "AES",
        "--schedule-plugins",
        "p1,p2",
        "--migrate-enable",
        "--grace-period-seconds",
        "60",
        "--health-monitor-max-failure",
        "10",
        "--health-monitor-retry-interval",
        "5000",
        "--enable-horizontal-scale",
        "--pool-config-path",
        "/etc/pool.yaml",
        "--domain-heartbeat-timeout",
        "9000",
        "--system-tenant-id",
        "42",
        "--services-path",
        "/svc",
        "--lib-path",
        "/lib",
        "--function-meta-path",
        "/meta/funcs",
        "--enable-sync-sys-func",
        "--meta-store-mode",
        "etcd",
        "--meta-store-max-flush-concurrency",
        "200",
        "--meta-store-max-flush-batch-size",
        "75",
    ])
    .expect("parse new C++ compat flags");

    assert_eq!(args.node_id, "master-1");
    assert!(args.enable_persistence);
    assert!(args.runtime_recover_enable);
    assert!(!args.is_schedule_tolerate_abnormal);
    assert_eq!(args.decrypt_algorithm, "AES");
    assert_eq!(args.schedule_plugins, "p1,p2");
    assert!(args.migrate_enable);
    assert_eq!(args.grace_period_seconds, 60);
    assert_eq!(args.health_monitor_max_failure, 10);
    assert_eq!(args.health_monitor_retry_interval, 5000);
    assert!(args.enable_horizontal_scale);
    assert_eq!(args.pool_config_path, "/etc/pool.yaml");
    assert_eq!(args.domain_heartbeat_timeout, 9000);
    assert_eq!(args.system_tenant_id, "42");
    assert_eq!(args.services_path, "/svc");
    assert_eq!(args.lib_path, "/lib");
    assert_eq!(args.function_meta_path, "/meta/funcs");
    assert!(args.enable_sync_sys_func);
    assert_eq!(args.meta_store_mode, "etcd");
    assert_eq!(args.meta_store_max_flush_concurrency, 200);
    assert_eq!(args.meta_store_max_flush_batch_size, 75);
}

#[test]
fn key_flags_parse_correctly() {
    let args = CliArgs::try_parse_from([
        "yr-master",
        "--host",
        "10.0.0.1",
        "--port",
        "9400",
        "--http-port",
        "9480",
        "--etcd-endpoints",
        "127.0.0.1:2379,127.0.0.1:2380",
        "--etcd-table-prefix",
        "/yr",
        "--cluster-id",
        "prod",
        "--election-mode",
        "etcd",
        "--max-locals-per-domain",
        "32",
        "--max-domain-sched-per-domain",
        "500",
        "--schedule-retry-sec",
        "7",
        "--domain-schedule-timeout-ms",
        "2500",
        "--meta-store-address",
        "10.0.0.2:2390",
        "--meta-store-port",
        "2391",
        "--assignment-strategy",
        "round-robin",
        "--default-domain-address",
        "10.0.0.3:9401",
    ])
    .expect("parse explicit flags");

    assert_eq!(args.host, "10.0.0.1");
    assert_eq!(args.port, 9400);
    assert_eq!(args.http_port, 9480);
    assert_eq!(
        args.etcd_endpoints,
        vec!["127.0.0.1:2379".to_string(), "127.0.0.1:2380".to_string()]
    );
    assert_eq!(args.etcd_table_prefix, "/yr");
    assert_eq!(args.cluster_id, "prod");
    assert_eq!(args.election_mode, ElectionMode::Etcd);
    assert_eq!(args.max_locals_per_domain, 32);
    assert_eq!(args.max_domain_sched_per_domain, 500);
    assert_eq!(args.schedule_retry_sec, 7);
    assert_eq!(args.domain_schedule_timeout_ms, 2500);
    assert!(args.enable_meta_store);
    assert_eq!(args.meta_store_address, "10.0.0.2:2390");
    assert_eq!(args.meta_store_port, 2391);
    assert_eq!(args.assignment_strategy, AssignmentStrategy::RoundRobin);
    assert_eq!(args.default_domain_address, "10.0.0.3:9401");
}

#[test]
fn port_defaults_match_cpp_conventions() {
    let args = CliArgs::try_parse_from(["yr-master"]).expect("parse defaults");
    assert_eq!(args.port, 8400, "function_master gRPC (C++ convention)");
    assert_eq!(args.http_port, 8480);
    assert_eq!(
        args.default_domain_address, "127.0.0.1:8401",
        "default domain scheduler advertisement"
    );
}

#[test]
fn election_mode_parses_standalone_explicit() {
    let args = CliArgs::try_parse_from(["yr-master", "--election-mode", "standalone"]).unwrap();
    assert_eq!(args.election_mode, ElectionMode::Standalone);
}

#[test]
fn election_mode_parses_etcd() {
    let args = CliArgs::try_parse_from(["yr-master", "--election-mode", "etcd"]).unwrap();
    assert_eq!(args.election_mode, ElectionMode::Etcd);
}

#[test]
fn election_mode_parses_txn() {
    let args = CliArgs::try_parse_from(["yr-master", "--election-mode", "txn"]).unwrap();
    assert_eq!(args.election_mode, ElectionMode::Txn);
}

#[test]
fn election_mode_parses_k8s() {
    let args = CliArgs::try_parse_from(["yr-master", "--election-mode", "k8s"]).unwrap();
    assert_eq!(args.election_mode, ElectionMode::K8s);
}

#[test]
fn enable_meta_store_is_true_by_default_and_accepts_flag() {
    let defaults = CliArgs::try_parse_from(["yr-master"]).unwrap();
    assert!(defaults.enable_meta_store);
    let with_flag = CliArgs::try_parse_from(["yr-master", "--enable-meta-store"]).unwrap();
    assert!(with_flag.enable_meta_store);
}

#[test]
fn help_documents_operational_and_conditional_requirements() {
    let help = long_help();
    for needle in [
        "--host",
        "--port",
        "--http-port",
        "--etcd-endpoints",
        "--election-mode",
        "--enable-meta-store",
        "--cluster-id",
    ] {
        assert!(
            help.contains(needle),
            "help should mention `{needle}` (etcd / election / meta-store paths are validated after parse)"
        );
    }
}
