//! Runtime-manager OOM lifecycle parity with C++ HealthCheckActor/RuntimeManager.

use std::collections::HashMap;
use std::sync::Arc;
use std::time::Instant;

use yr_runtime_manager::oom::monitor::{
    memory_limit_mb_for_resource, oom_kill_status_request, RUNTIME_MEMORY_EXCEED_LIMIT_MESSAGE,
};
use yr_runtime_manager::port_manager::SharedPortManager;
use yr_runtime_manager::state::{RunningProcess, RuntimeManagerState};
use yr_runtime_manager::Config;

fn test_state() -> Arc<RuntimeManagerState> {
    let log = std::env::temp_dir().join(format!("yr_rm_oom_{}", std::process::id()));
    let _ = std::fs::create_dir_all(&log);
    let cfg = Arc::new(Config::embedded_in_agent(
        "node-oom".into(),
        "http://127.0.0.1:9".into(),
        "/bin/true".into(),
        47300,
        8,
        log,
        "".into(),
    ));
    let ports = Arc::new(SharedPortManager::new(47300, 8).unwrap());
    Arc::new(RuntimeManagerState::new(cfg, ports))
}

fn running_process(instance_id: &str, runtime_id: &str, pid: i32) -> RunningProcess {
    RunningProcess {
        instance_id: instance_id.into(),
        runtime_id: runtime_id.into(),
        pid,
        port: 47301,
        status: "running".into(),
        exit_code: None,
        error_message: String::new(),
        cgroup_path: None,
        bind_mount_points: Vec::new(),
        health_spec: Default::default(),
        started_at: Instant::now(),
        resources: HashMap::new(),
    }
}

#[test]
fn oom_status_request_matches_cpp_runtime_memory_exceed_limit() {
    let req = oom_kill_status_request("instance-1", "runtime-1");

    assert_eq!(req.instance_id, "instance-1");
    assert_eq!(req.runtime_id, "runtime-1");
    assert_eq!(req.status, "oom_killed");
    assert_eq!(req.exit_code, -1);
    assert_eq!(req.error_message, RUNTIME_MEMORY_EXCEED_LIMIT_MESSAGE);
}

#[test]
fn memory_resource_values_are_megabytes_like_cpp_metrics() {
    assert_eq!(memory_limit_mb_for_resource(500.0), Some(500));
    assert_eq!(memory_limit_mb_for_resource(128.0), Some(128));
    assert_eq!(memory_limit_mb_for_resource(0.0), None);
    assert_eq!(memory_limit_mb_for_resource(-1.0), None);
}

#[test]
fn oom_mark_is_consumed_once_and_cleared_with_runtime_removal() {
    let state = test_state();
    state.insert_running(running_process("instance-1", "runtime-1", 12345));

    assert!(!state.take_oom_kill_mark("runtime-1"));
    state.mark_oom_kill_in_advance("runtime-1");
    assert!(state.take_oom_kill_mark("runtime-1"));
    assert!(!state.take_oom_kill_mark("runtime-1"));

    state.mark_oom_kill_in_advance("runtime-1");
    let removed = state.remove_by_runtime("runtime-1");
    assert!(removed.is_some());
    assert!(!state.take_oom_kill_mark("runtime-1"));
}
