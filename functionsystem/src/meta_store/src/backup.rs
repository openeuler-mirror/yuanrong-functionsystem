//! Persistence to etcd under the metastore backup prefix (uses `etcd_client` for wire I/O).

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use parking_lot::Mutex;
use prost::Message;
use serde::Deserialize;
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
        if let Err(err) = self.tx.try_send(op) {
            let msg = match err {
                mpsc::error::TrySendError::Full(_) => "backup queue full".to_string(),
                mpsc::error::TrySendError::Closed(_) => "backup queue closed".to_string(),
            };
            self.health.set_err(msg);
        }
    }
}

fn backup_kv_key(prefix: &str, logical: &[u8]) -> String {
    yr_common::etcd_keys::with_prefix(prefix, &String::from_utf8_lossy(logical))
}

#[derive(Debug, Deserialize)]
struct PersistedLease {
    id: i64,
    ttl: i64,
}

#[derive(Debug, Default)]
pub(crate) struct LeaseRecoverySnapshot {
    pub(crate) leases: Vec<(i64, i64)>,
    pub(crate) revision: i64,
}

pub(crate) fn decode_persisted_lease(value: &[u8]) -> Result<(i64, i64), MetaStoreError> {
    let persisted: PersistedLease =
        serde_json::from_slice(value).map_err(|e| MetaStoreError::Backup(e.to_string()))?;
    Ok((persisted.id, persisted.ttl))
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

pub(crate) async fn recover_leases_snapshot_from_etcd(
    cfg: &MetaStoreServerConfig,
) -> Result<LeaseRecoverySnapshot, MetaStoreError> {
    if cfg.etcd_endpoints.is_empty() {
        return Ok(LeaseRecoverySnapshot::default());
    }
    let sl: Vec<&str> = cfg.etcd_endpoints.iter().map(|s| s.as_str()).collect();
    let mut client = etcd_client::Client::connect(&sl, None)
        .await
        .map_err(|e| MetaStoreError::Backup(e.to_string()))?;
    let resp = client
        .get(
            cfg.lease_backup_prefix.as_str(),
            Some(etcd_client::GetOptions::new().with_prefix()),
        )
        .await
        .map_err(|e| MetaStoreError::Backup(e.to_string()))?;

    let mut leases = Vec::new();
    for kv in resp.kvs() {
        leases.push(decode_persisted_lease(kv.value())?);
    }
    Ok(LeaseRecoverySnapshot {
        leases,
        revision: resp.header().map(|h| h.revision()).unwrap_or(0),
    })
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
            client.delete(key, None).await.map_err(|e| e.to_string())?;
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

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use async_trait::async_trait;
    use tokio::net::TcpListener;
    use tokio_stream::wrappers::TcpListenerStream;
    use tonic::{Request, Response, Status};
    use tonic::transport::Server;

    use super::*;
    use crate::MetaStoreServer;
    use crate::pb::etcdserverpb::kv_server::{Kv, KvServer};
    use crate::pb::etcdserverpb::{
        CompactionRequest, CompactionResponse, DeleteRangeRequest, DeleteRangeResponse,
        PutRequest, PutResponse, RangeRequest, RangeResponse, TxnRequest, TxnResponse,
    };

    async fn start_server() -> (std::net::SocketAddr, tokio::task::JoinHandle<()>) {
        let listener = TcpListener::bind("127.0.0.1:0").await.expect("bind");
        let addr = listener.local_addr().expect("addr");
        let mut cfg = MetaStoreServerConfig::default();
        cfg.listen_addr = addr.to_string();
        let server = MetaStoreServer::new(cfg).await.expect("MetaStoreServer::new");
        let h = tokio::spawn(async move {
            let _ = server.serve(listener).await;
        });
        tokio::time::sleep(Duration::from_millis(50)).await;
        (addr, h)
    }

    #[derive(Default)]
    struct FailingDeleteKv;

    #[async_trait]
    impl Kv for FailingDeleteKv {
        async fn range(
            &self,
            _request: Request<RangeRequest>,
        ) -> Result<Response<RangeResponse>, Status> {
            Err(Status::unimplemented("range"))
        }

        async fn put(
            &self,
            _request: Request<PutRequest>,
        ) -> Result<Response<PutResponse>, Status> {
            Err(Status::unimplemented("put"))
        }

        async fn delete_range(
            &self,
            _request: Request<DeleteRangeRequest>,
        ) -> Result<Response<DeleteRangeResponse>, Status> {
            Err(Status::internal("forced delete failure"))
        }

        async fn txn(
            &self,
            _request: Request<TxnRequest>,
        ) -> Result<Response<TxnResponse>, Status> {
            Err(Status::unimplemented("txn"))
        }

        async fn compact(
            &self,
            _request: Request<CompactionRequest>,
        ) -> Result<Response<CompactionResponse>, Status> {
            Err(Status::unimplemented("compact"))
        }
    }

    async fn start_delete_failing_kv() -> (std::net::SocketAddr, tokio::task::JoinHandle<()>) {
        let listener = TcpListener::bind("127.0.0.1:0").await.expect("bind");
        let addr = listener.local_addr().expect("addr");
        let h = tokio::spawn(async move {
            let incoming = TcpListenerStream::new(listener);
            let _ = Server::builder()
                .add_service(KvServer::new(FailingDeleteKv))
                .serve_with_incoming(incoming)
                .await;
        });
        tokio::time::sleep(Duration::from_millis(50)).await;
        (addr, h)
    }

    #[test]
    fn backup_handle_marks_health_unhealthy_when_channel_is_full() {
        let (tx, _rx) = mpsc::channel(1);
        let health = BackupHealth::new();
        let handle = BackupHandle {
            tx,
            health: health.clone(),
        };

        handle.try_send(BackupOp::LeasePut {
            lease_id: 1,
            ttl_secs: 30,
        });
        assert!(health.healthy());

        handle.try_send(BackupOp::LeaseDelete { lease_id: 1 });
        assert!(!health.healthy());
        assert_eq!(health.error_message().as_deref(), Some("backup queue full"));
    }

    #[tokio::test]
    async fn recover_leases_snapshot_returns_revision_and_leases() {
        let (addr, _h) = start_server().await;
        let cfg = MetaStoreServerConfig {
            etcd_endpoints: vec![format!("http://{addr}")],
            ..MetaStoreServerConfig::default()
        };
        let key = format!("{}/{}", cfg.lease_backup_prefix.trim_end_matches('/'), 77_i64);
        let client = etcd_client::Client::connect([format!("http://{addr}")], None)
            .await
            .expect("connect");
        client
            .kv_client()
            .put(key.as_bytes(), br#"{"id":77,"ttl":45}"#, None)
            .await
            .expect("put lease snapshot");

        let snapshot = recover_leases_snapshot_from_etcd(&cfg)
            .await
            .expect("recover snapshot");
        assert_eq!(snapshot.leases, vec![(77, 45)]);
        assert!(snapshot.revision > 0);
    }

    #[tokio::test]
    async fn lease_delete_apply_op_propagates_delete_errors() {
        let (addr, _h) = start_delete_failing_kv().await;
        let mut client = etcd_client::Client::connect([format!("http://{addr}")], None)
            .await
            .expect("connect");
        let cfg = MetaStoreServerConfig::default();

        let err = apply_op(&mut client, &cfg, BackupOp::LeaseDelete { lease_id: 9 })
            .await
            .expect_err("lease delete should propagate kv delete errors");
        assert!(err.contains("forced delete failure"), "unexpected error: {err}");
    }
}
