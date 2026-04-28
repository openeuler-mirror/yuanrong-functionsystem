use crate::config::Config;
use crate::port_manager::SharedPortManager;
use std::collections::{HashMap, HashSet};
use std::time::{Duration, Instant};

/// Parsed from instance `config_json` for optional HTTP/TCP probes.
#[derive(Debug, Clone, Default)]
pub struct InstanceHealthSpec {
    pub http_url: Option<String>,
    pub tcp_host: Option<String>,
    pub tcp_port: Option<u16>,
    pub startup_deadline: Duration,
}
use parking_lot::RwLock;
use std::sync::Arc;

#[derive(Debug, Clone)]
pub struct RunningProcess {
    pub instance_id: String,
    pub runtime_id: String,
    pub pid: i32,
    pub port: u16,
    /// Best-effort status string for `GetRuntimeStatus`
    pub status: String,
    pub exit_code: Option<i32>,
    pub error_message: String,
    pub cgroup_path: Option<std::path::PathBuf>,
    pub bind_mount_points: Vec<std::path::PathBuf>,
    pub health_spec: InstanceHealthSpec,
    pub started_at: Instant,
    /// Resource limits from StartInstance (e.g. `memory` in MiB, `cpu` cores) for OOM / reporting.
    pub resources: HashMap<String, f64>,
}

pub struct RuntimeManagerState {
    pub config: Arc<Config>,
    pub ports: Arc<SharedPortManager>,
    by_runtime: RwLock<HashMap<String, RunningProcess>>,
    by_pid: RwLock<HashMap<i32, String>>,
    oom_kill_marks: RwLock<HashSet<String>>,
}

impl RuntimeManagerState {
    pub fn new(config: Arc<Config>, ports: Arc<SharedPortManager>) -> Self {
        Self {
            config,
            ports,
            by_runtime: RwLock::new(HashMap::new()),
            by_pid: RwLock::new(HashMap::new()),
            oom_kill_marks: RwLock::new(HashSet::new()),
        }
    }

    pub fn insert_running(&self, proc: RunningProcess) {
        let pid = proc.pid;
        let rid = proc.runtime_id.clone();
        self.by_pid.write().insert(pid, rid.clone());
        self.by_runtime.write().insert(rid, proc);
    }

    pub fn remove_by_runtime(&self, runtime_id: &str) -> Option<RunningProcess> {
        let p = self.by_runtime.write().remove(runtime_id)?;
        self.by_pid.write().remove(&p.pid);
        self.oom_kill_marks.write().remove(runtime_id);
        Some(p)
    }

    pub fn get_by_runtime(&self, runtime_id: &str) -> Option<RunningProcess> {
        self.by_runtime.read().get(runtime_id).cloned()
    }

    pub fn get_by_instance(&self, instance_id: &str) -> Option<RunningProcess> {
        self.by_runtime
            .read()
            .values()
            .find(|p| p.instance_id == instance_id)
            .cloned()
    }

    pub fn runtime_id_for_pid(&self, pid: i32) -> Option<String> {
        self.by_pid.read().get(&pid).cloned()
    }

    pub fn update_status(&self, runtime_id: &str, status: &str, exit_code: Option<i32>, err: &str) {
        let mut map = self.by_runtime.write();
        if let Some(p) = map.get_mut(runtime_id) {
            p.status = status.to_string();
            p.exit_code = exit_code;
            p.error_message = err.to_string();
        }
    }

    pub fn remove_pid_mapping(&self, pid: i32) {
        self.by_pid.write().remove(&pid);
    }

    pub fn mark_oom_kill_in_advance(&self, runtime_id: &str) {
        self.oom_kill_marks.write().insert(runtime_id.to_string());
    }

    pub fn take_oom_kill_mark(&self, runtime_id: &str) -> bool {
        self.oom_kill_marks.write().remove(runtime_id)
    }

    pub fn list_running_pids(&self) -> Vec<i32> {
        self.by_pid.read().keys().copied().collect()
    }

    pub fn has_instance(&self, instance_id: &str) -> bool {
        self.by_runtime
            .read()
            .values()
            .any(|p| p.instance_id == instance_id)
    }

    pub fn list_runtime_ids(&self) -> Vec<String> {
        self.by_runtime.read().keys().cloned().collect()
    }

    pub fn apply_health_status(&self, runtime_id: &str, label: &str) {
        let mut map = self.by_runtime.write();
        let Some(p) = map.get_mut(runtime_id) else {
            return;
        };
        match label {
            "healthy" | "starting" => {
                if p.status.starts_with("unhealthy") {
                    p.status = "running".into();
                }
            }
            "down" => {}
            _ if label.starts_with("unhealthy") => {
                p.status = label.to_string();
            }
            _ => {}
        }
    }
}
