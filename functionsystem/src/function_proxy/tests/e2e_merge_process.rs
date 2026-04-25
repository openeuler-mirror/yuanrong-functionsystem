//! E2E: merge_process config wires embedded runtime manager (local start/stop path).

use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Arc;

use clap::Parser;
use yr_proto::internal::{StartInstanceRequest, StopInstanceRequest};
use yr_proxy::config::Config;
use yr_runtime_manager::port_manager::SharedPortManager;
use yr_runtime_manager::runtime_ops::{start_instance_op, stop_instance_op};
use yr_runtime_manager::state::RuntimeManagerState;
use yr_runtime_manager::Config as RmConfig;

#[test]
fn merge_process_proxy_config_embeds_rm_network_shape() {
    let log = std::env::temp_dir().join(format!("yr_merge_e2e_{}", std::process::id()));
    let _ = std::fs::create_dir_all(&log);
    let c = Config::try_parse_from([
        "yr-proxy",
        "--node-id",
        "merge-e2e",
        "--grpc-listen-port",
        "1",
        "--enable-merge-process",
        "true",
        "--runtime-manager-address",
        "http://127.0.0.1:9",
        "--merge-runtime-paths",
        "/bin/true",
        "--merge-runtime-initial-port",
        "40100",
        "--merge-port-count",
        "16",
        "--merge-runtime-log-path",
        log.to_str().unwrap(),
    ])
    .expect("parse merge_process proxy");

    assert!(c.enable_merge_process);
    assert_eq!(c.merge_runtime_initial_port, 40100);
    assert_eq!(c.merge_port_count, 16);

    let agent_uri = "http://127.0.0.1:9".to_string();
    let rm = Arc::new(RmConfig::embedded_in_agent(
        c.node_id.clone(),
        agent_uri.clone(),
        c.merge_runtime_paths.clone(),
        c.merge_runtime_initial_port,
        c.merge_port_count,
        PathBuf::from(&c.merge_runtime_log_path),
        c.merge_runtime_bind_mounts.clone(),
    ));
    rm.ensure_log_dir().expect("log dir");
    assert_eq!(rm.grpc_listen_addr(), "127.0.0.1:0");
    assert_eq!(rm.agent_address, agent_uri);
    assert_eq!(rm.runtime_initial_port, 40100);
    assert_eq!(rm.port_count, 16);

    let ports =
        Arc::new(SharedPortManager::new(rm.runtime_initial_port, rm.port_count).expect("ports"));
    let state = Arc::new(RuntimeManagerState::new(rm.clone(), ports));
    let paths = rm.runtime_path_list();
    let start = StartInstanceRequest {
        instance_id: "merge-local-1".into(),
        function_name: "fn".into(),
        tenant_id: "t".into(),
        runtime_type: "0".into(),
        env_vars: HashMap::new(),
        resources: HashMap::new(),
        code_path: ".".into(),
        config_json: "{}".into(),
    };
    let out = start_instance_op(&state, &paths, start).expect("start");
    assert!(out.success);
    let stop = stop_instance_op(
        &state,
        StopInstanceRequest {
            instance_id: "merge-local-1".into(),
            runtime_id: out.runtime_id,
            force: true,
        },
    )
    .expect("stop");
    assert!(stop.success);
}
