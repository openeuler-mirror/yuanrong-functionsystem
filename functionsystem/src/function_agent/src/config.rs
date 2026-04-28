use clap::builder::BoolishValueParser;
use clap::Parser;
use std::path::PathBuf;

/// C++ `function_agent` flags without Rust behavior yet (accepted for `install.sh`).
#[derive(Parser, Debug, Clone)]
#[allow(dead_code)]
pub struct AgentCppIgnored {
    #[arg(long = "agent_uid", default_value = "")]
    pub agent_uid: String,
    #[arg(long = "alias", default_value = "")]
    pub alias: String,
    #[arg(long = "log_config", default_value = "")]
    pub log_config: String,
    #[arg(long = "litebus_thread_num", default_value = "")]
    pub litebus_thread_num: String,
    #[arg(long = "runtime_dir", default_value = "")]
    pub runtime_dir: String,
    #[arg(long = "runtime_home_dir", default_value = "")]
    pub runtime_home_dir: String,
    #[arg(long = "snuser_lib_dir", default_value = "")]
    pub snuser_lib_dir: String,
    #[arg(long = "runtime_std_log_dir", default_value = "")]
    pub runtime_std_log_dir: String,
    #[arg(long = "runtime_config_dir", default_value = "")]
    pub runtime_config_dir: String,
    #[arg(long = "enable_separated_redirect_runtime_std", default_value = "")]
    pub enable_separated_redirect_runtime_std: String,
    #[arg(long = "user_log_export_mode", default_value = "")]
    pub user_log_export_mode: String,
    #[arg(long = "npu_collection_mode", default_value = "")]
    pub npu_collection_mode: String,
    #[arg(long = "gpu_collection_enable", default_value = "")]
    pub gpu_collection_enable: String,
    #[arg(long = "proxy_ip", default_value = "")]
    pub proxy_ip: String,
    #[arg(long = "proxy_grpc_server_port", default_value = "")]
    pub proxy_grpc_server_port: String,
    #[arg(long = "driver_server_port", default_value = "")]
    pub driver_server_port: String,
    #[arg(long = "setCmdCred", default_value = "")]
    pub set_cmd_cred: String,
    #[arg(long = "python_dependency_path", default_value = "")]
    pub python_dependency_path: String,
    #[arg(long = "python_log_config_path", default_value = "")]
    pub python_log_config_path: String,
    #[arg(long = "java_system_property", default_value = "")]
    pub java_system_property: String,
    #[arg(long = "java_system_library_path", default_value = "")]
    pub java_system_library_path: String,
    #[arg(long = "host_ip", default_value = "")]
    pub host_ip: String,
    #[arg(long = "enable_metrics", default_value = "")]
    pub enable_metrics: String,
    #[arg(long = "metrics_config", default_value = "")]
    pub metrics_config: String,
    #[arg(long = "metrics_config_file", default_value = "")]
    pub metrics_config_file: String,
    #[arg(long = "system_timeout", default_value = "")]
    pub system_timeout: String,
    #[arg(long = "metrics_collector_type", default_value = "")]
    pub metrics_collector_type: String,
    #[arg(long = "proc_metrics_cpu", default_value = "")]
    pub proc_metrics_cpu: String,
    #[arg(long = "is_protomsg_to_runtime", default_value = "")]
    pub is_protomsg_to_runtime: String,
    #[arg(long = "massif_enable", default_value = "")]
    pub massif_enable: String,
    #[arg(long = "memory_detection_interval", default_value = "")]
    pub memory_detection_interval: String,
    #[arg(long = "runtime_ds_connect_timeout", default_value = "")]
    pub runtime_ds_connect_timeout: String,
    #[arg(long = "runtime_direct_connection_enable", default_value = "")]
    pub runtime_direct_connection_enable: String,
    #[arg(long = "ssl_enable", default_value = "")]
    pub ssl_enable: String,
    #[arg(long = "ssl_base_path", default_value = "")]
    pub ssl_base_path: String,
    #[arg(long = "ssl_root_file", default_value = "")]
    pub ssl_root_file: String,
    #[arg(long = "ssl_cert_file", default_value = "")]
    pub ssl_cert_file: String,
    #[arg(long = "ssl_key_file", default_value = "")]
    pub ssl_key_file: String,
    #[arg(long = "metrics_ssl_enable", default_value = "")]
    pub metrics_ssl_enable: String,
    #[arg(long = "etcd_auth_type", default_value = "")]
    pub etcd_auth_type: String,
    #[arg(long = "etcd_root_ca_file", default_value = "")]
    pub etcd_root_ca_file: String,
    #[arg(long = "etcd_cert_file", default_value = "")]
    pub etcd_cert_file: String,
    #[arg(long = "etcd_key_file", default_value = "")]
    pub etcd_key_file: String,
    #[arg(long = "etcd_ssl_base_path", default_value = "")]
    pub etcd_ssl_base_path: String,
    #[arg(long = "runtime_default_config", default_value = "")]
    pub runtime_default_config: String,
    #[arg(long = "proc_metrics_memory", default_value = "")]
    pub proc_metrics_memory: String,
    #[arg(long = "enable_dis_conv_call_stack", default_value = "")]
    pub enable_dis_conv_call_stack: String,
    #[arg(long = "data_system_enable", default_value = "")]
    pub data_system_enable: String,
    #[arg(long = "runtime_instance_debug_enable", default_value = "")]
    pub runtime_instance_debug_enable: String,
    #[arg(long = "log_expiration_enable", default_value = "")]
    pub log_expiration_enable: String,
    #[arg(long = "log_expiration_time_threshold", default_value = "")]
    pub log_expiration_time_threshold: String,
    #[arg(long = "log_expiration_cleanup_interval", default_value = "")]
    pub log_expiration_cleanup_interval: String,
    #[arg(long = "log_expiration_max_file_count", default_value = "")]
    pub log_expiration_max_file_count: String,
    #[arg(long = "user_log_auto_flush_interval_ms", default_value = "")]
    pub user_log_auto_flush_interval_ms: String,
    #[arg(long = "user_log_buffer_flush_threshold", default_value = "")]
    pub user_log_buffer_flush_threshold: String,
    #[arg(long = "user_log_rolling_size_limit_mb", default_value = "")]
    pub user_log_rolling_size_limit_mb: String,
    #[arg(long = "user_log_rolling_file_count_limit", default_value = "")]
    pub user_log_rolling_file_count_limit: String,
    #[arg(long = "npu_collection_enable", default_value = "")]
    pub npu_collection_enable: String,
    #[arg(long = "numa_collection_enable", default_value = "")]
    pub numa_collection_enable: String,
    #[arg(long = "runtime_ld_library_path", default_value = "")]
    pub runtime_ld_library_path: String,
    #[arg(long = "runtime_log_level", default_value = "")]
    pub runtime_log_level: String,
    #[arg(long = "runtime_uid", default_value = "")]
    pub runtime_uid: String,
    #[arg(long = "runtime_gid", default_value = "")]
    pub runtime_gid: String,
    #[arg(long = "runtime_max_log_size", default_value = "")]
    pub runtime_max_log_size: String,
    #[arg(long = "runtime_max_log_file_num", default_value = "")]
    pub runtime_max_log_file_num: String,
    #[arg(long = "local_node_id", default_value = "")]
    pub local_node_id: String,
}

#[derive(Parser, Debug, Clone)]
#[command(name = "function_agent", about = "openYuanrong function agent (Rust)")]
pub struct Config {
    #[arg(
        long = "ip",
        default_value = "0.0.0.0",
        aliases = ["host", "host-ip"]
    )]
    pub host: String,

    /// HTTP probe port (C++ `--port` in agent+RM combined start).
    #[arg(long = "port", default_value_t = 18403)]
    pub port: u16,

    #[arg(long = "node_id", default_value = "", aliases = ["node-id"])]
    pub node_id: String,

    #[arg(
        long = "local_scheduler_address",
        default_value = "http://127.0.0.1:8402",
        aliases = ["local-scheduler-address"]
    )]
    pub local_scheduler_address: String,

    #[arg(
        long = "agent_listen_port",
        default_value_t = 22799,
        aliases = ["agent-listen-port"]
    )]
    pub agent_listen_port: u16,

    #[arg(long = "s3_endpoint", default_value = "", aliases = ["s3-endpoint"])]
    pub s3_endpoint: String,

    #[arg(long = "s3_bucket", default_value = "", aliases = ["s3-bucket"])]
    pub s3_bucket: String,

    #[arg(
        long = "code_package_dir",
        default_value = "/tmp/yr-agent-code",
        aliases = ["code-package-dir"]
    )]
    pub code_package_dir: String,

    #[arg(
        long = "runtime_manager_address",
        default_value = "",
        aliases = ["runtime-manager-address"]
    )]
    pub runtime_manager_address: String,

    #[arg(
        long = "data_system_host",
        default_value = "127.0.0.1",
        aliases = ["data-system-host"]
    )]
    pub data_system_host: String,

    #[arg(
        long = "data_system_port",
        default_value_t = 31501,
        aliases = ["data-system-port"]
    )]
    pub data_system_port: u16,

    #[arg(
        long = "enable_merge_process",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        aliases = ["enable-merge-process"]
    )]
    pub enable_merge_process: bool,

    #[arg(
        long = "merge_runtime_paths",
        default_value = "/bin/sleep",
        aliases = ["merge-runtime-paths"]
    )]
    pub merge_runtime_paths: String,

    #[arg(
        long = "runtime_initial_port",
        default_value_t = 9000,
        aliases = [
            "merge_runtime_initial_port",
            "merge-runtime-initial-port"
        ]
    )]
    pub merge_runtime_initial_port: u16,

    #[arg(
        long = "port_num",
        default_value_t = 1000,
        aliases = ["merge_port_count", "merge-port-count"]
    )]
    pub merge_port_count: u32,

    #[arg(
        long = "runtime_logs_dir",
        default_value = "/tmp/yr-agent-runtime-logs",
        aliases = ["merge_runtime_log_path", "merge-runtime-log-path"]
    )]
    pub merge_runtime_log_path: String,

    #[arg(
        long = "merge_runtime_bind_mounts",
        default_value = "",
        aliases = ["merge-runtime-bind-mounts"]
    )]
    pub merge_runtime_bind_mounts: String,

    #[arg(long = "custom_resources", default_value = "", aliases = ["custom-resources"])]
    pub custom_resources: String,

    #[arg(
        long = "enable_inherit_env",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        aliases = ["enable-inherit-env"]
    )]
    pub enable_inherit_env: bool,

    #[arg(
        long = "oom_kill_enable",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        aliases = ["oom-kill-enable"]
    )]
    pub oom_kill_enable: bool,

    #[arg(
        long = "oom_kill_control_limit",
        default_value_t = 0,
        aliases = ["oom-kill-control-limit"]
    )]
    pub oom_kill_control_limit: i32,

    #[arg(
        long = "oom_consecutive_detection_count",
        default_value_t = 3,
        aliases = ["oom-consecutive-detection-count"]
    )]
    pub oom_consecutive_detection_count: i32,

    #[arg(
        long = "kill_process_timeout_seconds",
        default_value_t = 0,
        aliases = ["kill-process-timeout-seconds"]
    )]
    pub kill_process_timeout_seconds: u32,

    /// C++ `agent_address` (accepted; gRPC advertisement still uses `ip` + `agent_listen_port`).
    #[arg(long = "agent_address", default_value = "", aliases = ["agent-address"])]
    pub agent_address: String,

    #[command(flatten)]
    pub cpp_ignored: AgentCppIgnored,
}

impl Config {
    pub fn effective_merge_runtime_paths(&self) -> String {
        if self.merge_runtime_paths.trim() != "/bin/sleep"
            || self.cpp_ignored.runtime_dir.trim().is_empty()
        {
            return self.merge_runtime_paths.clone();
        }
        let runtime_dir = self.cpp_ignored.runtime_dir.trim().trim_end_matches('/');
        [
            format!("{runtime_dir}/cpp/bin/runtime"),
            format!("{runtime_dir}/go/bin/goruntime"),
            format!("{runtime_dir}/python/yr/main/yr_runtime_main.py"),
        ]
        .join(",")
    }

    pub fn effective_runtime_ld_library_path(&self) -> String {
        let mut parts: Vec<String> = self
            .cpp_ignored
            .runtime_ld_library_path
            .split(':')
            .map(|s| s.trim().to_string())
            .filter(|s| !s.is_empty())
            .collect();
        let runtime_dir = self.cpp_ignored.runtime_dir.trim().trim_end_matches('/');
        if !runtime_dir.is_empty() {
            let runtime_root = runtime_dir.strip_suffix("/service").unwrap_or(runtime_dir);
            parts.extend([
                format!("{runtime_dir}/cpp/lib"),
                format!("{runtime_root}/sdk/cpp/lib"),
                format!("{runtime_dir}/go/bin"),
                format!("{runtime_root}/sdk/go/lib"),
                format!("{runtime_dir}/java/lib"),
                format!("{runtime_root}/sdk/java/lib"),
                format!("{runtime_dir}/python/yr"),
            ]);
        }
        parts.dedup();
        parts.join(":")
    }

    pub fn grpc_listen_addr(&self) -> String {
        format!("{}:{}", self.host, self.agent_listen_port)
    }

    /// Endpoint advertised to schedulers (Register, etc.).
    pub fn agent_grpc_endpoint(&self) -> String {
        format!("http://{}:{}", self.host, self.agent_listen_port)
    }

    pub fn http_listen_addr(&self) -> String {
        format!("{}:{}", self.host, self.port)
    }

    pub fn embedded_runtime_manager_config(&self) -> yr_runtime_manager::Config {
        fn set_if_present(dst: &mut String, src: &str) {
            if !src.trim().is_empty() {
                *dst = src.trim().to_string();
            }
        }
        fn parse_i32(src: &str, default: i32) -> i32 {
            src.trim().parse().unwrap_or(default)
        }
        fn parse_u32(src: &str, default: u32) -> u32 {
            src.trim().parse().unwrap_or(default)
        }
        fn parse_bool(src: &str, default: bool) -> bool {
            match src.trim().to_ascii_lowercase().as_str() {
                "true" | "1" | "yes" | "on" => true,
                "false" | "0" | "no" | "off" | "" => default,
                _ => default,
            }
        }

        let mut rm = yr_runtime_manager::Config::embedded_in_agent(
            self.node_id.clone(),
            self.agent_grpc_endpoint(),
            self.effective_merge_runtime_paths(),
            self.merge_runtime_initial_port,
            self.merge_port_count,
            PathBuf::from(&self.merge_runtime_log_path),
            self.merge_runtime_bind_mounts.clone(),
        );

        rm.host = self.host.clone();
        rm.host_ip = if self.cpp_ignored.host_ip.trim().is_empty() {
            self.data_system_host.clone()
        } else {
            self.cpp_ignored.host_ip.trim().to_string()
        };
        rm.data_system_port = self.data_system_port.to_string();
        set_if_present(
            &mut rm.driver_server_port,
            &self.cpp_ignored.driver_server_port,
        );
        set_if_present(
            &mut rm.proxy_grpc_server_port,
            &self.cpp_ignored.proxy_grpc_server_port,
        );
        set_if_present(&mut rm.proxy_ip, &self.cpp_ignored.proxy_ip);
        set_if_present(&mut rm.runtime_dir, &self.cpp_ignored.runtime_dir);
        set_if_present(&mut rm.snuser_lib_dir, &self.cpp_ignored.snuser_lib_dir);
        set_if_present(&mut rm.runtime_logs_dir, &self.merge_runtime_log_path);
        set_if_present(&mut rm.runtime_home_dir, &self.cpp_ignored.runtime_home_dir);
        set_if_present(
            &mut rm.runtime_config_dir,
            &self.cpp_ignored.runtime_config_dir,
        );
        set_if_present(
            &mut rm.python_log_config_path,
            &self.cpp_ignored.python_log_config_path,
        );
        set_if_present(
            &mut rm.java_system_property,
            &self.cpp_ignored.java_system_property,
        );
        set_if_present(
            &mut rm.java_system_library_path,
            &self.cpp_ignored.java_system_library_path,
        );
        rm.runtime_ld_library_path = self.effective_runtime_ld_library_path();
        set_if_present(
            &mut rm.runtime_log_level,
            &self.cpp_ignored.runtime_log_level,
        );
        rm.runtime_max_log_size = parse_i32(
            &self.cpp_ignored.runtime_max_log_size,
            rm.runtime_max_log_size,
        );
        rm.runtime_max_log_file_num = parse_i32(
            &self.cpp_ignored.runtime_max_log_file_num,
            rm.runtime_max_log_file_num,
        );
        set_if_present(
            &mut rm.python_dependency_path,
            &self.cpp_ignored.python_dependency_path,
        );
        rm.runtime_ds_connect_timeout = parse_u32(
            &self.cpp_ignored.runtime_ds_connect_timeout,
            rm.runtime_ds_connect_timeout,
        );
        rm.runtime_uid = parse_i32(&self.cpp_ignored.runtime_uid, rm.runtime_uid);
        rm.runtime_gid = parse_i32(&self.cpp_ignored.runtime_gid, rm.runtime_gid);
        rm.enable_inherit_env = self.enable_inherit_env;
        rm.set_cmd_cred = parse_bool(&self.cpp_ignored.set_cmd_cred, rm.set_cmd_cred);
        rm.oom_kill_enable = self.oom_kill_enable;
        rm.oom_kill_control_limit = self.oom_kill_control_limit;
        rm.oom_consecutive_detection_count = self.oom_consecutive_detection_count;
        rm.kill_process_timeout_seconds = self.kill_process_timeout_seconds;
        rm.custom_resources = self.custom_resources.clone();
        rm
    }

    pub fn normalize_grpc_uri(addr: &str) -> String {
        let a = addr.trim();
        if a.starts_with("http://") || a.starts_with("https://") {
            a.to_string()
        } else {
            format!("http://{a}")
        }
    }
}
