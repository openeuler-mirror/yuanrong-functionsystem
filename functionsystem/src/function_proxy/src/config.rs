use clap::builder::BoolishValueParser;
use clap::Parser;
use serde::{Deserialize, Serialize};

/// Parsed schedule plugin configuration (opaque JSON for future plugin loaders).
#[derive(Debug, Clone, Default, Serialize, Deserialize)]
pub struct SchedulePluginsConfig {
    pub raw: serde_json::Value,
}

/// C++ `function_proxy` / merge-process flags not implemented in Rust; accepted for `install.sh`.
#[derive(Parser, Debug, Clone)]
#[allow(dead_code)]
pub struct ProxyCppIgnored {
    #[arg(long = "address", default_value = "")]
    pub address: String,
    #[arg(long = "log_config", default_value = "")]
    pub log_config: String,
    #[arg(long = "services_path", default_value = "")]
    pub services_path: String,
    #[arg(long = "lib_path", default_value = "")]
    pub lib_path: String,
    #[arg(long = "function_meta_path", default_value = "")]
    pub function_meta_path: String,
    #[arg(long = "enable_trace", default_value = "")]
    pub enable_trace: String,
    #[arg(long = "trace_config", default_value = "")]
    pub trace_config: String,
    #[arg(long = "enable_metrics", default_value = "")]
    pub enable_metrics: String,
    #[arg(long = "metrics_config", default_value = "")]
    pub metrics_config: String,
    #[arg(long = "metrics_config_file", default_value = "")]
    pub metrics_config_file: String,
    #[arg(long = "litebus_thread_num", default_value = "")]
    pub litebus_thread_num: String,
    #[arg(long = "update_resource_cycle", default_value = "")]
    pub update_resource_cycle: String,
    #[arg(long = "pseudo_data_plane", default_value = "")]
    pub pseudo_data_plane: String,
    #[arg(long = "system_timeout", default_value = "")]
    pub system_timeout: String,
    #[arg(long = "max_priority", default_value = "")]
    pub max_priority: String,
    #[arg(long = "runtime_ds_encrypt_enable", default_value = "")]
    pub runtime_ds_encrypt_enable: String,
    #[arg(long = "runtime_ds_auth_enable", default_value = "")]
    pub runtime_ds_auth_enable: String,
    #[arg(long = "curve_key_path", default_value = "")]
    pub curve_key_path: String,
    #[arg(long = "cache_storage_auth_type", default_value = "")]
    pub cache_storage_auth_type: String,
    #[arg(long = "cache_storage_auth_enable", default_value = "")]
    pub cache_storage_auth_enable: String,
    #[arg(long = "ssl_downgrade_enable", default_value = "")]
    pub ssl_downgrade_enable: String,
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
    #[arg(long = "etcd_target_name_override", default_value = "")]
    pub etcd_target_name_override: String,
    #[arg(long = "is_partial_watch_instances", default_value = "")]
    pub is_partial_watch_instances: String,
    #[arg(long = "meta_store_excluded_keys", default_value = "")]
    pub meta_store_excluded_keys: String,
    #[arg(long = "runtime_instance_debug_enable", default_value = "")]
    pub runtime_instance_debug_enable: String,
    #[arg(long = "enable_traefik_registry", default_value = "")]
    pub enable_traefik_registry: String,
    #[arg(long = "traefik_etcd_prefix", default_value = "")]
    pub traefik_etcd_prefix: String,
    #[arg(long = "traefik_lease_ttl", default_value = "")]
    pub traefik_lease_ttl: String,
    #[arg(long = "traefik_http_entrypoint", default_value = "")]
    pub traefik_http_entrypoint: String,
    #[arg(long = "traefik_enable_tls", default_value = "")]
    pub traefik_enable_tls: String,
    #[arg(long = "traefik_servers_transport", default_value = "")]
    pub traefik_servers_transport: String,
    /// Merge path only: second `--port=` (distinct from `grpc_listen_port`).
    #[arg(long = "port", default_value = "")]
    pub merge_process_port: String,
    #[arg(long = "agent_listen_port", default_value = "")]
    pub agent_listen_port: String,
    #[arg(long = "local_scheduler_address", default_value = "")]
    pub local_scheduler_address: String,
    #[arg(long = "runtime_dir", default_value = "")]
    pub runtime_dir: String,
    #[arg(long = "runtime_home_dir", default_value = "")]
    pub runtime_home_dir: String,
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
    pub proxy_grpc_server_port_line: String,
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
    #[arg(long = "agent_address", default_value = "")]
    pub agent_address: String,
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
    #[arg(long = "oom_kill_enable", default_value = "")]
    pub oom_kill_enable: String,
    #[arg(long = "oom_kill_control_limit", default_value = "")]
    pub oom_kill_control_limit: String,
    #[arg(long = "oom_consecutive_detection_count", default_value = "")]
    pub oom_consecutive_detection_count: String,
    #[arg(long = "kill_process_timeout_seconds", default_value = "")]
    pub kill_process_timeout_seconds: String,
    #[arg(long = "runtime_ds_connect_timeout", default_value = "")]
    pub runtime_ds_connect_timeout: String,
    #[arg(long = "runtime_direct_connection_enable", default_value = "")]
    pub runtime_direct_connection_enable: String,
    #[arg(long = "runtime_default_config", default_value = "")]
    pub runtime_default_config: String,
    #[arg(long = "proc_metrics_memory", default_value = "")]
    pub proc_metrics_memory: String,
    #[arg(long = "data_system_enable", default_value = "")]
    pub data_system_enable: String,
    #[arg(long = "agent_uid", default_value = "")]
    pub agent_uid: String,
    #[arg(long = "alias", default_value = "")]
    pub agent_alias: String,
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
    #[arg(long = "enable_dis_conv_call_stack", default_value = "")]
    pub enable_dis_conv_call_stack: String,
    #[arg(long = "runtime_ld_library_path", default_value = "")]
    pub runtime_ld_library_path: String,
    #[arg(long = "runtime_log_level", default_value = "")]
    pub runtime_log_level: String,
    #[arg(long = "runtime_max_log_size", default_value = "")]
    pub runtime_max_log_size: String,
    #[arg(long = "runtime_max_log_file_num", default_value = "")]
    pub runtime_max_log_file_num: String,
}

#[derive(Parser, Debug, Clone)]
#[command(
    name = "function_proxy",
    about = "openYuanrong function proxy / local scheduler (Rust)"
)]
pub struct Config {
    #[arg(
        long = "ip",
        default_value = "0.0.0.0",
        aliases = ["host"],
        visible_alias = "host"
    )]
    pub host: String,

    /// Primary gRPC listen port (C++ `grpc_listen_port`).
    #[arg(
        long = "grpc_listen_port",
        default_value_t = 8402,
        visible_alias = "grpc-listen-port",
        aliases = ["grpc-listen-port"]
    )]
    pub port: u16,

    #[arg(
        long = "posix_port",
        default_value_t = 8403,
        visible_alias = "posix-port",
        aliases = ["posix-port"]
    )]
    pub posix_port: u16,

    #[arg(
        long = "http_port",
        default_value_t = 18402,
        visible_alias = "http-port",
        aliases = ["http-port"]
    )]
    pub http_port: u16,

    #[arg(
        long = "session_grpc_port",
        default_value_t = 18403,
        visible_alias = "session-grpc-port",
        aliases = ["session-grpc-port"]
    )]
    pub session_grpc_port: u16,

    #[arg(
        long = "global_scheduler_address",
        default_value = "",
        visible_alias = "global-scheduler-address",
        aliases = ["global-scheduler-address"]
    )]
    pub global_scheduler_address: String,

    #[arg(
        long = "domain_scheduler_address",
        default_value = "",
        visible_alias = "domain-scheduler-address",
        aliases = ["domain-scheduler-address"]
    )]
    pub domain_scheduler_address: String,

    #[arg(
        long = "etcd_address",
        default_value = "",
        visible_alias = "etcd-endpoints",
        aliases = ["etcd-endpoints"]
    )]
    pub etcd_endpoints: String,

    #[arg(
        long = "etcd_table_prefix",
        default_value = "",
        visible_alias = "etcd-table-prefix",
        aliases = ["etcd-table-prefix"]
    )]
    pub etcd_table_prefix: String,

    #[arg(
        long = "node_id",
        default_value = "",
        visible_alias = "node-id",
        aliases = ["node-id"]
    )]
    pub node_id: String,

    #[arg(
        long = "enable_preemption",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        visible_alias = "enable-preemption",
        aliases = ["enable-preemption"]
    )]
    pub enable_preemption: bool,

    #[arg(long = "min_instance_cpu_size", default_value = "0.1")]
    pub min_instance_cpu: f64,

    #[arg(long = "max_instance_cpu_size", default_value = "64.0")]
    pub max_instance_cpu: f64,

    #[arg(long = "min_instance_memory_size", default_value = "134217728")]
    pub min_instance_memory: f64,

    #[arg(long = "max_instance_memory_size", default_value = "68719476736")]
    pub max_instance_memory: f64,

    #[arg(
        long = "schedule_plugins",
        default_value = "{}",
        visible_alias = "schedule-plugins",
        aliases = ["schedule-plugins"]
    )]
    pub schedule_plugins: String,

    #[arg(
        long = "cache_storage_host",
        default_value = "127.0.0.1",
        aliases = ["data_system_host", "data-system-host"]
    )]
    pub data_system_host: String,

    #[arg(
        long = "cache_storage_port",
        default_value = "0",
        aliases = ["data_system_port", "data-system-port"]
    )]
    pub data_system_port: u16,

    #[arg(
        long = "runtime_manager_address",
        default_value = "",
        visible_alias = "runtime-manager-address",
        aliases = ["runtime-manager-address"]
    )]
    pub runtime_manager_address: String,

    #[arg(
        long = "create_rate_limit_per_sec",
        default_value = "0",
        visible_alias = "create-rate-limit-per-sec",
        aliases = ["create-rate-limit-per-sec"]
    )]
    pub create_rate_limit_per_sec: u32,

    #[arg(long = "proxy_aid", default_value = "")]
    pub proxy_aid: String,

    #[arg(long = "proxy_access_key", default_value = "")]
    pub proxy_access_key: String,

    #[arg(long = "busproxy_tenant_segment", default_value = "0")]
    pub busproxy_tenant_segment: String,

    #[arg(long = "busproxy_lease_ttl_sec", default_value = "30")]
    pub busproxy_lease_ttl_sec: u64,

    #[arg(long = "dposix_uds_path", default_value = "", aliases = ["posix_uds_path"])]
    pub posix_uds_path: String,

    #[arg(
        long = "exec_session_idle_sec",
        default_value = "120",
        visible_alias = "exec-session-idle-sec",
        aliases = ["exec-session-idle-sec"]
    )]
    pub exec_session_idle_sec: u64,

    #[arg(
        long = "election_mode",
        default_value = "standalone",
        visible_alias = "election-mode",
        aliases = ["election-mode"]
    )]
    pub election_mode: String,

    #[arg(
        long = "state_storage_type",
        default_value = "disable",
        visible_alias = "state-storage-type",
        aliases = ["state-storage-type"]
    )]
    pub state_storage_type: String,

    #[arg(
        long = "enable_server_mode",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = true,
        value_parser = BoolishValueParser::new(),
        visible_alias = "enable-server-mode",
        aliases = ["enable-server-mode"]
    )]
    pub enable_server_mode: bool,

    #[arg(
        long = "enable_driver",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        visible_alias = "enable-driver",
        aliases = ["enable-driver"]
    )]
    pub enable_driver: bool,

    #[arg(
        long = "runtime_recover_enable",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        visible_alias = "runtime-recover-enable",
        aliases = ["runtime-recover-enable"]
    )]
    pub runtime_recover_enable: bool,

    #[arg(
        long = "runtime_heartbeat_enable",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = true,
        value_parser = BoolishValueParser::new(),
        visible_alias = "runtime-heartbeat-enable",
        aliases = ["runtime-heartbeat-enable"]
    )]
    pub runtime_heartbeat_enable: bool,

    #[arg(
        long = "runtime_max_heartbeat_timeout_times",
        default_value = "5",
        visible_alias = "runtime-max-heartbeat-timeout-times",
        aliases = ["runtime-max-heartbeat-timeout-times"]
    )]
    pub runtime_max_heartbeat_timeout_times: u32,

    #[arg(
        long = "runtime_heartbeat_timeout_ms",
        default_value = "5000",
        visible_alias = "runtime-heartbeat-timeout-ms",
        aliases = ["runtime-heartbeat-timeout-ms"]
    )]
    pub runtime_heartbeat_timeout_ms: u32,

    #[arg(
        long = "runtime_init_call_timeout_seconds",
        default_value = "300",
        visible_alias = "runtime-init-call-timeout-seconds",
        aliases = ["runtime-init-call-timeout-seconds"]
    )]
    pub runtime_init_call_timeout_seconds: u32,

    #[arg(
        long = "runtime_shutdown_timeout_seconds",
        default_value = "30",
        visible_alias = "runtime-shutdown-timeout-seconds",
        aliases = ["runtime-shutdown-timeout-seconds"]
    )]
    pub runtime_shutdown_timeout_seconds: u32,

    #[arg(
        long = "runtime_conn_timeout_s",
        default_value = "30",
        visible_alias = "runtime-conn-timeout-s",
        aliases = ["runtime-conn-timeout-s"]
    )]
    pub runtime_conn_timeout_s: u32,

    #[arg(
        long = "max_grpc_size",
        default_value = "11",
        visible_alias = "max-grpc-size",
        aliases = ["max-grpc-size"]
    )]
    pub max_grpc_size: i32,

    #[arg(
        long = "enable_iam",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        visible_alias = "enable-iam",
        aliases = ["enable-iam"]
    )]
    pub enable_iam: bool,

    #[arg(
        long = "iam_base_path",
        default_value = "",
        visible_alias = "iam-base-path",
        aliases = ["iam-base-path"]
    )]
    pub iam_base_path: String,

    #[arg(
        long = "iam_policy_file",
        default_value = "",
        visible_alias = "iam-policy-file",
        aliases = ["iam-policy-file"]
    )]
    pub iam_policy_file: String,

    #[arg(
        long = "iam_meta_store_address",
        default_value = "",
        visible_alias = "iam-meta-store-address",
        aliases = ["iam-meta-store-address"]
    )]
    pub iam_meta_store_address: String,

    #[arg(
        long = "iam_credential_type",
        default_value = "token",
        visible_alias = "iam-credential-type",
        aliases = ["iam-credential-type"]
    )]
    pub iam_credential_type: String,

    #[arg(
        long = "enable_tenant_affinity",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        visible_alias = "enable-tenant-affinity",
        aliases = ["enable-tenant-affinity"]
    )]
    pub enable_tenant_affinity: bool,

    #[arg(
        long = "enable_merge_process",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        visible_alias = "enable-merge-process",
        aliases = ["enable-merge-process"]
    )]
    pub enable_merge_process: bool,

    #[arg(
        long = "merge_runtime_paths",
        default_value = "/bin/sleep",
        visible_alias = "merge-runtime-paths",
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
        default_value = "/tmp/yr-proxy-runtime-logs",
        aliases = ["merge_runtime_log_path", "merge-runtime-log-path"]
    )]
    pub merge_runtime_log_path: String,

    #[arg(
        long = "merge_runtime_bind_mounts",
        default_value = "",
        visible_alias = "merge-runtime-bind-mounts",
        aliases = ["merge-runtime-bind-mounts"]
    )]
    pub merge_runtime_bind_mounts: String,

    #[arg(
        long = "forward_compatibility",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        visible_alias = "forward-compatibility",
        aliases = ["forward-compatibility"]
    )]
    pub forward_compatibility: bool,

    #[arg(
        long = "decrypt_algorithm",
        default_value = "NO_CRYPTO",
        visible_alias = "decrypt-algorithm",
        aliases = ["decrypt-algorithm"]
    )]
    pub decrypt_algorithm: String,

    #[arg(
        long = "enable_print_resource_view",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        visible_alias = "enable-print-resource-view",
        aliases = ["enable-print-resource-view"]
    )]
    pub enable_print_resource_view: bool,

    #[arg(
        long = "enable_print_perf",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        visible_alias = "enable-print-perf",
        aliases = ["enable-print-perf"]
    )]
    pub enable_print_perf: bool,

    #[arg(
        long = "enable_meta_store",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        visible_alias = "enable-meta-store",
        aliases = ["enable-meta-store"]
    )]
    pub enable_meta_store: bool,

    #[arg(
        long = "meta_store_address",
        default_value = "",
        visible_alias = "meta-store-address",
        aliases = ["meta-store-address"]
    )]
    pub meta_store_address: String,

    #[arg(
        long = "meta_store_mode",
        default_value = "local",
        visible_alias = "meta-store-mode",
        aliases = ["meta-store-mode"]
    )]
    pub meta_store_mode: String,

    #[arg(
        long = "service_register_times",
        default_value = "1000",
        visible_alias = "service-register-times",
        aliases = ["service-register-times"]
    )]
    pub service_register_times: u32,

    #[arg(
        long = "service_register_cycle",
        default_value = "10000",
        visible_alias = "service-register-cycle",
        aliases = ["service-register-cycle"]
    )]
    pub service_register_cycle: u32,

    #[arg(
        long = "service_ttl",
        default_value = "300000",
        visible_alias = "service-ttl",
        aliases = ["service-ttl"]
    )]
    pub service_ttl: i32,

    #[arg(
        long = "unregister_while_stop",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        visible_alias = "unregister-while-stop",
        aliases = ["unregister-while-stop"]
    )]
    pub unregister_while_stop: bool,

    #[arg(
        long = "enable_function_meta_watch",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = true,
        value_parser = BoolishValueParser::new(),
        visible_alias = "enable-function-meta-watch",
        aliases = ["enable-function-meta-watch"]
    )]
    pub enable_function_meta_watch: bool,

    #[arg(
        long = "require_function_meta",
        num_args = 0..=1,
        default_missing_value = "true",
        default_value_t = false,
        value_parser = BoolishValueParser::new(),
        visible_alias = "require-function-meta",
        aliases = ["require-function-meta"]
    )]
    pub require_function_meta: bool,

    #[command(flatten)]
    pub cpp_ignored: ProxyCppIgnored,
}

impl Config {
    pub fn etcd_endpoints_vec(&self) -> Vec<String> {
        self.etcd_endpoints
            .split(',')
            .map(|s| s.trim().to_string())
            .filter(|s| !s.is_empty())
            .collect()
    }

    pub fn schedule_plugins_config(&self) -> anyhow::Result<SchedulePluginsConfig> {
        let v: serde_json::Value = if self.schedule_plugins.trim().is_empty() {
            serde_json::json!({})
        } else {
            serde_json::from_str(&self.schedule_plugins)?
        };
        Ok(SchedulePluginsConfig { raw: v })
    }

    pub fn grpc_listen_addr(&self) -> String {
        format!("{}:{}", self.host, self.port)
    }

    pub fn advertise_grpc_endpoint(&self) -> String {
        format!("http://{}:{}", self.host, self.port)
    }
}
