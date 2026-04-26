//! Shared start/stop/status logic for gRPC and in-process (yr-agent merge) callers.

use crate::executor;
use crate::state::RuntimeManagerState;
use nix::sys::signal::{kill, Signal};
use nix::unistd::Pid;
use std::path::Path;
use std::sync::Arc;
use tonic::Status;
use tracing::{info, warn};
use yr_proto::internal::{
    RuntimeStatusRequest, RuntimeStatusResponse, SnapshotRequest, SnapshotResponse,
    StartInstanceRequest, StartInstanceResponse, StopInstanceRequest, StopInstanceResponse,
};

pub fn start_instance_op(
    state: &Arc<RuntimeManagerState>,
    paths: &[String],
    req: StartInstanceRequest,
) -> Result<StartInstanceResponse, Status> {
    if req.instance_id.trim().is_empty() {
        return Err(Status::invalid_argument("instance_id is required"));
    }
    if state.has_instance(&req.instance_id) {
        return Err(Status::already_exists(format!(
            "instance {} already running",
            req.instance_id
        )));
    }

    let ts = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or(0);
    let runtime_id = format!("rt-{}-{}", req.instance_id, ts);

    let port = state
        .ports
        .allocate(&runtime_id)
        .map_err(|e: anyhow::Error| Status::resource_exhausted(e.to_string()))?;

    match executor::start_runtime_process(&state.config, &req, paths, &runtime_id, port) {
        Ok(proc) => {
            state.insert_running(proc);
            Ok(StartInstanceResponse {
                success: true,
                message: String::new(),
                runtime_id,
                runtime_port: i32::from(port),
            })
        }
        Err(e) => {
            let _ = state.ports.release(&runtime_id);
            warn!(error = %e, "StartInstance spawn failed");
            Err(Status::internal(e.to_string()))
        }
    }
}

pub fn stop_instance_op(
    state: &Arc<RuntimeManagerState>,
    req: StopInstanceRequest,
) -> Result<StopInstanceResponse, Status> {
    let Some(proc) = state
        .get_by_runtime(&req.runtime_id)
        .or_else(|| state.get_by_instance(&req.instance_id))
    else {
        return Ok(StopInstanceResponse {
            success: false,
            message: "unknown runtime_id or instance_id".into(),
        });
    };
    let runtime_id = proc.runtime_id.clone();

    if let Err(e) = executor::stop_runtime_process(&state.config, &proc, req.force) {
        warn!(error = %e, pid = proc.pid, "StopInstance kill sequence");
    }

    state.remove_by_runtime(&runtime_id);
    state.ports.release(&runtime_id);

    info!(
        instance_id = %proc.instance_id,
        runtime_id = %runtime_id,
        pid = proc.pid,
        "StopInstance completed"
    );

    Ok(StopInstanceResponse {
        success: true,
        message: String::new(),
    })
}

pub fn snapshot_runtime_op(
    state: &Arc<RuntimeManagerState>,
    req: SnapshotRequest,
) -> Result<SnapshotResponse, Status> {
    let Some(proc) = state.get_by_runtime(&req.runtime_id) else {
        return Err(Status::not_found("unknown runtime_id"));
    };
    if !Path::new(&format!("/proc/{}", proc.pid)).exists() {
        return Err(Status::failed_precondition("runtime process not running"));
    }
    let pid = Pid::from_raw(proc.pid);
    if let Err(e) = kill(pid, Signal::SIGUSR2) {
        warn!(error = %e, "SIGUSR2 checkpoint trigger failed");
        return Ok(SnapshotResponse {
            success: false,
            snapshot_id: String::new(),
        });
    }
    let snapshot_id = format!(
        "snap-{}-{}-{}",
        req.runtime_id, req.instance_id, req.snap_type
    );
    Ok(SnapshotResponse {
        success: true,
        snapshot_id,
    })
}

/// Best-effort stop of every tracked runtime (shutdown / exit cleanup).
pub fn shutdown_all_runtimes(state: &Arc<RuntimeManagerState>, force: bool) {
    let ids = state.list_runtime_ids();
    for runtime_id in ids {
        let Some(proc) = state.get_by_runtime(&runtime_id) else {
            continue;
        };
        if let Err(e) = executor::stop_runtime_process(&state.config, &proc, force) {
            tracing::warn!(error = %e, %runtime_id, "shutdown_all stop_runtime_process");
        }
        state.remove_by_runtime(&runtime_id);
        state.ports.release(&runtime_id);
    }
}

pub fn get_runtime_status_op(
    state: &Arc<RuntimeManagerState>,
    req: RuntimeStatusRequest,
) -> Result<RuntimeStatusResponse, Status> {
    let Some(proc) = state.get_by_runtime(&req.runtime_id) else {
        return Ok(RuntimeStatusResponse {
            status: "unknown".into(),
            exit_code: -1,
        });
    };

    let alive = Path::new(&format!("/proc/{}", proc.pid)).exists();
    let (status, exit_code) = if alive {
        ("running".to_string(), 0)
    } else {
        (proc.status.clone(), proc.exit_code.unwrap_or(-1))
    };

    Ok(RuntimeStatusResponse { status, exit_code })
}
