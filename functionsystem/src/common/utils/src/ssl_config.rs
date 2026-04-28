use std::collections::BTreeMap;
use std::path::Path;

use crate::error::{YrError, YrResult};

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct SslInputs {
    pub ssl_enable: bool,
    pub metrics_ssl_enable: bool,
    pub ssl_base_path: String,
    pub ssl_root_file: String,
    pub ssl_cert_file: String,
    pub ssl_key_file: String,
}

impl SslInputs {
    pub fn from_flag_strings(
        ssl_enable: &str,
        metrics_ssl_enable: &str,
        ssl_base_path: &str,
        ssl_root_file: &str,
        ssl_cert_file: &str,
        ssl_key_file: &str,
    ) -> Self {
        Self {
            ssl_enable: parse_cpp_bool(ssl_enable),
            metrics_ssl_enable: parse_cpp_bool(metrics_ssl_enable),
            ssl_base_path: ssl_base_path.to_string(),
            ssl_root_file: ssl_root_file.to_string(),
            ssl_cert_file: ssl_cert_file.to_string(),
            ssl_key_file: ssl_key_file.to_string(),
        }
    }
}

fn parse_cpp_bool(value: &str) -> bool {
    matches!(
        value.trim().to_ascii_lowercase().as_str(),
        "true" | "1" | "yes" | "y" | "on"
    )
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct SslCertConfig {
    pub is_enable: bool,
    pub is_metrics_ssl_enable: bool,
    pub cert_path: String,
    pub root_cert_file: String,
    pub cert_file: String,
    pub key_file: String,
}

pub fn get_real_path(path: impl AsRef<Path>) -> String {
    std::fs::canonicalize(path.as_ref())
        .ok()
        .map(|p| p.to_string_lossy().into_owned())
        .unwrap_or_default()
}

fn cpp_cert_path(cert_path: &str, file: &str) -> String {
    format!("{cert_path}/{file}")
}

pub fn get_ssl_cert_config(inputs: &SslInputs) -> SslCertConfig {
    let mut cfg = SslCertConfig::default();
    if !inputs.ssl_enable && !inputs.metrics_ssl_enable {
        return cfg;
    }

    cfg.cert_path = get_real_path(&inputs.ssl_base_path);
    cfg.root_cert_file = get_real_path(cpp_cert_path(&cfg.cert_path, &inputs.ssl_root_file));
    cfg.cert_file = get_real_path(cpp_cert_path(&cfg.cert_path, &inputs.ssl_cert_file));
    cfg.key_file = get_real_path(cpp_cert_path(&cfg.cert_path, &inputs.ssl_key_file));

    if !Path::new(&cfg.root_cert_file).exists()
        || !Path::new(&cfg.cert_file).exists()
        || !Path::new(&cfg.key_file).exists()
    {
        return cfg;
    }

    cfg.is_enable = inputs.ssl_enable;
    cfg.is_metrics_ssl_enable = inputs.metrics_ssl_enable;
    cfg
}

pub fn litebus_ssl_envs(cfg: &SslCertConfig) -> YrResult<BTreeMap<String, String>> {
    if !cfg.is_enable {
        return Err(YrError::Config(
            "ssl_enable is false or ssl certificate files are invalid".into(),
        ));
    }
    Ok(BTreeMap::from([
        ("LITEBUS_SSL_ENABLED".into(), "1".into()),
        ("LITEBUS_SSL_VERIFY_CERT".into(), "1".into()),
        ("LITEBUS_SSL_DECRYPT_TYPE".into(), "0".into()),
        ("LITEBUS_SSL_CA_FILE".into(), cfg.root_cert_file.clone()),
        ("LITEBUS_SSL_CA_DIR".into(), cfg.cert_path.clone()),
        ("LITEBUS_SSL_CERT_FILE".into(), cfg.cert_file.clone()),
        ("LITEBUS_SSL_KEY_FILE".into(), cfg.key_file.clone()),
    ]))
}

pub fn apply_litebus_ssl_envs(cfg: &SslCertConfig) -> YrResult<()> {
    for (key, value) in litebus_ssl_envs(cfg)? {
        // Matches C++ startup ordering: called before worker tasks/servers are spawned.
        std::env::set_var(key, value);
    }
    Ok(())
}
