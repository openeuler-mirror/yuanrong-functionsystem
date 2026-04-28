use crate::agent::AgentClient;
use crate::state::RuntimeManagerState;
use nix::errno::Errno;
use nix::sys::signal::Signal;
use nix::sys::wait::{waitpid, WaitPidFlag, WaitStatus};
use nix::unistd::Pid;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::mpsc;
use tracing::{debug, info};
use yr_proto::internal::UpdateInstanceStatusRequest;

#[derive(Debug)]
pub struct ChildExitEvent {
    pub pid: i32,
    pub status: String,
    pub exit_code: i32,
    pub error_message: String,
}

const STD_POSTFIX: &str = "-user_func_std.log";
const STD_ERROR_LEVEL: &str = "ERROR";
const STD_TARGET_LINE_COUNT: usize = 20;
const STD_READ_LINE_COUNT: usize = 1000;

/// Best-effort: look for OOM killer mention of `pid` in recent `dmesg` output.
fn dmesg_suggests_oom(pid: i32) -> bool {
    let out = std::process::Command::new("dmesg")
        .arg("-T")
        .output()
        .ok()
        .filter(|o| o.status.success())
        .map(|o| String::from_utf8_lossy(&o.stdout).to_string())
        .unwrap_or_default();
    let needle = format!("Killed process {pid}");
    let needle2 = format!("pid {pid} ");
    out.contains(&needle) || out.contains(&needle2)
}

/// C++ `HealthCheckActor::GetRuntimeException` equivalent for Rust-managed paths.
///
/// Priority follows C++ for the safe local artifacts Rust can read:
/// exception `BackTrace_<runtimeID>.log` first, then standard ERROR logs.
pub fn runtime_exit_log_message(
    runtime_id: &str,
    instance_id: &str,
    exit_code: i32,
    runtime_logs_dir: &Path,
    runtime_std_log_dir: &str,
    std_log_name: &str,
    raw_log_dir: &Path,
) -> Option<String> {
    let exception_path = runtime_logs_dir
        .join("exception")
        .join(format!("BackTrace_{runtime_id}.log"));
    if let Some(content) = read_non_empty(&exception_path) {
        return Some(content);
    }

    std_error_message(
        runtime_id,
        instance_id,
        exit_code,
        runtime_logs_dir,
        runtime_std_log_dir,
        std_log_name,
        raw_log_dir,
    )
}

fn std_error_message(
    runtime_id: &str,
    instance_id: &str,
    exit_code: i32,
    runtime_logs_dir: &Path,
    runtime_std_log_dir: &str,
    std_log_name: &str,
    raw_log_dir: &Path,
) -> Option<String> {
    let runtime_std_dir = join_optional(runtime_logs_dir, runtime_std_log_dir);
    let mut candidates = Vec::new();
    if !std_log_name.trim().is_empty() {
        candidates.push(runtime_std_dir.join(format!("{std_log_name}{STD_POSTFIX}")));
    }
    candidates.push(runtime_std_dir.join(format!("{runtime_id}{STD_POSTFIX}")));
    candidates.push(runtime_std_dir.join(format!("{runtime_id}.out")));

    for path in candidates {
        let Some(content) = read_non_empty(&path) else {
            continue;
        };
        if let Some(lines) = select_std_error_lines(&content, runtime_id) {
            return Some(format_std_message(
                instance_id,
                runtime_id,
                exit_code,
                &lines,
            ));
        }
    }
    for path in [
        raw_log_dir.join(format!("{runtime_id}.stderr.log")),
        raw_log_dir.join(format!("{runtime_id}.stdout.log")),
    ] {
        let Some(content) = read_non_empty(&path) else {
            continue;
        };
        if let Some(lines) = select_raw_log_lines(&content) {
            return Some(format_std_message(
                instance_id,
                runtime_id,
                exit_code,
                &lines,
            ));
        }
    }
    None
}

fn join_optional(root: &Path, child: &str) -> PathBuf {
    let child = child.trim().trim_matches('/');
    if child.is_empty() {
        root.to_path_buf()
    } else {
        root.join(child)
    }
}

fn read_non_empty(path: &Path) -> Option<String> {
    let content = fs::read_to_string(path).ok()?;
    (!content.is_empty()).then_some(content)
}

fn select_std_error_lines(content: &str, runtime_id: &str) -> Option<String> {
    let mut lines: Vec<&str> = content
        .lines()
        .rev()
        .take(STD_READ_LINE_COUNT)
        .filter(|line| line.contains(runtime_id) && line.contains(STD_ERROR_LEVEL))
        .take(STD_TARGET_LINE_COUNT)
        .collect();
    if lines.is_empty() {
        return None;
    }
    lines.reverse();
    Some(format!("{}\n", lines.join("\n")))
}

fn select_raw_log_lines(content: &str) -> Option<String> {
    let mut lines: Vec<&str> = content
        .lines()
        .rev()
        .take(STD_READ_LINE_COUNT)
        .filter(|line| !line.trim().is_empty())
        .take(STD_TARGET_LINE_COUNT)
        .collect();
    if lines.is_empty() {
        return None;
    }
    lines.reverse();
    Some(format!("{}\n", lines.join("\n")))
}

fn format_std_message(
    instance_id: &str,
    runtime_id: &str,
    exit_code: i32,
    std_error_lines: &str,
) -> String {
    format!(
        "instance({instance_id}) runtime({runtime_id}) exit code({exit_code}) with exitState({}) exitStatus({exit_code})\n{std_error_lines}",
        i32::from(exit_code >= 0)
    )
}

pub fn classify_wait_status(status: WaitStatus) -> Option<ChildExitEvent> {
    match status {
        WaitStatus::StillAlive => None,
        WaitStatus::Exited(p, code) => {
            let (status, msg) = if code == 0 {
                ("returned", "runtime had been returned".to_string())
            } else {
                (
                    "failed",
                    format!("an unknown error caused the instance exited. exit code:{code}"),
                )
            };
            Some(ChildExitEvent {
                pid: p.as_raw(),
                status: status.to_string(),
                exit_code: code,
                error_message: msg,
            })
        }
        WaitStatus::Signaled(p, sig, core) => {
            let mut msg = format!("terminated by signal {sig:?}");
            if core {
                msg.push_str(" (core dumped)");
            }
            let code = -(sig as i32);
            if sig == Signal::SIGKILL && dmesg_suggests_oom(p.as_raw()) {
                msg.push_str("; likely OOM killer (dmesg)");
            } else if sig == Signal::SIGKILL {
                msg.push_str("; possible OOM or external SIGKILL");
            }
            Some(ChildExitEvent {
                pid: p.as_raw(),
                status: "failed".into(),
                exit_code: code,
                error_message: msg,
            })
        }
        _ => None,
    }
}

/// Blocking thread: `waitpid(-1, WNOHANG)` loop and forward exits to async code.
pub fn spawn_child_reaper(tx: mpsc::Sender<ChildExitEvent>) -> std::thread::JoinHandle<()> {
    std::thread::spawn(move || loop {
        std::thread::sleep(Duration::from_millis(250));
        loop {
            let w = match waitpid::<Option<Pid>>(None, Some(WaitPidFlag::WNOHANG)) {
                Ok(s) => s,
                Err(Errno::ECHILD) => break,
                Err(e) => {
                    debug!(error = %e, "waitpid");
                    break;
                }
            };
            if matches!(w, WaitStatus::StillAlive) {
                break;
            }
            let Some(ev) = classify_wait_status(w) else {
                continue;
            };
            let _ = tx.blocking_send(ev);
        }
    })
}

pub async fn handle_child_exits(
    mut rx: mpsc::Receiver<ChildExitEvent>,
    state: Arc<RuntimeManagerState>,
    agent: Arc<AgentClient>,
) {
    while let Some(ev) = rx.recv().await {
        let Some(rid) = state.runtime_id_for_pid(ev.pid) else {
            debug!(pid = ev.pid, "reaped unknown child (not tracked)");
            continue;
        };
        if state.take_oom_kill_mark(&rid) {
            debug!(
                runtime_id = %rid,
                pid = ev.pid,
                "reaped runtime after advance OOM status; suppress duplicate child-exit report"
            );
            state.remove_by_runtime(&rid);
            state.ports.release(&rid);
            state.remove_pid_mapping(ev.pid);
            continue;
        }
        let proc = match state.get_by_runtime(&rid) {
            Some(p) => p,
            None => continue,
        };
        let mut ev = ev;
        if ev.exit_code != 0 {
            if let Some(msg) = runtime_exit_log_message(
                &rid,
                &proc.instance_id,
                ev.exit_code,
                Path::new(&state.config.runtime_logs_dir),
                &state.config.runtime_std_log_dir,
                &state.config.node_id,
                &state.config.log_path,
            ) {
                ev.error_message = msg;
            }
        }

        let status = ev.status.as_str();
        state.update_status(&rid, status, Some(ev.exit_code), &ev.error_message);
        state.remove_by_runtime(&rid);
        state.ports.release(&rid);
        state.remove_pid_mapping(ev.pid);

        info!(
            instance_id = %proc.instance_id,
            runtime_id = %rid,
            pid = ev.pid,
            exit_code = ev.exit_code,
            "runtime process exited"
        );

        let req = UpdateInstanceStatusRequest {
            instance_id: proc.instance_id.clone(),
            runtime_id: rid.clone(),
            status: status.into(),
            exit_code: ev.exit_code,
            error_message: ev.error_message.clone(),
        };
        let ag = agent.clone();
        tokio::spawn(async move {
            ag.update_instance_status_retry(req).await;
        });
    }
}
