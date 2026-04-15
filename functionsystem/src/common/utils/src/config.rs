use clap::Args;
use serde::Deserialize;
use std::path::Path;

use crate::error::{YrError, YrResult};

/// Deserialize a JSON config document from a string (same error style as file-based loading).
pub fn load_config_from_json_str<T: for<'de> Deserialize<'de>>(content: &str) -> YrResult<T> {
    serde_json::from_str(content).map_err(|e| YrError::Config(e.to_string()))
}

/// Deserialize a YAML config document from a string.
pub fn load_config_from_yaml_str<T: for<'de> Deserialize<'de>>(content: &str) -> YrResult<T> {
    serde_yaml::from_str(content).map_err(|e| YrError::Config(e.to_string()))
}

/// Load a YAML or JSON config file, deserializing into the target type.
pub fn load_config<T: for<'de> Deserialize<'de>>(path: &Path) -> YrResult<T> {
    let content = std::fs::read_to_string(path).map_err(|e| {
        YrError::Config(format!("failed to read config file {}: {}", path.display(), e))
    })?;

    let ext = path.extension().and_then(|e| e.to_str()).unwrap_or("");
    match ext {
        "yaml" | "yml" => {
            serde_yaml::from_str(&content).map_err(|e| YrError::Config(e.to_string()))
        }
        "json" => serde_json::from_str(&content).map_err(|e| YrError::Config(e.to_string())),
        _ => {
            // Try YAML first, then JSON
            serde_yaml::from_str(&content)
                .or_else(|_| serde_json::from_str(&content).map_err(|e| YrError::Config(e.to_string())))
                .map_err(|e| YrError::Config(format!("failed to parse config: {}", e)))
        }
    }
}

/// Shared CLI flags inherited by all functionsystem components.
/// Mirrors C++ CommonFlags from common_flags.cpp.
#[derive(Args, Clone, Debug)]
pub struct CommonConfig {
    // === etcd ===
    #[arg(long, default_value = "")]
    pub etcd_address: String,

    #[arg(long, default_value = "")]
    pub etcd_table_prefix: String,

    #[arg(long, default_value = "")]
    pub cluster_id: String,

    // === etcd auth ===
    #[arg(long, default_value = "Noauth")]
    pub etcd_auth_type: String,

    #[arg(long, default_value = "")]
    pub etcd_secret_name: String,

    #[arg(long, default_value = "/home/sn/resource/etcd")]
    pub etcd_ssl_base_path: String,

    #[arg(long, default_value = "")]
    pub etcd_root_ca_file: String,

    #[arg(long, default_value = "")]
    pub etcd_cert_file: String,

    #[arg(long, default_value = "")]
    pub etcd_key_file: String,

    #[arg(long, default_value = "")]
    pub etcd_target_name_override: String,

    // === SSL/TLS ===
    #[arg(long, default_value_t = false)]
    pub ssl_enable: bool,

    #[arg(long, default_value_t = false)]
    pub ssl_downgrade_enable: bool,

    #[arg(long, default_value = "/")]
    pub ssl_base_path: String,

    #[arg(long, default_value = "")]
    pub ssl_root_file: String,

    #[arg(long, default_value = "")]
    pub ssl_cert_file: String,

    #[arg(long, default_value = "")]
    pub ssl_key_file: String,

    // === Metrics & Trace ===
    #[arg(long, default_value_t = false)]
    pub enable_metrics: bool,

    #[arg(long, default_value = "")]
    pub metrics_config: String,

    #[arg(long, default_value = "")]
    pub metrics_config_file: String,

    #[arg(long, default_value_t = false)]
    pub metrics_ssl_enable: bool,

    #[arg(long, default_value_t = false)]
    pub enable_trace: bool,

    #[arg(long, default_value = "")]
    pub trace_config: String,

    #[arg(long, default_value_t = 4317)]
    pub observability_agent_grpc_port: u32,

    #[arg(long, default_value_t = 9392)]
    pub observability_prometheus_port: u32,

    #[arg(long, default_value_t = 9091)]
    pub prometheus_pushgateway_port: u32,

    #[arg(long, default_value = "")]
    pub prometheus_pushgateway_ip: String,

    // === Metastore healthcheck ===
    #[arg(long, default_value_t = 60)]
    pub max_tolerate_metastore_healthcheck_failed_times: u32,

    #[arg(long, default_value_t = 10000)]
    pub metastore_healthcheck_interval: u32,

    #[arg(long, default_value_t = 20000)]
    pub metastore_healthcheck_timeout: u32,

    // === Scheduling ===
    #[arg(long, default_value_t = 0)]
    pub max_priority: u16,

    #[arg(long, default_value_t = false)]
    pub enable_preemption: bool,

    #[arg(long, default_value = "no_aggregate")]
    pub aggregated_strategy: String,

    #[arg(long, default_value_t = -1)]
    pub schedule_relaxed: i32,

    // === Instance limits ===
    #[arg(long, default_value_t = 300)]
    pub min_instance_cpu_size: u64,

    #[arg(long, default_value_t = 16000)]
    pub max_instance_cpu_size: u64,

    #[arg(long, default_value_t = 128)]
    pub min_instance_memory_size: u64,

    #[arg(long, default_value_t = 1048576)]
    pub max_instance_memory_size: u64,

    // === System ===
    #[arg(long, default_value_t = 180000)]
    pub system_timeout: u32,

    #[arg(long, default_value_t = 500)]
    pub pull_resource_interval: u64,

    #[arg(long, default_value = "")]
    pub system_auth_mode: String,

    #[arg(long, default_value = "")]
    pub meta_store_excluded_keys: String,

    #[arg(long, default_value = "")]
    pub quota_config_file: String,
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde::Deserialize;

    #[derive(Debug, Deserialize, PartialEq)]
    struct SampleCfg {
        name: String,
        port: u16,
    }

    #[test]
    fn load_config_from_json_str_ok() {
        let s = r#"{"name":"node-a","port":8080}"#;
        let c: SampleCfg = load_config_from_json_str(s).expect("json");
        assert_eq!(
            c,
            SampleCfg {
                name: "node-a".into(),
                port: 8080
            }
        );
    }

    #[test]
    fn load_config_from_yaml_str_ok() {
        let s = "name: node-b\nport: 9090\n";
        let c: SampleCfg = load_config_from_yaml_str(s).expect("yaml");
        assert_eq!(
            c,
            SampleCfg {
                name: "node-b".into(),
                port: 9090
            }
        );
    }
}
