use serde::{Deserialize, Serialize};

/// Default backup prefix (C++ `META_STORE_BACKUP_KV_PREFIX`).
pub const DEFAULT_KV_BACKUP_PREFIX: &str = "/metastore/kv/";
/// Lease metadata under etcd (C++ metastore lease backup path).
pub const DEFAULT_LEASE_BACKUP_PREFIX: &str = "/metastore/lease/";

/// Max serialized backup body per etcd txn (C++ `ETCD_BODY_LIMIT` scale).
pub const DEFAULT_BACKUP_MAX_BATCH_BYTES: usize = 1024 * 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize, Default)]
pub enum MetaStoreRole {
    #[default]
    Master,
    Slave,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MetaStoreServerConfig {
    pub listen_addr: String,
    /// When empty, backup / recover / slave watch are disabled.
    pub etcd_endpoints: Vec<String>,
    pub role: MetaStoreRole,
    pub kv_backup_prefix: String,
    pub lease_backup_prefix: String,
    pub backup_max_batch_bytes: usize,
    pub backup_max_concurrent_flushes: usize,
    pub backup_max_ops_per_txn: usize,
    pub cluster_id: u64,
    pub member_id: u64,
    /// When set, master periodically snapshots full KV state to this path (bincode).
    /// If `etcd_endpoints` is empty and the file exists, it is loaded at startup.
    pub local_snapshot_path: Option<String>,
}

impl Default for MetaStoreServerConfig {
    fn default() -> Self {
        Self {
            listen_addr: "127.0.0.1:23790".to_string(),
            etcd_endpoints: Vec::new(),
            role: MetaStoreRole::Master,
            kv_backup_prefix: DEFAULT_KV_BACKUP_PREFIX.to_string(),
            lease_backup_prefix: DEFAULT_LEASE_BACKUP_PREFIX.to_string(),
            backup_max_batch_bytes: DEFAULT_BACKUP_MAX_BATCH_BYTES,
            backup_max_concurrent_flushes: 4,
            backup_max_ops_per_txn: 128,
            cluster_id: 1,
            member_id: 1,
            local_snapshot_path: None,
        }
    }
}
