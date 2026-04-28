//! User-space OOM policy: RSS / cgroup vs configured memory limit (C++ `MetricsActor` runtime OOM path).

use crate::agent::AgentClient;
use crate::runtime_ops;
use crate::state::RuntimeManagerState;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::sync::Arc;
use tokio::time::Duration;
use tracing::warn;
use yr_proto::internal::{StopInstanceRequest, UpdateInstanceStatusRequest};

pub const RUNTIME_MEMORY_EXCEED_LIMIT_MESSAGE: &str = "runtime memory exceed limit";

pub fn oom_kill_status_request(instance_id: &str, runtime_id: &str) -> UpdateInstanceStatusRequest {
    UpdateInstanceStatusRequest {
        instance_id: instance_id.to_string(),
        runtime_id: runtime_id.to_string(),
        status: "oom_killed".into(),
        exit_code: -1,
        error_message: RUNTIME_MEMORY_EXCEED_LIMIT_MESSAGE.into(),
    }
}

fn rss_kb(pid: i32) -> u64 {
    let path = format!("/proc/{pid}/status");
    let Ok(text) = std::fs::read_to_string(path) else {
        return 0;
    };
    for line in text.lines() {
        if line.starts_with("VmRSS:") {
            if let Some(kb) = line.split_whitespace().nth(1).and_then(|s| s.parse().ok()) {
                return kb;
            }
        }
    }
    0
}

fn usage_mb_for_process(pid: i32, cgroup: Option<&std::path::PathBuf>) -> u64 {
    if let Some(cg) = cgroup {
        if let Some((cur, _)) = super::cgroup::read_cgroup_memory(cg) {
            return (cur / (1024 * 1024)).max(1);
        }
    }
    rss_kb(pid) / 1024
}

/// When `oom_kill_enable`, periodically compares usage to `resources["memory"]` (GiB) plus `oom_kill_control_limit` (MiB slack).
pub async fn supervision_loop(state: Arc<RuntimeManagerState>, agent: Arc<AgentClient>) {
    let counts: Arc<Mutex<HashMap<String, u32>>> = Arc::new(Mutex::new(HashMap::new()));

    loop {
        let iv = Duration::from_millis(state.config.memory_detection_interval_ms.max(200));
        tokio::time::sleep(iv).await;

        if !state.config.oom_kill_enable {
            counts.lock().clear();
            continue;
        }

        let need = state.config.oom_consecutive_detection_count.max(1) as u32;
        let slack_mb = state.config.oom_kill_control_limit.max(0) as u64;

        let rids = state.list_runtime_ids();
        for rid in rids {
            let Some(proc) = state.get_by_runtime(&rid) else {
                continue;
            };
            let limit_gib = proc.resources.get("memory").copied().unwrap_or(0.0);
            if limit_gib <= 0.0 {
                counts.lock().remove(&proc.instance_id);
                continue;
            }
            let limit_mb = (limit_gib * 1024.0).ceil() as u64;
            let used_mb = usage_mb_for_process(proc.pid, proc.cgroup_path.as_ref());
            let threshold = limit_mb.saturating_add(slack_mb);

            if used_mb > threshold {
                let mut g = counts.lock();
                let n = g.entry(proc.instance_id.clone()).or_insert(0);
                *n += 1;
                if *n < need {
                    continue;
                }
                drop(g);

                warn!(
                    instance_id = %proc.instance_id,
                    runtime_id = %rid,
                    pid = proc.pid,
                    used_mb,
                    limit_mb,
                    slack_mb,
                    "runtime memory over limit; stopping instance"
                );

                state.mark_oom_kill_in_advance(&rid);
                let status_req = oom_kill_status_request(&proc.instance_id, &rid);
                let ag = agent.clone();
                tokio::spawn(async move {
                    ag.update_instance_status_retry(status_req).await;
                });

                let stop_req = StopInstanceRequest {
                    instance_id: proc.instance_id.clone(),
                    runtime_id: rid.clone(),
                    force: true,
                };
                if let Err(e) = runtime_ops::stop_instance_op(&state, stop_req) {
                    warn!(error = %e, "OOM stop_instance failed");
                    counts.lock().remove(&proc.instance_id);
                    continue;
                }

                counts.lock().remove(&proc.instance_id);
            } else {
                counts.lock().remove(&proc.instance_id);
            }
        }
    }
}
