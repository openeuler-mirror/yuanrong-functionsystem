//! Port of `functionsystem/src/common/metadata/metadata_type.h` (core structs + shared service metadata types).

use serde::{Deserialize, Serialize};
use std::collections::HashMap;

pub const LOCAL_STORAGE_TYPE: &str = "local";
pub const S3_STORAGE_TYPE: &str = "s3";
pub const COPY_STORAGE_TYPE: &str = "copy";
pub const WORKING_DIR_STORAGE_TYPE: &str = "working_dir";
pub const SHARED_DIR_STORAGE_TYPE: &str = "shared_dir";
pub const DEPLOY_DIR: &str = "/dcache";

pub const RELIABILITY_TYPE: &str = "ReliabilityType";
pub const IDLE_TIMEOUT: &str = "idle_timeout";

/// One CPU core corresponds to 1000 in upstream conventions.
pub const DEFAULT_MIN_INSTANCE_CPU_SIZE: u64 = 300;
pub const DEFAULT_MAX_INSTANCE_CPU_SIZE: u64 = 16000;
pub const DEFAULT_MIN_INSTANCE_MEMORY_SIZE: u64 = 128;
pub const DEFAULT_MAX_INSTANCE_MEMORY_SIZE: u64 = 1024 * 1024 * 1024;

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct ProxyMeta {
    pub node: String,
    pub aid: String,
    pub ak: String,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct InstanceResource {
    pub cpu: String,
    pub memory: String,
    #[serde(default)]
    pub custom_resources: HashMap<String, String>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct InstanceLimitResource {
    #[serde(default = "default_min_cpu")]
    pub min_cpu: u64,
    #[serde(default = "default_min_memory")]
    pub min_memory: u64,
    #[serde(default = "default_max_cpu")]
    pub max_cpu: u64,
    #[serde(default = "default_max_memory")]
    pub max_memory: u64,
}

fn default_min_cpu() -> u64 {
    DEFAULT_MIN_INSTANCE_CPU_SIZE
}
fn default_max_cpu() -> u64 {
    DEFAULT_MAX_INSTANCE_CPU_SIZE
}
fn default_min_memory() -> u64 {
    DEFAULT_MIN_INSTANCE_MEMORY_SIZE
}
fn default_max_memory() -> u64 {
    DEFAULT_MAX_INSTANCE_MEMORY_SIZE
}

impl Default for InstanceLimitResource {
    fn default() -> Self {
        Self {
            min_cpu: DEFAULT_MIN_INSTANCE_CPU_SIZE,
            min_memory: DEFAULT_MIN_INSTANCE_MEMORY_SIZE,
            max_cpu: DEFAULT_MAX_INSTANCE_CPU_SIZE,
            max_memory: DEFAULT_MAX_INSTANCE_MEMORY_SIZE,
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct FuncMetaData {
    pub urn: String,
    /// Language / runtime label.
    pub runtime: String,
    pub handler: String,
    pub code_sha256: String,
    pub code_sha512: String,
    pub entry_file: String,
    #[serde(default)]
    pub hook_handler: HashMap<String, String>,
    pub name: String,
    pub version: String,
    #[serde(default, rename = "tenantId")]
    pub tenant_id: String,
    #[serde(default, rename = "revisionId")]
    pub revision_id: String,
    #[serde(default, rename = "isSystemFunc")]
    pub is_system_func: bool,
    #[serde(default)]
    pub timeout: u32,
    #[serde(default, rename = "staticHandler")]
    pub static_handler: String,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Layer {
    #[serde(default, rename = "appID")]
    pub app_id: String,
    #[serde(default, rename = "bucketID")]
    pub bucket_id: String,
    #[serde(default, rename = "objectID")]
    pub object_id: String,
    #[serde(default, rename = "bucketURL")]
    pub bucket_url: String,
    #[serde(default)]
    pub sha256: String,
    #[serde(default)]
    pub sha512: String,
    #[serde(default, rename = "hostName")]
    pub host_name: String,
    #[serde(default, rename = "securityToken")]
    pub security_token: String,
    #[serde(default, rename = "temporaryAccessKey")]
    pub temporary_access_key: String,
    #[serde(default, rename = "temporarySecretKey")]
    pub temporary_secret_key: String,
    #[serde(default, rename = "storageType")]
    pub storage_type: String,
    #[serde(default, rename = "codePath")]
    pub code_path: String,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct CodeMetaData {
    #[serde(default, rename = "storageType")]
    pub storage_type: String,
    #[serde(default, rename = "bucketID")]
    pub bucket_id: String,
    #[serde(default, rename = "objectID")]
    pub object_id: String,
    #[serde(default, rename = "bucketUrl")]
    pub bucket_url: String,
    #[serde(default)]
    pub layers: Vec<Layer>,
    #[serde(default, rename = "deployDir")]
    pub deploy_dir: String,
    #[serde(default)]
    pub sha512: String,
    #[serde(default, rename = "appId")]
    pub app_id: String,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct EnvMetaData {
    #[serde(default, rename = "envKey")]
    pub env_key: String,
    #[serde(default, rename = "envInfo")]
    pub env_info: String,
    #[serde(default, rename = "encryptedUserData")]
    pub encrypted_user_data: String,
    #[serde(default, rename = "cryptoAlgorithm")]
    pub crypto_algorithm: String,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct InstanceMetaData {
    #[serde(default, rename = "maxInstance")]
    pub max_instance: i32,
    #[serde(default, rename = "minInstance")]
    pub min_instance: i32,
    #[serde(default, rename = "concurrentNum")]
    pub concurrent_num: i32,
    #[serde(default, rename = "cacheInstance")]
    pub cache_instance: i32,
    #[serde(default, rename = "diskLimit")]
    pub disk_limit: i32,
    #[serde(default, rename = "scalePolicy")]
    pub scale_policy: String,
}

/// Key/value pair used when persisting instance or route blobs to the metastore.
#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct StoreInfo {
    pub key: String,
    pub value: String,
}

impl StoreInfo {
    pub fn new(key: impl Into<String>, value: impl Into<String>) -> Self {
        Self {
            key: key.into(),
            value: value.into(),
        }
    }
}

// ---- Types shared with `service_json` / extended metadata (same header) ----

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum RootfsSrcType {
    S3,
    Image,
    Local,
    #[serde(other)]
    Invalid,
}

impl Default for RootfsSrcType {
    fn default() -> Self {
        Self::Invalid
    }
}

impl RootfsSrcType {
    pub fn from_str(s: &str) -> Self {
        match s {
            "s3" => Self::S3,
            "image" => Self::Image,
            "local" => Self::Local,
            "invalid" => Self::Invalid,
            _ => Self::Invalid,
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct RootfsStorageInfo {
    pub endpoint: String,
    pub bucket: String,
    pub object: String,
    #[serde(default, rename = "accessKey")]
    pub access_key: String,
    #[serde(default, rename = "secretKey")]
    pub secret_key: String,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct RootfsSpecMeta {
    pub runtime: String,
    #[serde(default, rename = "type")]
    pub r#type: RootfsSrcType,
    #[serde(default)]
    pub readonly: bool,
    #[serde(default, rename = "storageInfo")]
    pub storage_info: RootfsStorageInfo,
    #[serde(default, rename = "imageurl")]
    pub image_url: String,
    #[serde(default)]
    pub path: String,
    #[serde(default)]
    pub mountpoint: String,
}

impl Default for RootfsSpecMeta {
    fn default() -> Self {
        Self {
            runtime: String::new(),
            r#type: RootfsSrcType::default(),
            readonly: false,
            storage_info: RootfsStorageInfo::default(),
            image_url: String::new(),
            path: String::new(),
            mountpoint: String::new(),
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
pub struct BootstrapMetaData {
    #[serde(default, rename = "type")]
    pub r#type: String,
    #[serde(default)]
    pub root: String,
    #[serde(default)]
    pub entrypoint: String,
    #[serde(default)]
    pub cmd: String,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct DeviceMetaData {
    #[serde(default)]
    pub hbm: f32,
    #[serde(default)]
    pub latency: f32,
    #[serde(default)]
    pub stream: u32,
    #[serde(default)]
    pub count: u32,
    #[serde(default)]
    pub model: String,
    #[serde(rename = "type", default)]
    pub device_type: String,
}

impl Default for DeviceMetaData {
    fn default() -> Self {
        Self {
            hbm: 0.0,
            latency: 0.0,
            stream: 0,
            count: 0,
            model: String::new(),
            device_type: String::new(),
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum WarmupType {
    #[default]
    None = 0,
    Seed = 1,
    Preload = 2,
    Invalid = 255,
}

impl Serialize for WarmupType {
    fn serialize<S: serde::Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        match self {
            Self::None => serializer.serialize_u8(0),
            Self::Seed => serializer.serialize_u8(1),
            Self::Preload => serializer.serialize_u8(2),
            Self::Invalid => serializer.serialize_u8(255),
        }
    }
}

impl<'de> Deserialize<'de> for WarmupType {
    fn deserialize<D: serde::Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        use serde::de::{self, Visitor};
        struct W;
        impl<'de> Visitor<'de> for W {
            type Value = WarmupType;
            fn expecting(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
                write!(f, "warmup enum as u8 or string")
            }
            fn visit_u64<E: de::Error>(self, v: u64) -> Result<WarmupType, E> {
                match v {
                    0 => Ok(WarmupType::None),
                    1 => Ok(WarmupType::Seed),
                    2 => Ok(WarmupType::Preload),
                    255 => Ok(WarmupType::Invalid),
                    _ => Ok(WarmupType::Invalid),
                }
            }
            fn visit_str<E: de::Error>(self, v: &str) -> Result<WarmupType, E> {
                Ok(WarmupType::from_str(v))
            }
        }
        deserializer.deserialize_any(W)
    }
}

impl WarmupType {
    pub fn from_str(s: &str) -> Self {
        match s {
            "seed" => Self::Seed,
            "preload" => Self::Preload,
            "none" => Self::None,
            _ => Self::Invalid,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn instance_metadata_roundtrip() {
        let m = InstanceMetaData {
            max_instance: 10,
            min_instance: 1,
            concurrent_num: 5,
            cache_instance: 2,
            disk_limit: 1024,
            scale_policy: "default".into(),
        };
        let j = serde_json::to_string(&m).unwrap();
        let back: InstanceMetaData = serde_json::from_str(&j).unwrap();
        assert_eq!(m, back);
    }

    #[test]
    fn func_metadata_hook_handler_map() {
        let mut h = HashMap::new();
        h.insert("init".into(), "pkg.mod::init".into());
        let f = FuncMetaData {
            hook_handler: h.clone(),
            name: "f1".into(),
            ..Default::default()
        };
        let j = serde_json::to_string(&f).unwrap();
        let back: FuncMetaData = serde_json::from_str(&j).unwrap();
        assert_eq!(back.hook_handler, h);
    }

    #[test]
    fn warmup_deserialize_string_and_int() {
        let a: WarmupType = serde_json::from_str("\"seed\"").unwrap();
        assert_eq!(a, WarmupType::Seed);
        let b: WarmupType = serde_json::from_str("0").unwrap();
        assert_eq!(b, WarmupType::None);
    }
}
