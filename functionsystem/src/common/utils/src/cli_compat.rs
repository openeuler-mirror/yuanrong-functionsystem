use std::collections::HashSet;
use std::ffi::{OsStr, OsString};

use clap::{Command, CommandFactory, Parser};

fn long_names(command: &Command) -> HashSet<String> {
    let mut names = HashSet::new();
    for arg in command.get_arguments() {
        if let Some(long) = arg.get_long() {
            names.insert(long.to_string());
        }
        if let Some(aliases) = arg.get_all_aliases() {
            names.extend(aliases.into_iter().map(str::to_string));
        }
    }
    names
}

fn split_long_flag(arg: &OsStr) -> Option<(String, Option<String>)> {
    let s = arg.to_str()?;
    let rest = s.strip_prefix("--")?;
    if rest.is_empty() {
        return None;
    }
    if let Some((name, value)) = rest.split_once('=') {
        Some((name.to_string(), Some(value.to_string())))
    } else {
        Some((rest.to_string(), None))
    }
}

fn make_long_arg(name: &str, value: Option<&str>) -> OsString {
    match value {
        Some(v) => OsString::from(format!("--{name}={v}")),
        None => OsString::from(format!("--{name}")),
    }
}

/// Parse a clap command while preserving C++ 0.8 black-box launch compatibility.
///
/// The official deployment layer may still pass C++ `snake_case` flags. Rust clap
/// prefers hyphenated names for fields declared with `#[arg(long)]`, so this
/// adapter rewrites `--foo_bar` to `--foo-bar` when that target flag exists. C++
/// legacy flags that Rust does not implement are accepted and ignored rather than
/// aborting process startup.
pub fn parse_with_legacy_flags<T>(legacy_ignored_flags: &[&str]) -> T
where
    T: Parser + CommandFactory,
{
    let command = T::command();
    let valid = long_names(&command);
    let legacy: HashSet<&str> = legacy_ignored_flags.iter().copied().collect();
    let mut out = Vec::<OsString>::new();
    let mut args = std::env::args_os();

    if let Some(program) = args.next() {
        out.push(program);
    }

    let mut skip_next_value = false;
    for arg in args {
        if skip_next_value {
            if let Some(s) = arg.to_str() {
                if !s.starts_with('-') {
                    skip_next_value = false;
                    continue;
                }
            }
            skip_next_value = false;
        }

        let Some((name, inline_value)) = split_long_flag(&arg) else {
            out.push(arg);
            continue;
        };

        if valid.contains(&name) {
            out.push(make_long_arg(&name, inline_value.as_deref()));
            continue;
        }

        let hyphen = name.replace('_', "-");
        if hyphen != name && valid.contains(&hyphen) {
            out.push(make_long_arg(&hyphen, inline_value.as_deref()));
            continue;
        }

        if legacy.contains(name.as_str()) {
            if inline_value.is_none() {
                skip_next_value = true;
            }
            continue;
        }

        out.push(arg);
    }

    T::parse_from(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use clap::Parser;

    #[derive(Parser, Debug, PartialEq)]
    struct Args {
        #[arg(long)]
        runtime_dir: String,
        #[arg(long, default_value = "")]
        known_flag: String,
    }

    #[test]
    fn rewrites_snake_case_to_hyphenated_clap_long() {
        let command = Args::command();
        let valid = long_names(&command);
        assert!(valid.contains("runtime-dir"));
        assert!(!valid.contains("runtime_dir"));
    }

    #[test]
    fn ignored_legacy_flags_are_removed() {
        let legacy = ["old_flag"];
        let args = vec![
            OsString::from("bin"),
            OsString::from("--runtime_dir=/r"),
            OsString::from("--old_flag"),
            OsString::from("ignored"),
        ];
        let command = Args::command();
        let valid = long_names(&command);
        let mut out = Vec::<OsString>::new();
        let legacy_set: HashSet<&str> = legacy.iter().copied().collect();
        let mut skip_next_value = false;
        for arg in args.into_iter() {
            if out.is_empty() {
                out.push(arg);
                continue;
            }
            if skip_next_value {
                if let Some(s) = arg.to_str() {
                    if !s.starts_with('-') {
                        skip_next_value = false;
                        continue;
                    }
                }
                skip_next_value = false;
            }
            if let Some((name, inline_value)) = split_long_flag(&arg) {
                let hyphen = name.replace('_', "-");
                if hyphen != name && valid.contains(&hyphen) {
                    out.push(make_long_arg(&hyphen, inline_value.as_deref()));
                } else if legacy_set.contains(name.as_str()) {
                    if inline_value.is_none() {
                        skip_next_value = true;
                    }
                } else {
                    out.push(arg);
                }
            } else {
                out.push(arg);
            }
        }
        let parsed = Args::parse_from(out);
        assert_eq!(parsed.runtime_dir, "/r");
    }
}

/// C++ 0.8 flags accepted for black-box launch compatibility.
pub mod legacy_flags {
    pub const DOMAIN_SCHEDULER: &[&str] = &[
        "aggregated_strategy",
        "cluster_id",
        "decrypt_algorithm",
        "domain_listen_port",
        "elect_keep_alive_interval",
        "election_mode",
        "etcd_address",
        "etcd_auth_type",
        "etcd_cert_file",
        "etcd_key_file",
        "etcd_root_ca_file",
        "etcd_secret_name",
        "etcd_ssl_base_path",
        "etcd_table_prefix",
        "etcd_target_name_override",
        "global_dddress",
        "ip",
        "k8s_base_path",
        "k8s_namespace",
        "litebus_thread_num",
        "log_config",
        "max_instance_cpu_size",
        "max_instance_memory_size",
        "max_priority",
        "max_tolerate_metastore_healthcheck_failed_times",
        "meta_store_address",
        "meta_store_excluded_keys",
        "metastore_healthcheck_interval",
        "metastore_healthcheck_timeout",
        "metrics_config",
        "metrics_config_file",
        "min_instance_cpu_size",
        "min_instance_memory_size",
        "node_id",
        "observability_agent_grpc_port",
        "observability_prometheus_port",
        "prometheus_pushgateway_ip",
        "prometheus_pushgateway_port",
        "pull_resource_interval",
        "quota_config_file",
        "resource_path",
        "schedule_relaxed",
        "ssl_base_path",
        "ssl_cert_file",
        "ssl_key_file",
        "ssl_root_file",
        "system_auth_mode",
        "system_timeout",
        "trace_config",
    ];

    pub const FUNCTION_AGENT: &[&str] = &[
        "access_key",
        "agent_plugin_configs",
        "aggregated_strategy",
        "cluster_id",
        "code_aging_time",
        "code_package_thresholds_config_path",
        "credential_type",
        "decrypt_algorithm",
        "dir_depth_max",
        "etcd_address",
        "etcd_secret_name",
        "etcd_table_prefix",
        "etcd_target_name_override",
        "file_count_max",
        "max_instance_cpu_size",
        "max_instance_memory_size",
        "max_priority",
        "max_tolerate_metastore_healthcheck_failed_times",
        "meta_store_excluded_keys",
        "metastore_healthcheck_interval",
        "metastore_healthcheck_timeout",
        "min_instance_cpu_size",
        "min_instance_memory_size",
        "observability_agent_grpc_port",
        "observability_prometheus_port",
        "prometheus_pushgateway_ip",
        "prometheus_pushgateway_port",
        "pull_resource_interval",
        "quota_config_file",
        "resource_path",
        "s3_protocol",
        "schedule_relaxed",
        "secret_key",
        "system_auth_mode",
        "trace_config",
        "unzip_file_size_max_MB",
        "zip_file_size_max_MB",
        "enable_trace",
        "metrics_ssl_enable",
        "scc_algorithm",
        "scc_base_path",
        "scc_enable",
        "scc_log_path",
        "scc_primary_file",
        "scc_standby_file",
        "signature_validation",
        "ssl_decrypt_tool",
        "ssl_pwd_file",
    ];

    pub const FUNCTION_MASTER: &[&str] = &[
        "agent_template_path",
        "aggregated_strategy",
        "az_id",
        "d1",
        "d2",
        "elect_keep_alive_interval",
        "elect_lease_ttl",
        "etcd_secret_name",
        "evicted_taint_key",
        "health_Monitor_retry_interval",
        "k8s_base_path",
        "k8s_client_cert_file",
        "k8s_client_key_file",
        "k8s_namespace",
        "kube_api_retry_cycle",
        "kube_client_retry_times",
        "local_scheduler_port",
        "max_instance_cpu_size",
        "max_instance_memory_size",
        "max_tolerate_metastore_healthcheck_failed_times",
        "metastore_healthcheck_interval",
        "metastore_healthcheck_timeout",
        "migrate_prefix",
        "min_instance_cpu_size",
        "min_instance_memory_size",
        "observability_agent_grpc_port",
        "observability_prometheus_port",
        "prometheus_pushgateway_ip",
        "prometheus_pushgateway_port",
        "quota_config_file",
        "resource_path",
        "self_taint_prefix",
        "skip_k8s_tls_verify",
        "sys_func_custom_args",
        "system_auth_mode",
        "system_upgrade_address",
        "system_upgrade_key",
        "taint_tolerance_list",
        "worker_taint_exclude_labels",
        "enable_fake_suspend_resume",
        "etcd_decrypt_tool",
        "metrics_ssl_enable",
        "system_upgrade_watch_enable",
    ];

    pub const FUNCTION_PROXY: &[&str] = &[
        "aggregated_strategy",
        "cache_storage_auth_ak",
        "cache_storage_auth_sk",
        "cache_storage_info_prefix",
        "cluster_id",
        "ds_health_check_interval",
        "ds_health_check_path",
        "elect_keep_alive_interval",
        "etcd_secret_name",
        "fc_agent_mgr_retry_cycle",
        "fc_agent_mgr_retry_times",
        "high_memory_threshold",
        "k8s_base_path",
        "k8s_namespace",
        "low_memory_threshold",
        "max_ds_health_check_times",
        "max_tolerate_metastore_healthcheck_failed_times",
        "message_size_threshold",
        "metastore_healthcheck_interval",
        "metastore_healthcheck_timeout",
        "observability_agent_grpc_port",
        "observability_prometheus_port",
        "prometheus_pushgateway_ip",
        "prometheus_pushgateway_port",
        "pull_resource_interval",
        "quota_config_file",
        "redis_conf_path",
        "resource_path",
        "runtime_ds_client_private_key",
        "runtime_ds_client_public_key",
        "runtime_ds_server_public_key",
        "schedule_policy",
        "schedule_relaxed",
        "system_auth_mode",
        "tenant_pod_reuse_time_window",
        "token_bucket_capacity",
        "create_limitation_enable",
        "custom_resources",
        "enable_fake_suspend_resume",
        "enable_inherit_env",
        "enable_ipv4_tenant_isolation",
        "external_iam_endpoint",
        "invoke_limitation_enable",
        "metrics_ssl_enable",
        "oidc_audience",
        "oidc_project_id",
        "oidc_project_name",
        "oidc_workload_identity",
        "s3_credential_type",
        "temporary_accessKey_expiration_seconds",
    ];

    pub const IAM_SERVER: &[&str] = &[
        "aggregated_strategy",
        "credential_host_address",
        "decrypt_algorithm",
        "elect_keep_alive_interval",
        "elect_lease_ttl",
        "etcd_auth_type",
        "etcd_cert_file",
        "etcd_key_file",
        "etcd_root_ca_file",
        "etcd_secret_name",
        "etcd_ssl_base_path",
        "etcd_target_name_override",
        "k8s_base_path",
        "k8s_namespace",
        "litebus_thread_num",
        "max_instance_cpu_size",
        "max_instance_memory_size",
        "max_priority",
        "max_tolerate_metastore_healthcheck_failed_times",
        "meta_store_excluded_keys",
        "metastore_healthcheck_interval",
        "metastore_healthcheck_timeout",
        "metrics_config",
        "metrics_config_file",
        "min_instance_cpu_size",
        "min_instance_memory_size",
        "observability_agent_grpc_port",
        "observability_prometheus_port",
        "permanent_cred_conf_path",
        "prometheus_pushgateway_ip",
        "prometheus_pushgateway_port",
        "pull_resource_interval",
        "quota_config_file",
        "resource_path",
        "schedule_relaxed",
        "system_auth_mode",
        "system_timeout",
        "trace_config",
    ];

    pub const RUNTIME_MANAGER: &[&str] = &[
        "agent_address",
        "aggregated_strategy",
        "cluster_id",
        "custom_resources",
        "data_system_port",
        "decrypt_algorithm",
        "disk_resources",
        "disk_usage_limit",
        "disk_usage_monitor_duration",
        "disk_usage_monitor_path",
        "driver_server_port",
        "etcd_address",
        "etcd_auth_type",
        "etcd_cert_file",
        "etcd_key_file",
        "etcd_root_ca_file",
        "etcd_secret_name",
        "etcd_ssl_base_path",
        "etcd_table_prefix",
        "etcd_target_name_override",
        "host_ip",
        "ip",
        "java_system_library_path",
        "java_system_property",
        "kill_process_timeout_seconds",
        "litebus_thread_num",
        "log_config",
        "log_expiration_cleanup_interval",
        "log_expiration_max_file_count",
        "log_expiration_time_threshold",
        "max_instance_cpu_size",
        "max_instance_memory_size",
        "max_priority",
        "max_tolerate_metastore_healthcheck_failed_times",
        "memory_detection_interval",
        "meta_store_excluded_keys",
        "metastore_healthcheck_interval",
        "metastore_healthcheck_timeout",
        "metrics_collector_type",
        "metrics_config",
        "metrics_config_file",
        "min_instance_cpu_size",
        "min_instance_memory_size",
        "node_id",
        "nodejs_entry",
        "npu_collection_mode",
        "npu_device_info_path",
        "observability_agent_grpc_port",
        "observability_prometheus_port",
        "oom_consecutive_detection_count",
        "oom_kill_control_limit",
        "overhead_cpu",
        "overhead_memory",
        "port_num",
        "proc_metrics_cpu",
        "proc_metrics_memory",
        "prometheus_pushgateway_ip",
        "prometheus_pushgateway_port",
        "proxy_grpc_server_port",
        "proxy_ip",
        "pull_resource_interval",
        "python_dependency_path",
        "python_log_config_path",
        "quota_config_file",
        "resource_label_path",
        "resource_path",
        "runtime_config_dir",
        "runtime_default_config",
        "runtime_dir",
        "runtime_ds_connect_timeout",
        "runtime_gid",
        "runtime_home_dir",
        "runtime_initial_port",
        "runtime_ld_library_path",
        "runtime_log_level",
        "runtime_logs_dir",
        "runtime_max_log_file_num",
        "runtime_max_log_size",
        "runtime_prestart_config",
        "runtime_std_log_dir",
        "runtime_uid",
        "schedule_relaxed",
        "snuser_disk_usage_limit",
        "snuser_lib_dir",
        "ssl_base_path",
        "ssl_cert_file",
        "ssl_key_file",
        "ssl_root_file",
        "system_auth_mode",
        "system_timeout",
        "tmp_disk_usage_limit",
        "trace_config",
        "user_log_auto_flush_interval_ms",
        "user_log_buffer_flush_threshold",
        "user_log_export_mode",
        "user_log_rolling_file_count_limit",
        "user_log_rolling_size_limit_mb",
        "virtual_env_idle_time_limit",
        "enable_clean_stream_producer",
        "enable_metrics",
        "enable_trace",
        "gpu_collection_enable",
        "is_protomsg_to_runtime",
        "log_expiration_enable",
        "log_reuse_enable",
        "massif_enable",
        "runtime_direct_connection_enable",
        "runtime_instance_debug_enable",
    ];
}
