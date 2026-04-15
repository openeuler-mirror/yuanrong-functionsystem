//! Client configuration and TLS placeholders (tonic/etcd TLS wiring TBD).

/// TLS material for etcd / MetaStore gRPC (optional).
#[derive(Debug, Clone, Default)]
pub struct SslConfig {
    pub ca_certificate: Option<Vec<u8>>,
    pub client_certificate: Option<Vec<u8>>,
    pub client_key: Option<Vec<u8>>,
    pub server_name: Option<String>,
}

/// Controls direct etcd vs MetaStore gRPC routing (C++ `MetaStoreClient` parity).
#[derive(Debug, Clone)]
pub struct MetaStoreClientConfig {
    /// When true, KV / watch / lease (except routing exceptions) go through `meta_store_address`.
    pub enable_meta_store: bool,
    /// When true with `enable_meta_store`, election is also routed via MetaStore gRPC (server must implement it).
    pub is_passthrough: bool,
    /// Comma-separated etcd endpoints, e.g. `127.0.0.1:2379,127.0.0.1:2479`.
    pub etcd_address: String,
    /// MetaStore server gRPC URL, e.g. `http://127.0.0.1:9600`.
    pub meta_store_address: String,
    /// Prepended to logical keys (C++ `etcd_table_prefix` / `GetKeyWithPrefix`).
    pub etcd_table_prefix: String,
    /// Logical key prefixes that always use direct etcd even when `enable_meta_store` is true.
    pub excluded_keys: Vec<String>,
    pub ssl_config: Option<SslConfig>,
}

impl Default for MetaStoreClientConfig {
    fn default() -> Self {
        Self {
            enable_meta_store: false,
            is_passthrough: false,
            etcd_address: String::new(),
            meta_store_address: String::new(),
            etcd_table_prefix: String::new(),
            excluded_keys: Vec::new(),
            ssl_config: None,
        }
    }
}

impl MetaStoreClientConfig {
    pub fn etcd_endpoints(&self) -> Vec<String> {
        self.etcd_address
            .split(',')
            .map(|s| s.trim().to_string())
            .filter(|s| !s.is_empty())
            .collect()
    }

    /// Direct etcd only (legacy `MetaStoreClient::connect` behavior).
    pub fn direct_etcd(endpoints_csv: impl Into<String>, etcd_table_prefix: impl Into<String>) -> Self {
        Self {
            enable_meta_store: false,
            is_passthrough: false,
            etcd_address: endpoints_csv.into(),
            meta_store_address: String::new(),
            etcd_table_prefix: etcd_table_prefix.into(),
            excluded_keys: Vec::new(),
            ssl_config: None,
        }
    }
}
