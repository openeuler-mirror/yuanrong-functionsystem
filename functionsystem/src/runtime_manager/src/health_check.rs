use crate::agent::AgentClient;
use crate::state::RuntimeManagerState;
use nix::errno::Errno;
use nix::sys::signal::Signal;
use nix::sys::wait::{waitpid, WaitPidFlag, WaitStatus};
use nix::unistd::Pid;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::mpsc;
use tracing::{debug, info};
use yr_proto::internal::UpdateInstanceStatusRequest;

#[derive(Debug)]
pub struct ChildExitEvent {
    pub pid: i32,
    pub exit_code: i32,
    pub error_message: String,
}

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

fn describe_exit(status: WaitStatus) -> Option<(i32, i32, String)> {
    match status {
        WaitStatus::StillAlive => None,
        WaitStatus::Exited(p, code) => {
            let msg = if code == 0 {
                "exited normally".into()
            } else {
                format!("exited with status {code}")
            };
            Some((p.as_raw(), code, msg))
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
            Some((p.as_raw(), code, msg))
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
            let Some((pid, exit_code, err)) = describe_exit(w) else {
                continue;
            };
            let _ = tx.blocking_send(ChildExitEvent {
                pid,
                exit_code,
                error_message: err,
            });
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
        let proc = match state.get_by_runtime(&rid) {
            Some(p) => p,
            None => continue,
        };
        let status = if ev.exit_code == 0 {
            "exited"
        } else {
            "failed"
        };
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
            status: status.to_string(),
            exit_code: ev.exit_code,
            error_message: ev.error_message.clone(),
        };
        let ag = agent.clone();
        tokio::spawn(async move {
            ag.update_instance_status_retry(req).await;
        });
    }
}
