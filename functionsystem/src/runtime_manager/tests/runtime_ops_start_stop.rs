//! In-process start/stop validation (uses `/bin/true` where possible).

use std::collections::HashMap;
use std::sync::Arc;

use yr_proto::internal::{StartInstanceRequest, StopInstanceRequest};
use yr_runtime_manager::port_manager::SharedPortManager;
use yr_runtime_manager::runtime_ops::{start_instance_op, stop_instance_op};
use yr_runtime_manager::state::RuntimeManagerState;
use yr_runtime_manager::Config;

fn test_state() -> Arc<RuntimeManagerState> {
    let log = std::env::temp_dir().join(format!("yr_rm_ops_{}", std::process::id()));
    let _ = std::fs::create_dir_all(&log);
    let cfg = Arc::new(Config::embedded_in_agent(
        "node-t".into(),
        "http://127.0.0.1:9999".into(),
        "/bin/true".into(),
        40300,
        20,
        log,
        "".into(),
    ));
    cfg.ensure_log_dir().unwrap();
    let ports = Arc::new(SharedPortManager::new(40300, 20).unwrap());
    Arc::new(RuntimeManagerState::new(cfg, ports))
}

fn minimal_start(instance_id: &str) -> StartInstanceRequest {
    StartInstanceRequest {
        instance_id: instance_id.into(),
        function_name: "fn".into(),
        tenant_id: "t".into(),
        runtime_type: "0".into(),
        env_vars: HashMap::new(),
        resources: HashMap::new(),
        code_path: ".".into(),
        config_json: "{}".into(),
    }
}

#[test]
fn start_instance_rejects_empty_id() {
    let st = test_state();
    let paths = vec!["/bin/true".into()];
    let err = start_instance_op(&st, &paths, minimal_start("   "))
        .expect_err("empty instance_id");
    assert_eq!(err.code(), tonic::Code::InvalidArgument);
}

#[test]
fn start_then_stop_succeeds() {
    let st = test_state();
    let paths = vec!["/bin/true".into()];
    let resp = start_instance_op(&st, &paths, minimal_start("inst-1")).expect("start");
    assert!(resp.success);
    let rid = resp.runtime_id;
    let stop = stop_instance_op(
        &st,
        StopInstanceRequest {
            instance_id: "inst-1".into(),
            runtime_id: rid,
            force: true,
        },
    )
    .expect("stop");
    assert!(stop.success);
}

#[test]
fn duplicate_start_returns_already_exists() {
    let st = test_state();
    let paths = vec!["/bin/true".into()];
    let req = minimal_start("dup-i");
    start_instance_op(&st, &paths, req.clone()).unwrap();
    let err = start_instance_op(&st, &paths, req).expect_err("duplicate");
    assert_eq!(err.code(), tonic::Code::AlreadyExists);
}

#[test]
fn stop_unknown_runtime_returns_failure_response() {
    let st = test_state();
    let out = stop_instance_op(
        &st,
        StopInstanceRequest {
            instance_id: "x".into(),
            runtime_id: "no-such-rt".into(),
            force: false,
        },
    )
    .unwrap();
    assert!(!out.success);
    assert!(out.message.contains("unknown"));
}

#[test]
fn start_fails_when_no_runtime_executable_configured() {
    let st = test_state();
    let paths: Vec<String> = vec![];
    let err = start_instance_op(&st, &paths, minimal_start("no-exe")).expect_err("no exe");
    assert_eq!(err.code(), tonic::Code::Internal);
}
