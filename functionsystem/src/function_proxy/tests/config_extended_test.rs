//! Extended CLI/config parsing and derived helpers.

use std::sync::Arc;

use clap::Parser;
use yr_proxy::config::Config;
use yr_proxy::instance_ctrl::InstanceController;
use yr_proxy::resource_view::{ResourceVector, ResourceView};

fn parse(args: &[&str]) -> Config {
    let mut v = vec!["yr-proxy"];
    v.extend_from_slice(args);
    Config::try_parse_from(v).expect("parse")
}

#[test]
fn defaults_host_and_ports() {
    let c = parse(&["--node-id", "n1"]);
    assert_eq!(c.host, "0.0.0.0");
    assert_eq!(c.port, 8402);
    assert_eq!(c.posix_port, 8403);
    assert_eq!(c.http_port, 18402);
    assert_eq!(c.session_grpc_port, 18403);
}

#[test]
fn defaults_scheduler_and_storage_flags() {
    let c = parse(&["--node-id", "n1"]);
    assert_eq!(c.global_scheduler_address, "");
    assert_eq!(c.domain_scheduler_address, "");
    assert_eq!(c.state_storage_type, "disable");
    assert_eq!(c.election_mode, "standalone");
}

#[test]
fn defaults_resource_bounds() {
    let c = parse(&["--node-id", "n1"]);
    assert!(!c.enable_preemption);
    assert_eq!(c.min_instance_cpu, 0.1);
    assert_eq!(c.max_instance_cpu, 64.0);
    assert_eq!(c.min_instance_memory, 134217728.0);
    assert_eq!(c.max_instance_memory, 68719476736.0);
}

#[test]
fn defaults_busproxy_registration() {
    let c = parse(&["--node-id", "n1"]);
    assert_eq!(c.proxy_aid, "");
    assert_eq!(c.proxy_access_key, "");
    assert_eq!(c.busproxy_tenant_segment, "0");
    assert_eq!(c.busproxy_lease_ttl_sec, 30);
}

#[test]
fn defaults_runtime_and_merge_flags() {
    let c = parse(&["--node-id", "n1"]);
    assert!(!c.enable_merge_process);
    assert_eq!(c.merge_runtime_paths, "/bin/sleep");
    assert_eq!(c.merge_runtime_initial_port, 9000);
    assert_eq!(c.merge_port_count, 1000);
    assert!(!c.runtime_recover_enable);
    assert!(c.runtime_heartbeat_enable);
}

#[test]
fn defaults_iam_and_meta_store() {
    let c = parse(&["--node-id", "n1"]);
    assert!(!c.enable_iam);
    assert_eq!(c.iam_credential_type, "token");
    assert!(!c.enable_meta_store);
    assert_eq!(c.meta_store_mode, "local");
    assert_eq!(c.service_register_times, 1000);
}

#[test]
fn explicit_ports_and_host_override() {
    let c = parse(&[
        "--node-id",
        "x",
        "--host",
        "10.0.0.5",
        "--grpc-listen-port",
        "9001",
        "--posix-port",
        "9002",
        "--http-port",
        "19000",
        "--session-grpc-port",
        "19001",
    ]);
    assert_eq!(c.host, "10.0.0.5");
    assert_eq!(c.port, 9001);
    assert_eq!(c.posix_port, 9002);
    assert_eq!(c.http_port, 19000);
    assert_eq!(c.session_grpc_port, 19001);
}

#[test]
fn explicit_boolish_enable_server_mode_off() {
    let c = parse(&["--node-id", "n", "--enable-server-mode", "false"]);
    assert!(!c.enable_server_mode);
}

#[test]
fn explicit_boolish_enable_server_mode_on() {
    let c = parse(&["--node-id", "n", "--enable-server-mode", "yes"]);
    assert!(c.enable_server_mode);
}

#[test]
fn enable_flags_merge_driver_meta_combo() {
    let c = parse(&[
        "--node-id",
        "n",
        "--enable-driver",
        "--enable-merge-process",
        "--enable-meta-store",
        "--forward-compatibility",
    ]);
    assert!(c.enable_driver);
    assert!(c.enable_merge_process);
    assert!(c.enable_meta_store);
    assert!(c.forward_compatibility);
}

#[test]
fn etcd_endpoints_vec_trims_and_skips_empty() {
    let c = parse(&[
        "--node-id",
        "n",
        "--etcd-endpoints",
        " http:1 , ,http:2 ",
    ]);
    assert_eq!(c.etcd_endpoints_vec(), vec!["http:1", "http:2"]);
}

#[test]
fn schedule_plugins_empty_string_yields_empty_object() {
    let c = parse(&["--node-id", "n", "--schedule-plugins", "   "]);
    let p = c.schedule_plugins_config().expect("plugins");
    assert_eq!(p.raw, serde_json::json!({}));
}

#[test]
fn schedule_plugins_explicit_json_round_trips() {
    let c = parse(&[
        "--node-id",
        "n",
        "--schedule-plugins",
        r#"{"a":1,"b":"x"}"#,
    ]);
    let p = c.schedule_plugins_config().expect("plugins");
    assert_eq!(p.raw["a"], 1);
    assert_eq!(p.raw["b"], "x");
}

#[test]
fn schedule_plugins_invalid_json_errors() {
    let c = parse(&["--node-id", "n", "--schedule-plugins", "{"]);
    assert!(c.schedule_plugins_config().is_err());
}

#[test]
fn grpc_listen_addr_and_advertise_endpoint() {
    let c = parse(&[
        "--node-id",
        "n",
        "--host",
        "192.168.1.2",
        "--grpc-listen-port",
        "7777",
    ]);
    assert_eq!(c.grpc_listen_addr(), "192.168.1.2:7777");
    assert_eq!(c.advertise_grpc_endpoint(), "http://192.168.1.2:7777");
}

#[test]
fn start_instance_without_runtime_address_is_rejected() {
    let c = Arc::new(parse(&["--node-id", "n"]));
    let rv = ResourceView::new(ResourceVector {
        cpu: 4.0,
        memory: 8.0,
        npu: 0.0,
    });
    let ctrl = InstanceController::new(c.clone(), rv, None, None);
    let rt = tokio::runtime::Runtime::new().unwrap();
    let err = rt
        .block_on(ctrl.start_instance(
            "i1",
            "fn",
            "t",
            Default::default(),
            "default",
        ))
        .expect_err("missing runtime_manager_address");
    assert_eq!(err.code(), tonic::Code::FailedPrecondition);
}

#[test]
fn exec_session_idle_default() {
    let c = parse(&["--node-id", "n"]);
    assert_eq!(c.exec_session_idle_sec, 120);
}

#[test]
fn max_grpc_size_default_mb() {
    let c = parse(&["--node-id", "n"]);
    assert_eq!(c.max_grpc_size, 11);
}

#[test]
fn decrypt_algorithm_default() {
    let c = parse(&["--node-id", "n"]);
    assert_eq!(c.decrypt_algorithm, "NO_CRYPTO");
}

#[test]
fn tenant_affinity_default_off() {
    let c = parse(&["--node-id", "n"]);
    assert!(!c.enable_tenant_affinity);
}

#[test]
fn create_rate_limit_default_zero_means_disabled() {
    let c = parse(&["--node-id", "n"]);
    assert_eq!(c.create_rate_limit_per_sec, 0);
}
