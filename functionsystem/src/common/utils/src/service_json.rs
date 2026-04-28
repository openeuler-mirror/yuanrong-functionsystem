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
pub const POSIX_CUSTOM_RUNTIME_VERSION: &str = "posix-custom-runtime";

const SERVICE_NAME_MAX_LEN: usize = 16;
const DEFAULT_HANDLER_MAX_LENGTH: usize = 64;
const CPP_HANDLER_MAX_LENGTH: usize = 256;
const JAVA_HANDLER_MAX_LENGTH: usize = 256;

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

pub fn validate_service_infos(infos: &[ServiceInfo]) -> Result<(), String> {
    for info in infos {
        validate_service_info(info)?;
    }
    Ok(())
}

pub fn validate_service_info(info: &ServiceInfo) -> Result<(), String> {
    check_service_name(&info.service)?;
    check_kind(&info.kind)?;
    for (name, cfg) in &info.functions {
        check_function_name(name)?;
        validate_function_config(cfg)?;
    }
    Ok(())
}

fn validate_function_config(cfg: &FunctionConfig) -> Result<(), String> {
    check_runtime(&cfg.runtime)?;
    check_cpu_memory(cfg.cpu, cfg.memory)?;
    check_env(&cfg.environment)?;
    check_layers(&cfg.layers)?;
    check_worker(cfg)?;
    check_hook_handlers(&cfg.function_hook_handler_config, &cfg.runtime)?;
    Ok(())
}

fn check_service_name(name: &str) -> Result<(), String> {
    if name.len() > SERVICE_NAME_MAX_LEN {
        return Err(format!("service name too long: {name}"));
    }
    if !is_service_name(name) {
        return Err(format!("invalid service name: {name}"));
    }
    Ok(())
}

fn check_kind(kind: &str) -> Result<(), String> {
    match kind {
        FAAS | YR_LIB | CUSTOM | POSIX_RUNTIME_CUSTOM => Ok(()),
        _ => Err(format!("unsupported kind: {kind}")),
    }
}

fn check_function_name(name: &str) -> Result<(), String> {
    if is_function_name(name) {
        Ok(())
    } else {
        Err(format!("invalid function name: {name}"))
    }
}

fn check_runtime(runtime: &str) -> Result<(), String> {
    match runtime {
        "cpp11" | "java1.8" | "java11" | "python" | "python3" | "python3.6"
        | "python3.7" | "python3.8" | "python3.9" | "python3.10" | "python3.11"
        | "go1.13" | POSIX_CUSTOM_RUNTIME_VERSION => Ok(()),
        _ => Err(format!("unsupported runtime: {runtime}")),
    }
}

fn check_cpu_memory(cpu: i64, memory: i64) -> Result<(), String> {
    if !(CUSTOM_REQUEST_CPU..=CUSTOM_LIMIT_CPU).contains(&cpu) {
        return Err(format!("cpu out of range: {cpu}"));
    }
    if !(CUSTOM_REQUEST_MEM..=CUSTOM_LIMIT_MEM).contains(&memory) {
        return Err(format!("memory out of range: {memory}"));
    }
    Ok(())
}

fn check_env(envs: &HashMap<String, String>) -> Result<(), String> {
    const RESERVED: &[&str] = &[
        "FAAS_FUNCTION_NAME",
        "FAAS_FUNCTION_VERSION",
        "FAAS_FUNCTION_BUSINESS",
        "FAAS_FUNCTION_TENANTID",
        "FAAS_FUNCTION_USER_FILE_PATH",
        "FAAS_FUNCTION_USER_PATH_LIMITS",
        "FAAS_FUNCTION_DEPLOY_DIR",
        "FAAS_LAYER_DEPLOY_DIR",
        "FAAS_FUNCTION_TIMEOUT",
        "FAAS_FUNCTION_MEMORY",
        "FAAS_FUNCTION_REGION",
        "FAAS_FUNCTION_TIMEZONE",
        "FAAS_FUNCTION_LANGUAGE",
        "FAAS_FUNCTION_LD_LIBRARY_PATH",
        "FAAS_FUNCTION_NODE_PATH",
        "FAAS_FUNCTION_PYTHON_PATH",
        "FAAS_FUNCTION_JAVA_PATH",
    ];
    let mut size = 0usize;
    for (k, v) in envs {
        if RESERVED.contains(&k.as_str()) {
            return Err(format!("reserved env: {k}"));
        }
        size += k.len() + v.len();
        if size > ENV_LENGTH_LIMIT as usize {
            return Err(format!("env size exceeds {ENV_LENGTH_LIMIT}"));
        }
    }
    Ok(())
}

fn check_layers(layers: &[String]) -> Result<(), String> {
    if layers.len() > MAX_LAYERS_SIZE as usize {
        return Err(format!("too many layers: {}", layers.len()));
    }
    for layer in layers {
        let Some((name, version)) = layer.split_once(':') else {
            return Err(format!("invalid layer ref: {layer}"));
        };
        if !is_layer_name(name) {
            return Err(format!("invalid layer name: {name}"));
        }
        let n = version
            .parse::<i32>()
            .map_err(|_| format!("invalid layer version: {version}"))?;
        if !(1..=MAX_LAYER_VERSION).contains(&n) {
            return Err(format!("layer version out of range: {version}"));
        }
    }
    Ok(())
}

fn check_worker(cfg: &FunctionConfig) -> Result<(), String> {
    if cfg.min_instance < 0 {
        return Err("minInstance must be at least 0".into());
    }
    if cfg.max_instance < 1 || cfg.max_instance as i64 > MAX_MAX_INSTANCE {
        return Err(format!("maxInstance out of range: {}", cfg.max_instance));
    }
    if cfg.min_instance > cfg.max_instance {
        return Err(format!(
            "minInstance {} is greater than maxInstance {}",
            cfg.min_instance, cfg.max_instance
        ));
    }
    if cfg.concurrent_num < 1 || cfg.concurrent_num > MAX_CONCURRENT_NUM {
        return Err(format!(
            "concurrentNum out of range: {}",
            cfg.concurrent_num
        ));
    }
    Ok(())
}

fn check_hook_handlers(cfg: &FunctionHookHandlerConfig, runtime: &str) -> Result<(), String> {
    // Mirror C++ 0.8 source behavior in `CheckHookHandler`, including its
    // checkpoint+recover rejection despite the upstream log text wording.
    if !cfg.checkpoint_handler.is_empty() && !cfg.recover_handler.is_empty() {
        return Err("checkpoint and recover hook combination rejected by C++ 0.8".into());
    }
    let handlers = [
        cfg.init_handler.as_str(),
        cfg.call_handler.as_str(),
        cfg.checkpoint_handler.as_str(),
        cfg.recover_handler.as_str(),
        cfg.shutdown_handler.as_str(),
        cfg.signal_handler.as_str(),
        cfg.health_handler.as_str(),
    ];
    for handler in handlers.into_iter().filter(|h| !h.is_empty()) {
        check_hook_handler(handler, runtime)?;
    }
    Ok(())
}

fn check_hook_handler(handler: &str, runtime: &str) -> Result<(), String> {
    let (max_len, kind) = match runtime {
        "cpp11" => (CPP_HANDLER_MAX_LENGTH, "cpp"),
        "python" | "python3" | "python3.7" | "python3.8" | "python3.9" | "python3.10"
        | "python3.11" | "go1.13" => (DEFAULT_HANDLER_MAX_LENGTH, "default"),
        "java1.8" => (JAVA_HANDLER_MAX_LENGTH, "java"),
        _ => return Err(format!("handler unsupported for runtime: {runtime}")),
    };
    if handler.len() > max_len {
        return Err(format!("handler too long: {handler}"));
    }
    let ok = match kind {
        "cpp" => true,
        "java" => is_java_handler(handler),
        _ => is_default_handler(handler),
    };
    if ok {
        Ok(())
    } else {
        Err(format!("invalid handler: {handler}"))
    }
}

fn is_service_name(name: &str) -> bool {
    !name.is_empty() && name.bytes().all(|b| b.is_ascii_lowercase() || b.is_ascii_digit())
}

fn is_function_name(name: &str) -> bool {
    let b = name.as_bytes();
    if b.is_empty() || !b[0].is_ascii_lowercase() {
        return false;
    }
    if b.len() == 1 {
        return true;
    }
    let last = *b.last().unwrap();
    (last.is_ascii_lowercase() || last.is_ascii_digit())
        && b[1..b.len() - 1]
            .iter()
            .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || *c == b'-')
        && b.len() <= 128
}

fn is_layer_name(name: &str) -> bool {
    let b = name.as_bytes();
    if b.len() < 2 || !b[0].is_ascii_lowercase() {
        return false;
    }
    let last = *b.last().unwrap();
    (last.is_ascii_lowercase() || last.is_ascii_digit())
        && b[1..b.len() - 1]
            .iter()
            .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || *c == b'-')
        && b.len() <= 32
}

fn is_default_handler(handler: &str) -> bool {
    let Some((module, func)) = handler.split_once('.') else {
        return false;
    };
    !module.is_empty()
        && !func.is_empty()
        && module.bytes().all(is_handler_char)
        && func.bytes().all(is_handler_char)
}

fn is_handler_char(b: u8) -> bool {
    b.is_ascii_alphanumeric() || b == b'-' || b == b'_'
}

fn is_java_identifier(s: &str) -> bool {
    let b = s.as_bytes();
    if b.is_empty() || b.len() > 24 || !(b[0].is_ascii_alphabetic() || b[0] == b'_') {
        return false;
    }
    b[1..]
        .iter()
        .all(|c| c.is_ascii_alphanumeric() || *c == b'_')
}

fn is_java_handler(handler: &str) -> bool {
    let (class_path, method) = handler
        .split_once("::")
        .map_or((handler, None), |(c, m)| (c, Some(m)));
    if method.is_some_and(|m| !is_java_identifier(m)) {
        return false;
    }
    let parts: Vec<_> = class_path.split('.').collect();
    !parts.is_empty() && parts.len() <= 9 && parts.into_iter().all(is_java_identifier)
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
    fn validate_service_info_accepts_cpp_compatible_metadata() {
        let svc = ServiceInfo {
            service: "demo".into(),
            kind: YR_LIB.into(),
            functions: HashMap::from([(
                "hello".into(),
                FunctionConfig {
                    runtime: "python3".into(),
                    handler: "pkg.run".into(),
                    cpu: 1000,
                    memory: 1000,
                    layers: vec!["base:1".into()],
                    ..Default::default()
                },
            )]),
            ..Default::default()
        };

        validate_service_info(&svc).expect("valid C++ metadata");
    }

    #[test]
    fn validate_service_info_rejects_invalid_service_runtime_env_and_layers() {
        let mut svc = ServiceInfo {
            service: "Demo".into(),
            kind: YR_LIB.into(),
            functions: HashMap::from([(
                "hello".into(),
                FunctionConfig {
                    runtime: "python4".into(),
                    environment: HashMap::from([(
                        "FAAS_FUNCTION_NAME".into(),
                        "reserved".into(),
                    )]),
                    layers: vec!["bad-layer".into()],
                    ..Default::default()
                },
            )]),
            ..Default::default()
        };
        assert!(validate_service_info(&svc).is_err());

        svc.service = "demo".into();
        assert!(validate_service_info(&svc).is_err());
    }

    #[test]
    fn validate_service_info_rejects_worker_and_handler_limits() {
        let svc = ServiceInfo {
            service: "demo".into(),
            kind: YR_LIB.into(),
            functions: HashMap::from([(
                "bad-name-".into(),
                FunctionConfig {
                    runtime: "python3".into(),
                    min_instance: 2,
                    max_instance: 1,
                    concurrent_num: 101,
                    function_hook_handler_config: FunctionHookHandlerConfig {
                        init_handler: "not_a_python_handler".into(),
                        ..Default::default()
                    },
                    ..Default::default()
                },
            )]),
            ..Default::default()
        };

        assert!(validate_service_info(&svc).is_err());
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
