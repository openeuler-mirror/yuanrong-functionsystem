//! Standalone `yr-runtime-manager` process vs merge (`embedded_in_agent`) mode.
//!
//! Merge mode: gRPC port `0`, loopback host, runtime port pool from the agent.
//! Standalone: non-zero gRPC port, default `0.0.0.0`, own `runtime_initial_port` / `port_count`.

use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Arc;

use clap::Parser;
use yr_proto::internal::{StartInstanceRequest, StopInstanceRequest};
use yr_runtime_manager::port_manager::{PortManager, SharedPortManager};
use yr_runtime_manager::runtime_ops::start_instance_op;
use yr_runtime_manager::state::RuntimeManagerState;
use yr_runtime_manager::Config;

/// Separate-process RM listens on a real gRPC port; embedded merge mode uses `port == 0`.
fn is_standalone_process(cfg: &Config) -> bool {
    cfg.port != 0
}

#[test]
fn standalone_config_exposes_cpp_parity_network_fields() {
    let c = Config::try_parse_from([
        "yr-runtime-manager",
        "--host-ip",
        "192.168.1.10",
        "--proxy-ip",
        "192.168.1.1",
        "--proxy-grpc-server-port",
        "50051",
    ])
    .expect("parse standalone network flags");

    assert_eq!(c.host_ip, "192.168.1.10");
    assert_eq!(c.proxy_ip, "192.168.1.1");
    assert_eq!(c.proxy_grpc_server_port, "50051");
}

#[test]
fn default_standalone_binary_config_parses() {
    let c = Config::try_parse_from(["yr-runtime-manager"]).expect("default CLI parse");
    assert!(is_standalone_process(&c));
    assert_eq!(c.host, "0.0.0.0");
    assert_eq!(c.port, 8404);
    assert_eq!(c.grpc_listen_addr(), "0.0.0.0:8404");
    assert_eq!(c.runtime_initial_port, 9000);
    assert_eq!(c.port_count, 1000);
}

#[test]
fn standalone_default_leaves_proxy_ip_empty_operator_must_set() {
    let c = Config::try_parse_from(["yr-runtime-manager"]).unwrap();
    assert!(is_standalone_process(&c));
    assert!(
        c.proxy_ip.trim().is_empty(),
        "default CLI does not set proxy_ip; standalone deployments should pass --proxy-ip"
    );
}

#[test]
fn standalone_with_proxy_ip_satisfies_connect_back_expectation() {
    let c = Config::try_parse_from([
        "yr-runtime-manager",
        "--proxy-ip",
        "10.20.30.40",
    ])
    .unwrap();
    assert!(is_standalone_process(&c));
    assert_eq!(c.proxy_ip, "10.20.30.40");
}

#[test]
fn standalone_runtime_port_pool_spans_initial_through_count() {
    let base: u16 = 9100;
    let count: u32 = 5;
    let mut mgr = PortManager::new(base, count).expect("pool");
    // Pool covers [base, base + count); first pop is `base`, last is `base + count - 1`.
    let mut ports = Vec::new();
    for i in 0..count {
        ports.push(
            mgr.allocate(&format!("rt-{i}"))
                .unwrap_or_else(|e| panic!("allocate {i}: {e}")),
        );
    }
    assert_eq!(ports.first().copied(), Some(base));
    assert_eq!(
        ports.last().copied(),
        Some(base.checked_add((count - 1) as u16).unwrap())
    );
}

#[test]
fn standalone_rm_http_port_is_configurable() {
    let c = Config::try_parse_from([
        "yr-runtime-manager",
        "--port",
        "8404",
        "--http-host",
        "127.0.0.1",
        "--http-port",
        "18080",
    ])
    .unwrap();
    assert_eq!(c.http_listen_addr(), "127.0.0.1:18080");
}

#[test]
fn embedded_in_agent_sets_merge_mode_defaults() {
    let log = PathBuf::from("/tmp/yr-rm-embedded-test");
    let c = Config::embedded_in_agent(
        "node-merge".into(),
        "http://127.0.0.1:8403".into(),
        "/bin/true".into(),
        41000,
        64,
        log,
        "".into(),
    );
    assert_eq!(c.host, "127.0.0.1");
    assert_eq!(c.port, 0);
    assert!(!is_standalone_process(&c));
    assert_eq!(c.http_host, None);
    assert_eq!(c.http_port, None);
    assert_eq!(c.grpc_listen_addr(), "127.0.0.1:0");
}

#[test]
fn embedded_mode_runtime_port_pool_comes_from_agent_arguments() {
    let log = PathBuf::from("/tmp/yr-rm-embedded-ports");
    let c = Config::embedded_in_agent(
        "n".into(),
        "http://127.0.0.1:9999".into(),
        "/bin/true".into(),
        50000,
        128,
        log,
        "".into(),
    );
    assert_eq!(c.runtime_initial_port, 50000);
    assert_eq!(c.port_count, 128);
}

#[test]
fn standalone_mode_uses_cli_runtime_port_range() {
    let embedded = Config::embedded_in_agent(
        "n".into(),
        "http://127.0.0.1:1".into(),
        "/bin/true".into(),
        40000,
        10,
        PathBuf::from("/tmp/x"),
        "".into(),
    );
    let standalone = Config::try_parse_from([
        "yr-runtime-manager",
        "--runtime-initial-port",
        "30000",
        "--port-count",
        "500",
    ])
    .unwrap();
    assert_ne!(
        embedded.runtime_initial_port,
        standalone.runtime_initial_port
    );
    assert_ne!(embedded.port_count, standalone.port_count);
    assert_eq!(standalone.runtime_initial_port, 30000);
    assert_eq!(standalone.port_count, 500);
}

#[test]
fn config_can_switch_between_embedded_and_standalone_shape() {
    let mut c = Config::embedded_in_agent(
        "node".into(),
        "http://127.0.0.1:8403".into(),
        "/bin/sleep".into(),
        40200,
        32,
        PathBuf::from("/tmp/yr-switch"),
        "".into(),
    );
    assert!(!is_standalone_process(&c));

    c.host = "0.0.0.0".into();
    c.port = 8404;
    c.proxy_ip = "10.0.0.2".into();
    assert!(is_standalone_process(&c));
    assert_eq!(c.grpc_listen_addr(), "0.0.0.0:8404");
    assert_eq!(c.runtime_initial_port, 40200);
}

#[test]
fn start_instance_request_matches_runtime_manager_contract() {
    let req = StartInstanceRequest {
        instance_id: "inst-standalone".into(),
        function_name: "hello".into(),
        tenant_id: "tenant-a".into(),
        runtime_type: "python".into(),
        env_vars: HashMap::from([("FOO".into(), "bar".into())]),
        resources: HashMap::from([("memory".into(), 1.0)]),
        code_path: "/tmp/wd".into(),
        config_json: r#"{"health":{}}"#.into(),
    };
    assert_eq!(req.instance_id, "inst-standalone");
    assert_eq!(req.function_name, "hello");
    assert_eq!(req.runtime_type, "python");
    assert_eq!(req.env_vars.get("FOO").map(String::as_str), Some("bar"));
}

#[test]
fn stop_instance_request_matches_runtime_manager_contract() {
    let req = StopInstanceRequest {
        instance_id: "inst-standalone".into(),
        runtime_id: "rt-inst-standalone-1".into(),
        force: true,
    };
    assert_eq!(req.runtime_id, "rt-inst-standalone-1");
    assert!(req.force);
}

#[test]
fn port_allocation_in_standalone_shaped_config_uses_shared_manager_range() {
    let log = std::env::temp_dir().join(format!("yr_rm_standalone_ports_{}", std::process::id()));
    let _ = std::fs::create_dir_all(&log);
    let cfg = Arc::new(Config::try_parse_from([
        "yr-runtime-manager",
        "--runtime-initial-port",
        "43000",
        "--port-count",
        "8",
        "--runtime-paths",
        "/bin/true",
        "--log-path",
        log.to_str().unwrap(),
    ])
    .unwrap());
    cfg.ensure_log_dir().unwrap();
    let ports = Arc::new(
        SharedPortManager::new(cfg.runtime_initial_port, cfg.port_count).expect("pool"),
    );
    let state = Arc::new(RuntimeManagerState::new(cfg.clone(), ports.clone()));

    let p1 = ports.allocate("rt-one").expect("alloc 1");
    let p2 = ports.allocate("rt-two").expect("alloc 2");
    assert_ne!(p1, p2);
    assert!(p1 >= 43000 && p1 < 43000 + 8);
    assert!(p2 >= 43000 && p2 < 43000 + 8);

    let paths = vec!["/bin/true".into()];
    let start = StartInstanceRequest {
        instance_id: "alloc-test".into(),
        function_name: "f".into(),
        tenant_id: "t".into(),
        runtime_type: "0".into(),
        env_vars: HashMap::new(),
        resources: HashMap::new(),
        code_path: ".".into(),
        config_json: "{}".into(),
    };
    let resp = start_instance_op(&state, &paths, start).expect("start");
    assert!(resp.success);
    assert!(resp.runtime_port >= 43000 && (resp.runtime_port as u32) < 43000 + 8);
}
