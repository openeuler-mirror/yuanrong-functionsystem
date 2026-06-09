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

pub async fn start_instance_op(
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

    // CONTAINER backend: config_json carries a `"sandbox": true` block. Process-mode
    // requests (empty/no marker) fall through to the unchanged RUNTIME path below.
    if let Some(cfg) = crate::sandbox::parse_sandbox_config(&req.config_json) {
        return start_container_instance(state, &req, &cfg).await;
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

/// CONTAINER backend dispatch (C++ `EXECUTOR_TYPE::CONTAINER`). Decodes the sandbox
/// config carried in `config_json`, picks the start path, and drives the
/// `SandboxExecutor` (RuntimeLauncher gRPC over CONTAINER_EP).
async fn start_container_instance(
    state: &Arc<RuntimeManagerState>,
    req: &StartInstanceRequest,
    cfg: &crate::sandbox::SandboxConfig,
) -> Result<StartInstanceResponse, Status> {
    let Some(sandbox) = state.sandbox.as_ref() else {
        return Err(Status::failed_precondition(
            "CONTAINER backend unavailable (CONTAINER_EP not set)",
        ));
    };
    let ts = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or(0);
    let runtime_id = format!("rt-{}-{}", req.instance_id, ts);
    let trace_id = req
        .env_vars
        .get("YR_JOB_ID")
        .cloned()
        .unwrap_or_else(|| req.instance_id.clone());

    let mut extracted =
        crate::sandbox::extract_from_config(cfg, &runtime_id, &trace_id, req.resources.clone());

    // The container's userEnvs are the full instance env (C++ parity: the sandbox
    // FunctionRuntime carries the instance's env_vars as userEnvs). config_json only
    // carries container shape, so fold the request env in here without clobbering it.
    for (k, v) in &req.env_vars {
        extracted
            .params
            .user_envs
            .entry(k.clone())
            .or_insert_with(|| v.clone());
    }

    // Append the runtime CLI args after the services.yaml bootstrap entrypoint
    // (C++ `python_strategy::BuildArgs`): --rt_server_address <posix-addr>
    // --deploy_dir <dir> --runtime_id <id> --job_id <job> --log_level <level>.
    if !extracted.params.command.is_empty() {
        let addr = req
            .env_vars
            .get("YR_SERVER_ADDRESS")
            .or_else(|| req.env_vars.get("POSIX_LISTEN_ADDR"))
            .cloned()
            .unwrap_or_default();
        let deploy_dir = req
            .env_vars
            .get("YR_RUNTIME_DEPLOY_DIR")
            .or_else(|| req.env_vars.get("DEPLOY_DIR"))
            .cloned()
            .unwrap_or_else(|| "/openyuanrong/".to_string());
        let log_level = req
            .env_vars
            .get("RUNTIME_LOG_LEVEL")
            .cloned()
            .unwrap_or_else(|| "INFO".to_string());
        extracted.params.command.extend([
            "--rt_server_address".to_string(),
            addr,
            "--deploy_dir".to_string(),
            deploy_dir,
            "--runtime_id".to_string(),
            runtime_id.clone(),
            "--job_id".to_string(),
            trace_id.clone(),
            "--log_level".to_string(),
            log_level,
        ]);
    }

    use crate::sandbox::StartPath;
    let result = match extracted.path {
        StartPath::Normal => sandbox.start_normal(extracted.params, extracted.forwards).await,
        StartPath::Restore => {
            sandbox
                .start_by_snapshot(
                    extracted.params,
                    &extracted.checkpoint_id,
                    &extracted.storage_url,
                    extracted.forwards,
                )
                .await
        }
        StartPath::WarmUp => {
            // WarmUp registers a pre-warm entry (no container, no port); report success.
            let fr = yr_proto::runtime::v1::FunctionRuntime {
                id: runtime_id.clone(),
                command: extracted.params.command.clone(),
                runtime_envs: extracted.params.runtime_envs.clone(),
                cwd: extracted.params.cwd.clone(),
                rootfs: extracted.params.rootfs.as_ref().map(|r| r.to_config(false)),
                ..Default::default()
            };
            return match sandbox.start_warmup(fr).await {
                Ok(()) => Ok(StartInstanceResponse {
                    success: true,
                    message: String::new(),
                    runtime_id,
                    runtime_port: 0,
                }),
                Err(e) => Err(Status::internal(e.to_string())),
            };
        }
    };

    match result {
        Ok(started) => {
            // First host port from "proto:host:container" mappings (0 if none).
            let runtime_port = first_host_port(&started.port_mappings_json);
            info!(
                instance_id = %req.instance_id, runtime_id = %runtime_id,
                sandbox_id = %started.sandbox_id, "CONTAINER StartInstance ok"
            );
            Ok(StartInstanceResponse {
                success: true,
                message: String::new(),
                runtime_id,
                runtime_port,
            })
        }
        Err(e) => {
            warn!(instance_id = %req.instance_id, error = %e, "CONTAINER StartInstance failed");
            Err(Status::internal(e.to_string()))
        }
    }
}

/// Extract the first host port from a JSON array of "proto:host:container" mappings.
fn first_host_port(port_mappings_json: &str) -> i32 {
    serde_json::from_str::<Vec<String>>(port_mappings_json)
        .ok()
        .and_then(|v| v.into_iter().next())
        .and_then(|m| m.split(':').nth(1).and_then(|p| p.parse::<i32>().ok()))
        .unwrap_or(0)
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
