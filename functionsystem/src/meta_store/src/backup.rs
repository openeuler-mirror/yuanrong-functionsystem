//! Persistence to etcd under the metastore backup prefix (uses `etcd_client` for wire I/O).

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use parking_lot::Mutex;
use prost::Message;
use tokio::sync::mpsc;
use tracing::error;

use crate::config::MetaStoreServerConfig;
use crate::error::MetaStoreError;
use crate::pb::mvccpb::KeyValue;

#[derive(Debug)]
pub enum BackupOp {
    PutKv {
        logical_key: Vec<u8>,
        kv: KeyValue,
    },
    DeleteKey {
        logical_key: Vec<u8>,
    },
    LeasePut {
        lease_id: i64,
        ttl_secs: i64,
    },
    LeaseDelete {
        lease_id: i64,
    },
}

#[derive(Clone)]
pub struct BackupHealth {
    ok: Arc<AtomicBool>,
    err: Arc<Mutex<Option<String>>>,
}

impl BackupHealth {
    pub fn new() -> Self {
        Self {
            ok: Arc::new(AtomicBool::new(true)),
            err: Arc::new(Mutex::new(None)),
        }
    }

    pub fn set_err(&self, msg: String) {
        self.ok.store(false, Ordering::SeqCst);
        *self.err.lock() = Some(msg);
    }

    pub fn clear_err(&self) {
        self.ok.store(true, Ordering::SeqCst);
        *self.err.lock() = None;
    }

    pub fn healthy(&self) -> bool {
        self.ok.load(Ordering::SeqCst)
    }

    pub fn error_message(&self) -> Option<String> {
        self.err.lock().clone()
    }
}

#[derive(Clone)]
pub struct BackupHandle {
    tx: mpsc::Sender<BackupOp>,
    pub health: BackupHealth,
}

impl BackupHandle {
    pub fn try_send(&self, op: BackupOp) {
        let _ = self.tx.try_send(op);
    }
}

fn backup_kv_key(prefix: &str, logical: &[u8]) -> String {
    yr_common::etcd_keys::with_prefix(prefix, &String::from_utf8_lossy(logical))
}

pub async fn recover_from_etcd(cfg: &MetaStoreServerConfig) -> Result<crate::kv_store::KvState, MetaStoreError> {
    if cfg.etcd_endpoints.is_empty() {
        return Ok(crate::kv_store::KvState::default());
    }
    let sl: Vec<&str> = cfg.etcd_endpoints.iter().map(|s| s.as_str()).collect();
    let mut client = etcd_client::Client::connect(&sl, None)
        .await
        .map_err(|e| MetaStoreError::Backup(e.to_string()))?;
    let resp = client
        .get(
            cfg.kv_backup_prefix.as_str(),
            Some(etcd_client::GetOptions::new().with_prefix()),
        )
        .await
        .map_err(|e| MetaStoreError::Backup(e.to_string()))?;

    let mut st = crate::kv_store::KvState::default();
    for kv in resp.kvs() {
        let key_str = kv.key_str().map_err(|e| MetaStoreError::Backup(e.to_string()))?;
        let Some(lk) = key_str.strip_prefix(&cfg.kv_backup_prefix) else {
            continue;
        };
        let pkv = KeyValue::decode(kv.value())?;
        let key_bytes = lk.as_bytes().to_vec();
        st.cache.insert(
            key_bytes,
            crate::kv_store::ValueEntry {
                value: pkv.value.clone(),
                create_rev: pkv.create_revision,
                mod_rev: pkv.mod_revision,
                version: pkv.version,
                lease: pkv.lease,
            },
        );
        st.revision = st.revision.max(pkv.mod_revision);
    }
    Ok(st)
}

async fn apply_op(
    client: &mut etcd_client::Client,
    cfg: &MetaStoreServerConfig,
    op: BackupOp,
) -> Result<(), String> {
    match op {
        BackupOp::PutKv { logical_key, kv } => {
            let key = backup_kv_key(&cfg.kv_backup_prefix, &logical_key);
            let mut buf = Vec::new();
            kv.encode(&mut buf).map_err(|e| e.to_string())?;
            client
                .put(key, buf, None)
                .await
                .map_err(|e| e.to_string())?;
        }
        BackupOp::DeleteKey { logical_key } => {
            let key = backup_kv_key(&cfg.kv_backup_prefix, &logical_key);
            client.delete(key, None).await.map_err(|e| e.to_string())?;
        }
        BackupOp::LeasePut { lease_id, ttl_secs } => {
            let key = format!("{}{}", cfg.lease_backup_prefix, lease_id);
            let payload = format!(r#"{{"id":{lease_id},"ttl":{ttl_secs}}}"#);
            client
                .put(key, payload, None)
                .await
                .map_err(|e| e.to_string())?;
        }
        BackupOp::LeaseDelete { lease_id } => {
            let key = format!("{}{}", cfg.lease_backup_prefix, lease_id);
            let _ = client.delete(key, None).await;
        }
    }
    Ok(())
}

async fn backup_loop(cfg: MetaStoreServerConfig, mut rx: mpsc::Receiver<BackupOp>, health: BackupHealth) {
    let sl: Vec<&str> = cfg.etcd_endpoints.iter().map(|s| s.as_str()).collect();
    let mut client = match etcd_client::Client::connect(&sl, None).await {
        Ok(c) => c,
        Err(e) => {
            health.set_err(format!("backup connect: {e}"));
            return;
        }
    };

    while let Some(op) = rx.recv().await {
        match apply_op(&mut client, &cfg, op).await {
            Ok(()) => health.clear_err(),
            Err(e) => {
                error!(%e, "backup op failed");
                health.set_err(e);
                if let Ok(c2) = etcd_client::Client::connect(&sl, None).await {
                    client = c2;
                }
            }
        }
    }
}

pub fn start_backup(cfg: &MetaStoreServerConfig) -> Option<BackupHandle> {
    if cfg.etcd_endpoints.is_empty() {
        return None;
    }
    let (tx, rx) = mpsc::channel(4096);
    let health = BackupHealth::new();
    let cfg2 = cfg.clone();
    let h2 = health.clone();
    tokio::spawn(async move {
        backup_loop(cfg2, rx, h2).await;
    });
    Some(BackupHandle { tx, health })
}
