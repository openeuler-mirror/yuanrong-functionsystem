//! Port of `functionsystem/src/common/service_json/service_info.h` (JSON-facing service metadata).

use crate::metadata::{
    BootstrapMetaData, DeviceMetaData, RootfsSpecMeta, WarmupType,
};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;

pub const CUSTOM_LIMIT_CPU: i64 = 4000;
pub const CUSTOM_REQUEST_CPU: i64 = 0;
pub const CUSTOM_LIMIT_MEM: i64 = 16000;
pub const CUSTOM_REQUEST_MEM: i64 = 0;

pub const DEFAULT_MIN_INSTANCE: i32 = 0;
pub const DEFAULT_MAX_INSTANCE: i32 = 100;
pub const DEFAULT_CPU: i64 = 0;
pub const DEFAULT_MEMORY: i64 = 0;
pub const DEFAULT_CONCURRENT_NUM: i64 = 100;
pub const DEFAULT_TIME_OUT_MS: i64 = 900;

pub const ENV_LENGTH_LIMIT: i64 = 4 * (1 << 10);

pub const MAX_LAYERS_SIZE: i32 = 5;
pub const REFERENCE_LAYER_SPLIT_SIZE: i32 = 2;
pub const MAX_LAYER_VERSION: i32 = 1_000_000;
pub const MAX_MAX_INSTANCE: i64 = 1000;
pub const MAX_CONCURRENT_NUM: i32 = 100;

pub const DEFAULT_TENANT_ID: &str = "default";
pub const DEFAULT_STORAGE_TYPE: &str = "local";

pub const FAAS: &str = "faas";
pub const YR_LIB: &str = "yrlib";
pub const CUSTOM: &str = "custom";
pub const POSIX_RUNTIME_CUSTOM: &str = "posix-runtime-custom";

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FunctionHookHandlerConfig {
    #[serde(default)]
    pub init_handler: String,
    #[serde(default)]
    pub call_handler: String,
    #[serde(default)]
    pub checkpoint_handler: String,
    #[serde(default)]
    pub recover_handler: String,
    #[serde(default)]
    pub shutdown_handler: String,
    #[serde(default)]
    pub signal_handler: String,
    #[serde(default)]
    pub health_handler: String,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct FunctionConfig {
    #[serde(default = "default_min_instance")]
    pub min_instance: i32,
    #[serde(default = "default_max_instance")]
    pub max_instance: i32,
    #[serde(default = "default_concurrent_num")]
    pub concurrent_num: i32,
    #[serde(default)]
    pub handler: String,
    #[serde(default)]
    pub initializer: String,
    #[serde(default)]
    pub initializer_timeout: i32,
    #[serde(default)]
    pub prestop: String,
    #[serde(default)]
    pub pre_stop_timeout: i32,
    #[serde(default)]
    pub description: String,
    #[serde(default)]
    pub environment: HashMap<String, String>,
    #[serde(default)]
    pub encrypted_env_str: String,
    #[serde(default)]
    pub custom_resources: HashMap<String, String>,
    #[serde(default)]
    pub runtime: String,
    #[serde(default = "default_memory")]
    pub memory: i64,
    #[serde(default = "default_timeout")]
    pub timeout: i64,
    #[serde(default)]
    pub layers: Vec<String>,
    #[serde(default = "default_cpu")]
    pub cpu: i64,
    #[serde(default = "default_storage_type")]
    pub storage_type: String,
    #[serde(default)]
    pub code_path: String,
    #[serde(default)]
    pub cache_instance: i32,
    #[serde(default)]
    pub function_hook_handler_config: FunctionHookHandlerConfig,
    #[serde(default)]
    pub device: DeviceMetaData,
    #[serde(default)]
    pub warmup: WarmupType,
    #[serde(default)]
    pub rootfs: RootfsSpecMeta,
    #[serde(default)]
    pub bootstrap: BootstrapMetaData,
}

fn default_min_instance() -> i32 {
    DEFAULT_MIN_INSTANCE
}
fn default_max_instance() -> i32 {
    DEFAULT_MAX_INSTANCE
}
fn default_concurrent_num() -> i32 {
    DEFAULT_CONCURRENT_NUM as i32
}
fn default_memory() -> i64 {
    DEFAULT_MEMORY
}
fn default_timeout() -> i64 {
    DEFAULT_TIME_OUT_MS
}
fn default_cpu() -> i64 {
    DEFAULT_CPU
}
fn default_storage_type() -> String {
    DEFAULT_STORAGE_TYPE.to_string()
}

impl Default for FunctionConfig {
    fn default() -> Self {
        Self {
            min_instance: DEFAULT_MIN_INSTANCE,
            max_instance: DEFAULT_MAX_INSTANCE,
            concurrent_num: DEFAULT_CONCURRENT_NUM as i32,
            handler: String::new(),
            initializer: String::new(),
            initializer_timeout: 0,
            prestop: String::new(),
            pre_stop_timeout: 0,
            description: String::new(),
            environment: HashMap::new(),
            encrypted_env_str: String::new(),
            custom_resources: HashMap::new(),
            runtime: String::new(),
            memory: DEFAULT_MEMORY,
            timeout: DEFAULT_TIME_OUT_MS,
            layers: Vec::new(),
            cpu: DEFAULT_CPU,
            storage_type: DEFAULT_STORAGE_TYPE.to_string(),
            code_path: String::new(),
            cache_instance: 0,
            function_hook_handler_config: FunctionHookHandlerConfig::default(),
            device: DeviceMetaData::default(),
            warmup: WarmupType::None,
            rootfs: RootfsSpecMeta::default(),
            bootstrap: BootstrapMetaData::default(),
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ServiceInfo {
    #[serde(default)]
    pub service: String,
    #[serde(default)]
    pub kind: String,
    #[serde(default)]
    pub description: String,
    #[serde(default)]
    pub functions: HashMap<String, FunctionConfig>,
}

/// Parse service metadata JSON into [`ServiceInfo`].
pub fn parse_service_info_json(s: &str) -> Result<ServiceInfo, serde_json::Error> {
    serde_json::from_str(s)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_minimal_service_info() {
        let j = r#"{
            "service": "demo",
            "kind": "faas",
            "description": "d",
            "functions": {
                "hello": {
                    "handler": "pkg.mod::run",
                    "runtime": "python3",
                    "warmup": "seed"
                }
            }
        }"#;
        let s = parse_service_info_json(j).unwrap();
        assert_eq!(s.service, "demo");
        assert_eq!(s.kind, FAAS);
        let f = s.functions.get("hello").unwrap();
        assert_eq!(f.handler, "pkg.mod::run");
        assert_eq!(f.warmup, WarmupType::Seed);
        assert_eq!(f.max_instance, DEFAULT_MAX_INSTANCE);
    }

    #[test]
    fn hook_handler_config_roundtrip() {
        let c = FunctionHookHandlerConfig {
            init_handler: "a".into(),
            health_handler: "b".into(),
            ..Default::default()
        };
        let j = serde_json::to_string(&c).unwrap();
        let back: FunctionHookHandlerConfig = serde_json::from_str(&j).unwrap();
        assert_eq!(c, back);
    }
}
