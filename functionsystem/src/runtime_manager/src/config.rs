use anyhow::{bail, Context};
use clap::builder::BoolishValueParser;
use clap::Parser;
use std::path::PathBuf;

use crate::volume::BindMount;

fn cgroup_parent_from_flag(s: &str) -> Result<PathBuf, String> {
    Ok(PathBuf::from(s))
}

/// CLI and runtime configuration for `yr-runtime-manager`.
#[derive(Parser, Debug, Clone)]
#[command(
    name = "yr-runtime-manager",
    about = "openYuanrong runtime manager — spawns and supervises runtime processes on a worker node"
)]
pub struct Config {
    /// gRPC listen host
    #[arg(long, default_value = "0.0.0.0")]
    pub host: String,

    /// gRPC listen port
    #[arg(long, default_value = "8404")]
    pub port: u16,

    /// HTTP listen host (health checks)
    #[arg(long)]
    pub http_host: Option<String>,

    /// HTTP listen port (defaults to gRPC port + 1)
    #[arg(long)]
    pub http_port: Option<u16>,

    /// Logical node id (included in resource reports)
    #[arg(long, default_value = "node-0")]
    pub node_id: String,

    /// Function agent gRPC address, e.g. `127.0.0.1:8403` or `http://127.0.0.1:8403`
    #[arg(long, default_value = "http://127.0.0.1:8403")]
    pub agent_address: String,

    /// First TCP port handed out to runtimes
    #[arg(long, default_value_t = 9000)]
    pub runtime_initial_port: u16,

    /// Size of the runtime port pool
    #[arg(long, default_value_t = 1000)]
    pub port_count: u32,

    /// Comma-separated paths to runtime executables (indexed by `runtime_type` or matched as substring)
    #[arg(long, default_value = "/bin/sleep")]
    pub runtime_paths: String,

    /// Directory for per-runtime stdout/stderr logs
    #[arg(long, default_value = "/tmp/yr-runtime-logs")]
    pub log_path: PathBuf,

    /// How often to sample `/proc` and push metrics to the agent (milliseconds)
    #[arg(long, default_value_t = 5000)]
    pub metrics_interval_ms: u64,

    /// C++ `metrics_collector_type`; `proc` uses configured capacity, `node` reads host capacity.
    #[arg(
        long = "metrics_collector_type",
        default_value = "proc",
        alias = "metrics-collector-type"
    )]
    pub metrics_collector_type: String,

    /// C++ default logical CPU capacity for `proc` metrics mode.
    #[arg(
        long = "proc_metrics_cpu",
        default_value_t = 1000.0,
        alias = "proc-metrics-cpu"
    )]
    pub proc_metrics_cpu: f64,

    /// C++ default logical memory capacity (MB) for `proc` metrics mode.
    #[arg(
        long = "proc_metrics_memory",
        default_value_t = 4000.0,
        alias = "proc-metrics-memory"
    )]
    pub proc_metrics_memory: f64,

    /// CPU capacity reserved from node metrics mode.
    #[arg(long = "overhead_cpu", default_value_t = 0.0, alias = "overhead-cpu")]
    pub overhead_cpu: f64,

    /// Memory capacity reserved from node metrics mode (MB).
    #[arg(
        long = "overhead_memory",
        default_value_t = 0.0,
        alias = "overhead-memory"
    )]
    pub overhead_memory: f64,

    /// cgroup v2 parent directory (e.g. `/sys/fs/cgroup/yr_runtime_manager`). Empty disables cgroups.
    #[arg(long, default_value = "", value_parser = cgroup_parent_from_flag)]
    pub cgroup_parent: PathBuf,

    #[arg(
        long,
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = true,
        value_parser = BoolishValueParser::new()
    )]
    pub cgroup_enable_cpu: bool,

    #[arg(
        long,
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = true,
        value_parser = BoolishValueParser::new()
    )]
    pub cgroup_enable_memory: bool,

    /// `unshare(2)` NEWIPC/NEWUTS/NEWNS before exec (requires privileges; default off).
    #[arg(
        long,
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new()
    )]
    pub isolate_namespaces: bool,

    /// Comma-separated bind mounts: `src:dst` or `src:dst:ro` applied under each runtime workdir.
    #[arg(long, default_value = "")]
    pub extra_bind_mounts: String,

    #[arg(long, default_value_t = 64 * 1024 * 1024)]
    pub log_rotate_max_bytes: u64,

    #[arg(long, default_value_t = 3)]
    pub log_rotate_keep: u32,

    /// Interval for deep instance health probes (HTTP/TCP from `config_json`, milliseconds).
    #[arg(long, default_value_t = 5000)]
    pub instance_health_interval_ms: u64,

    /// HTTP GET URL for manager liveness (empty = skip).
    #[arg(long, default_value = "")]
    pub manager_health_http_url: String,

    /// TCP `host:port` for manager liveness (empty = skip).
    #[arg(long, default_value = "")]
    pub manager_health_tcp: String,

    /// Startup grace before failing manager HTTP/TCP probes (seconds).
    #[arg(long, default_value_t = 30)]
    pub manager_startup_probe_secs: u64,

    // --- C++ runtime-manager flag parity ---
    /// Visible IP for runtime to connect back (`host_ip`).
    #[arg(long, default_value = "")]
    pub host_ip: String,

    #[arg(long, default_value = "31501")]
    pub data_system_port: String,

    #[arg(long, default_value = "22773")]
    pub driver_server_port: String,

    #[arg(long, default_value = "22773")]
    pub proxy_grpc_server_port: String,

    #[arg(long, default_value = "")]
    pub proxy_ip: String,

    #[arg(long, default_value_t = 0)]
    pub runtime_uid: i32,

    #[arg(long, default_value_t = 0)]
    pub runtime_gid: i32,

    #[arg(long, default_value = "/home/snuser", alias = "runtime_dir")]
    pub runtime_dir: String,

    #[arg(long, default_value = "/home/snuser/lib", alias = "snuser_lib_dir")]
    pub snuser_lib_dir: String,

    #[arg(
        long = "runtime_logs_dir",
        default_value = "/home/snuser",
        alias = "runtime-logs-dir"
    )]
    pub runtime_logs_dir: String,

    #[arg(
        long = "runtime_std_log_dir",
        default_value = "",
        alias = "runtime-std-log-dir"
    )]
    pub runtime_std_log_dir: String,

    #[arg(
        long = "runtime_home_dir",
        default_value = "/home/snuser",
        alias = "runtime-home-dir"
    )]
    pub runtime_home_dir: String,

    #[arg(
        long = "runtime_config_dir",
        default_value = "/home/snuser/config",
        alias = "runtime-config-dir"
    )]
    pub runtime_config_dir: String,

    #[arg(
        long = "python_log_config_path",
        default_value = "/home/snuser/config/python-runtime-log.json",
        alias = "python-log-config-path"
    )]
    pub python_log_config_path: String,

    #[arg(
        long = "java_system_property",
        default_value = "-Dlog4j2.configurationFile=file:/home/snuser/runtime/java/log4j2.xml",
        alias = "java-system-property"
    )]
    pub java_system_property: String,

    #[arg(
        long = "java_system_library_path",
        default_value = "/home/snuser/runtime/java/lib",
        alias = "java-system-library-path"
    )]
    pub java_system_library_path: String,

    #[arg(long, default_value = "", alias = "runtime_ld_library_path")]
    pub runtime_ld_library_path: String,

    #[arg(long, default_value = "DEBUG", alias = "runtime_log_level")]
    pub runtime_log_level: String,

    /// Maximum runtime log size (MB).
    #[arg(long, default_value_t = 40)]
    pub runtime_max_log_size: i32,

    #[arg(long, default_value_t = 20)]
    pub runtime_max_log_file_num: i32,

    #[arg(long, default_value = "/")]
    pub python_dependency_path: String,

    #[arg(
        long,
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new()
    )]
    pub oom_kill_enable: bool,

    /// OOM kill control limit (MB).
    #[arg(long, default_value_t = 0)]
    pub oom_kill_control_limit: i32,

    #[arg(long, default_value_t = 3)]
    pub oom_consecutive_detection_count: i32,

    /// Interval for cgroup/RSS memory sampling when `oom_kill_enable` (milliseconds).
    #[arg(long, default_value_t = 2000)]
    pub memory_detection_interval_ms: u64,

    /// Applied to each runtime child via `/proc/<pid>/oom_score_adj` (Linux; 0 = kernel default).
    #[arg(long, default_value_t = 0)]
    pub runtime_child_oom_score_adj: i32,

    #[arg(
        long,
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new()
    )]
    pub disk_usage_monitor_notify_failure_enable: bool,

    #[arg(long, default_value = "/tmp")]
    pub disk_usage_monitor_path: String,

    /// Disk usage limit (MB); `-1` means unlimited.
    #[arg(long, default_value_t = -1)]
    pub disk_usage_limit: i32,

    #[arg(long, default_value = "")]
    pub custom_resources: String,

    #[arg(
        long,
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new()
    )]
    pub enable_inherit_env: bool,

    /// Maps from C++ `setCmdCred`.
    #[arg(
        long = "setCmdCred",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        aliases = ["set-cmd-cred"]
    )]
    pub set_cmd_cred: bool,

    #[arg(
        long = "runtime_ds_connect_timeout",
        default_value_t = 60,
        alias = "runtime-ds-connect-timeout"
    )]
    pub runtime_ds_connect_timeout: u32,

    #[arg(long, default_value_t = 0, alias = "kill_process_timeout_seconds")]
    pub kill_process_timeout_seconds: u32,
}

impl Config {
    /// Minimal config when runtime_manager runs inside yr-agent (no separate RM gRPC/HTTP).
    pub fn embedded_in_agent(
        node_id: String,
        agent_grpc_uri: String,
        runtime_paths: String,
        runtime_initial_port: u16,
        port_count: u32,
        log_path: PathBuf,
        extra_bind_mounts: String,
    ) -> Self {
        Self {
            host: "127.0.0.1".into(),
            port: 0,
            http_host: None,
            http_port: None,
            node_id,
            agent_address: agent_grpc_uri,
            runtime_initial_port,
            port_count,
            runtime_paths,
            log_path,
            metrics_interval_ms: 5000,
            metrics_collector_type: "proc".into(),
            proc_metrics_cpu: 1000.0,
            proc_metrics_memory: 4000.0,
            overhead_cpu: 0.0,
            overhead_memory: 0.0,
            cgroup_parent: PathBuf::new(),
            cgroup_enable_cpu: true,
            cgroup_enable_memory: true,
            isolate_namespaces: false,
            extra_bind_mounts,
            log_rotate_max_bytes: 64 * 1024 * 1024,
            log_rotate_keep: 3,
            instance_health_interval_ms: 5000,
            manager_health_http_url: String::new(),
            manager_health_tcp: String::new(),
            manager_startup_probe_secs: 30,
            host_ip: String::new(),
            data_system_port: "31501".into(),
            driver_server_port: "22773".into(),
            proxy_grpc_server_port: "22773".into(),
            proxy_ip: String::new(),
            runtime_uid: 0,
            runtime_gid: 0,
            runtime_dir: "/home/snuser".into(),
            snuser_lib_dir: "/home/snuser/lib".into(),
            runtime_logs_dir: "/home/snuser".into(),
            runtime_std_log_dir: String::new(),
            runtime_home_dir: "/home/snuser".into(),
            runtime_config_dir: "/home/snuser/config".into(),
            python_log_config_path: "/home/snuser/config/python-runtime-log.json".into(),
            java_system_property:
                "-Dlog4j2.configurationFile=file:/home/snuser/runtime/java/log4j2.xml".into(),
            java_system_library_path: "/home/snuser/runtime/java/lib".into(),
            runtime_ld_library_path: String::new(),
            runtime_log_level: "DEBUG".into(),
            runtime_max_log_size: 40,
            runtime_max_log_file_num: 20,
            python_dependency_path: "/".into(),
            oom_kill_enable: false,
            oom_kill_control_limit: 0,
            oom_consecutive_detection_count: 3,
            memory_detection_interval_ms: 2000,
            runtime_child_oom_score_adj: 0,
            disk_usage_monitor_notify_failure_enable: false,
            disk_usage_monitor_path: "/tmp".into(),
            disk_usage_limit: -1,
            custom_resources: String::new(),
            enable_inherit_env: false,
            set_cmd_cred: false,
            runtime_ds_connect_timeout: 60,
            kill_process_timeout_seconds: 0,
        }
    }

    pub fn grpc_listen_addr(&self) -> String {
        format!("{}:{}", self.host.trim(), self.port)
    }

    pub fn http_listen_addr(&self) -> String {
        let h = self
            .http_host
            .as_deref()
            .unwrap_or_else(|| self.host.as_str())
            .trim();
        let p = self.http_port.unwrap_or(self.port.saturating_add(1));
        format!("{h}:{p}")
    }

    pub fn runtime_path_list(&self) -> Vec<String> {
        self.runtime_paths
            .split(',')
            .map(|s| s.trim().to_string())
            .filter(|s| !s.is_empty())
            .collect()
    }

    pub fn normalize_grpc_uri(addr: &str) -> anyhow::Result<String> {
        let a = addr.trim();
        if a.is_empty() {
            bail!("empty gRPC address");
        }
        if a.starts_with("http://") || a.starts_with("https://") {
            Ok(a.to_string())
        } else {
            Ok(format!("http://{a}"))
        }
    }

    pub fn agent_uri(&self) -> anyhow::Result<String> {
        Self::normalize_grpc_uri(&self.agent_address)
    }

    pub fn ensure_log_dir(&self) -> anyhow::Result<()> {
        std::fs::create_dir_all(&self.log_path)
            .with_context(|| format!("create log directory {}", self.log_path.display()))
    }

    pub fn parse_bind_mounts(&self) -> Vec<BindMount> {
        let mut out = Vec::new();
        for part in self.extra_bind_mounts.split(',') {
            let part = part.trim();
            if part.is_empty() {
                continue;
            }
            let bits: Vec<&str> = part.split(':').collect();
            if bits.len() < 2 {
                continue;
            }
            let ro = bits.last() == Some(&"ro");
            let (src, dst) = if ro && bits.len() >= 3 {
                (bits[0], bits[1])
            } else {
                (bits[0], bits[1])
            };
            if src.is_empty() || dst.is_empty() {
                continue;
            }
            out.push(BindMount {
                src: PathBuf::from(src),
                dst: PathBuf::from(dst),
                read_only: ro,
            });
        }
        out
    }
}
