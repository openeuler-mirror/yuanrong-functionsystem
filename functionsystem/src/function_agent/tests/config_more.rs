//! Extra config parsing and derived address helpers.

use clap::Parser;
use yr_agent::config::Config;

#[test]
fn normalize_grpc_uri_adds_http_scheme() {
    assert_eq!(
        Config::normalize_grpc_uri("127.0.0.1:8402"),
        "http://127.0.0.1:8402"
    );
}

#[test]
fn normalize_grpc_uri_preserves_explicit_scheme() {
    assert_eq!(
        Config::normalize_grpc_uri("https://example:443/path"),
        "https://example:443/path"
    );
}

#[test]
fn normalize_grpc_uri_trims_whitespace() {
    assert_eq!(
        Config::normalize_grpc_uri("  10.0.0.1:9000  "),
        "http://10.0.0.1:9000"
    );
}

#[test]
fn listen_addrs_use_host_and_ports() {
    let c = Config::try_parse_from([
        "yr-agent",
        "--host",
        "10.0.0.2",
        "--port",
        "18080",
        "--agent-listen-port",
        "22000",
    ])
    .unwrap();
    assert_eq!(c.grpc_listen_addr(), "10.0.0.2:22000");
    assert_eq!(c.http_listen_addr(), "10.0.0.2:18080");
    assert_eq!(c.agent_grpc_endpoint(), "http://10.0.0.2:22000");
}

#[test]
fn merge_process_related_flags_parse() {
    let c = Config::try_parse_from([
        "yr-agent",
        "--merge-runtime-paths",
        "/opt/a,/opt/b",
        "--merge-runtime-bind-mounts",
        "/host/data:/work:ro,/tmp/x:/y",
        "--merge-runtime-log-path",
        "/var/log/yr-merge",
    ])
    .unwrap();
    assert_eq!(c.merge_runtime_paths, "/opt/a,/opt/b");
    assert_eq!(
        c.merge_runtime_bind_mounts,
        "/host/data:/work:ro,/tmp/x:/y"
    );
    assert_eq!(c.merge_runtime_log_path, "/var/log/yr-merge");
}
