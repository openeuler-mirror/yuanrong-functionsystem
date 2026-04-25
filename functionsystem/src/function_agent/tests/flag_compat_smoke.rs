//! CLI flag compatibility smoke tests for function agent (`config::Config`).

use clap::{CommandFactory, Parser};
use yr_agent::config::Config;

fn long_help() -> String {
    format!("{}", Config::command().render_long_help())
}

#[test]
fn default_values_match_expectations() {
    let c = Config::try_parse_from(["yr-agent"]).expect("parse defaults");
    assert_eq!(c.host, "0.0.0.0");
    assert_eq!(c.port, 18403);
    assert_eq!(c.node_id, "");
    assert_eq!(c.local_scheduler_address, "http://127.0.0.1:8402");
    assert_eq!(c.agent_listen_port, 22799);
    assert_eq!(c.s3_endpoint, "");
    assert_eq!(c.s3_bucket, "");
    assert_eq!(c.code_package_dir, "/tmp/yr-agent-code");
    assert_eq!(c.runtime_manager_address, "");
    assert_eq!(c.data_system_host, "127.0.0.1");
    assert_eq!(c.data_system_port, 31501);
    assert!(!c.enable_merge_process);
    assert_eq!(c.merge_runtime_paths, "/bin/sleep");
    assert_eq!(c.merge_runtime_initial_port, 9000);
    assert_eq!(c.merge_port_count, 1000);
    assert_eq!(c.merge_runtime_log_path, "/tmp/yr-agent-runtime-logs");
    assert_eq!(c.merge_runtime_bind_mounts, "");
}

#[test]
fn key_flags_parse_correctly() {
    let c = Config::try_parse_from([
        "yr-agent",
        "--host",
        "10.0.0.3",
        "--port",
        "28403",
        "--node-id",
        "agent-1",
        "--local-scheduler-address",
        "127.0.0.1:9402",
        "--agent-listen-port",
        "32799",
        "--runtime-manager-address",
        "http://127.0.0.1:8404",
        "--data-system-host",
        "10.0.0.9",
        "--data-system-port",
        "41501",
        "--code-package-dir",
        "/var/lib/yr/code",
        "--enable-merge-process",
        "--merge-runtime-initial-port",
        "9100",
        "--merge-port-count",
        "500",
    ])
    .expect("parse explicit flags");

    assert_eq!(c.host, "10.0.0.3");
    assert_eq!(c.port, 28403);
    assert_eq!(c.node_id, "agent-1");
    assert_eq!(c.local_scheduler_address, "127.0.0.1:9402");
    assert_eq!(c.agent_listen_port, 32799);
    assert_eq!(c.runtime_manager_address, "http://127.0.0.1:8404");
    assert_eq!(c.data_system_host, "10.0.0.9");
    assert_eq!(c.data_system_port, 41501);
    assert_eq!(c.code_package_dir, "/var/lib/yr/code");
    assert!(c.enable_merge_process);
    assert_eq!(c.merge_runtime_initial_port, 9100);
    assert_eq!(c.merge_port_count, 500);
}

#[test]
fn port_defaults_match_cpp_conventions() {
    let c = Config::try_parse_from(["yr-agent"]).expect("parse defaults");
    assert_eq!(
        c.local_scheduler_address, "http://127.0.0.1:8402",
        "default local scheduler follows function_proxy :8402"
    );
    assert_eq!(c.agent_listen_port, 22799, "FunctionAgentService gRPC");
    assert_eq!(c.port, 18403, "HTTP health / readiness");
}

#[test]
fn help_documents_key_operational_flags() {
    let help = long_help();
    for needle in [
        "--ip",
        "--port",
        "--agent_listen_port",
        "--local_scheduler_address",
        "--runtime_manager_address",
        "--data_system_port",
        "--enable_merge_process",
    ] {
        assert!(help.contains(needle), "help should mention `{needle}`");
    }
}

#[test]
fn cpp_runtime_dir_feeds_merge_runtime_paths_and_library_path() {
    let c = Config::try_parse_from([
        "yr-agent",
        "--enable_merge_process=true",
        "--runtime_dir=/opt/yr/runtime/service",
        "--runtime_ld_library_path=/opt/yr/runtime/service/python/yr:/usr/lib64",
    ])
    .expect("parse runtime dir flags");

    assert_eq!(
        c.effective_merge_runtime_paths(),
        "/opt/yr/runtime/service/cpp/bin/runtime,/opt/yr/runtime/service/go/bin/goruntime"
    );
    let ld = c.effective_runtime_ld_library_path();
    assert!(ld.contains("/opt/yr/runtime/service/cpp/lib"));
    assert!(ld.contains("/opt/yr/runtime/service/python/yr"));
}
