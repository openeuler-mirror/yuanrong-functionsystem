//! gRPC services: etcd v3-compatible KV, Watch, Lease, Maintenance (status).

use std::pin::Pin;
use std::sync::atomic::{AtomicI64, Ordering};
use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use futures::Stream;
use prost::Message;
use tokio::sync::mpsc;
use tokio_stream::wrappers::{ReceiverStream, TcpListenerStream};
use tonic::transport::Server;
use tonic::{Request, Response, Status, Streaming};

use crate::backup::{
    recover_from_etcd, recover_leases_snapshot_from_etcd, start_backup, BackupHandle, BackupOp,
};
use crate::config::{MetaStoreRole, MetaStoreServerConfig};
use crate::error::MetaStoreError;
use crate::kv_store::KvStore;
use crate::lease_service::LeaseService;
use crate::lease_validator::LeaseValidator;
use crate::pb::etcdserverpb::lease_server::{Lease, LeaseServer};
use crate::pb::etcdserverpb::maintenance_server::{Maintenance, MaintenanceServer};
use crate::pb::etcdserverpb::watch_request::RequestUnion;
use crate::pb::etcdserverpb::watch_server::{Watch, WatchServer};
use crate::pb::etcdserverpb::{
    AlarmRequest, AlarmResponse, CompactionRequest, CompactionResponse, DefragmentRequest,
    DefragmentResponse, DeleteRangeRequest, HashKvRequest, HashKvResponse, HashRequest, HashResponse,
    LeaseGrantRequest, LeaseGrantResponse, LeaseKeepAliveRequest, LeaseKeepAliveResponse,
    LeaseLeasesRequest, LeaseLeasesResponse, LeaseRevokeRequest, LeaseRevokeResponse,
    LeaseTimeToLiveRequest, LeaseTimeToLiveResponse, MoveLeaderRequest, MoveLeaderResponse,
    PutRequest, PutResponse, RangeRequest, RangeResponse, SnapshotRequest, SnapshotResponse,
    StatusRequest, StatusResponse, TxnRequest, TxnResponse, WatchCancelRequest, WatchRequest,
    WatchResponse,
};
use crate::pb::etcdserverpb::{kv_server::{Kv, KvServer}, ResponseHeader};
use crate::pb::mvccpb::KeyValue;
use crate::watch_service::WatchHub;
use tracing::warn;

/// Embedded MetaStore: in-memory KV + revision + watch + lease + optional etcd backup.
#[derive(Clone)]
pub struct MetaStoreServer {
    pub(crate) inner: Arc<Inner>,
}

pub(crate) struct Inner {
    pub(crate) kv: Arc<KvStore>,
    pub(crate) watch: WatchHub,
    pub(crate) lease: LeaseService,
    pub(crate) lease_dyn: Arc<dyn LeaseValidator>,
    pub(crate) backup: Option<BackupHandle>,
    pub(crate) cfg: MetaStoreServerConfig,
    pub(crate) next_watch_id: Arc<AtomicI64>,
}

fn should_resync_after_idle_timeout(idle_timeouts: u64, idle_timeout_secs: u64) -> bool {
    idle_timeout_secs > 0 && idle_timeouts >= idle_timeout_secs
}

impl MetaStoreServer {
    pub async fn new(cfg: MetaStoreServerConfig) -> Result<Self, MetaStoreError> {
        let backup = start_backup(&cfg);
        let kv = Arc::new(KvStore::new(cfg.clone()));
        let recovered = recover_from_etcd(&cfg).await?;
        let mut st = recovered;
        if cfg.etcd_endpoints.is_empty() {
            if let Some(ref p) = cfg.local_snapshot_path {
                let path = std::path::Path::new(p);
                if path.exists() {
                    if let Ok(loaded) = crate::snapshot_file::load_kv_state(path) {
                        st = loaded;
                    }
                }
            }
        }
        kv.set_state(st).await;

        let watch = WatchHub::new();
        let lease = LeaseService::new(backup.clone(), cfg.role);
        let recovered_leases = recover_leases_snapshot_from_etcd(&cfg).await?;
        lease.sync_backup_snapshot(&recovered_leases.leases).await;
        let lease_dyn: Arc<dyn LeaseValidator> = Arc::new(lease.clone());
        lease.clone().spawn_expiry(kv.clone(), watch.clone(), cfg.clone());

        let slave_sync = cfg.role == MetaStoreRole::Slave && !cfg.etcd_endpoints.is_empty();
        let s = Self {
            inner: Arc::new(Inner {
                kv,
                watch,
                lease,
                lease_dyn,
                backup,
                cfg,
                next_watch_id: Arc::new(AtomicI64::new(1)),
            }),
        };

        if slave_sync {
            s.spawn_slave_watcher();
            s.spawn_slave_lease_watcher(recovered_leases.revision);
        }

        if s.inner.cfg.role == MetaStoreRole::Master {
            if let Some(ref p) = s.inner.cfg.local_snapshot_path.clone() {
                let kv_snap = s.inner.kv.clone();
                let path = std::path::PathBuf::from(p);
                tokio::spawn(async move {
                    let mut interval =
                        tokio::time::interval(std::time::Duration::from_secs(5));
                    loop {
                        interval.tick().await;
                        let snapshot = kv_snap.get_state().await;
                        let _ = crate::snapshot_file::save_kv_state(&path, &snapshot);
                    }
                });
            }
        }

        Ok(s)
    }

    fn spawn_slave_watcher(&self) {
        let inner = self.inner.clone();
        let cfg = self.inner.cfg.clone();
        tokio::spawn(async move {
            use etcd_client::WatchOptions;
            let sl: Vec<&str> = cfg.etcd_endpoints.iter().map(|s| s.as_str()).collect();
            let Ok(client) = etcd_client::Client::connect(&sl, None).await else {
                return;
            };
            let mut wc = client.watch_client();
            let Ok((_w, mut stream)) = wc
                .watch(
                    cfg.kv_backup_prefix.as_bytes().to_vec(),
                    Some(WatchOptions::new().with_prefix()),
                )
                .await
            else {
                return;
            };
            loop {
                let msg = match stream.message().await {
                    Ok(m) => m,
                    Err(_) => break,
                };
                let Some(msg) = msg else { break };
                for ev in msg.events() {
                    if let Some(kv) = ev.kv() {
                        let full = kv.key();
                        let Ok(full_str) = std::str::from_utf8(full) else { continue };
                        let Some(rest) = full_str.strip_prefix(&cfg.kv_backup_prefix) else {
                            continue;
                        };
                        let key = rest.as_bytes().to_vec();
                        match ev.event_type() {
                            etcd_client::EventType::Put => {
                                if let Ok(dec) = KeyValue::decode(kv.value()) {
                                    let st = inner.kv.get_state().await;
                                    let mut new_st = st;
                                    new_st.cache.insert(
                                        key,
                                        crate::kv_store::ValueEntry {
                                            value: dec.value.clone(),
                                            create_rev: dec.create_revision,
                                            mod_rev: dec.mod_revision,
                                            version: dec.version,
                                            lease: dec.lease,
                                        },
                                    );
                                    new_st.revision = new_st.revision.max(dec.mod_revision);
                                    inner.kv.set_state(new_st).await;
                                }
                            }
                            etcd_client::EventType::Delete => {
                                let mut st = inner.kv.get_state().await;
                                st.cache.remove(&key);
                                inner.kv.set_state(st).await;
                            }
                        }
                    }
                }
            }
        });
    }

    fn spawn_slave_lease_watcher(&self, start_revision: i64) {
        let lease = self.inner.lease.clone();
        let cfg = self.inner.cfg.clone();
        tokio::spawn(async move {
            use etcd_client::WatchOptions;
            let mut watch_revision = start_revision;
            let mut backoff = Duration::from_millis(200);
            let mut need_snapshot_sync = false;
            loop {
                if need_snapshot_sync {
                    match recover_leases_snapshot_from_etcd(&cfg).await {
                        Ok(snapshot) => {
                            lease.sync_backup_snapshot(&snapshot.leases).await;
                            watch_revision = snapshot.revision;
                            backoff = Duration::from_millis(200);
                        }
                        Err(err) => {
                            warn!(error = %err, "slave lease resync failed");
                            tokio::time::sleep(backoff).await;
                            backoff = (backoff * 2).min(Duration::from_secs(3));
                            continue;
                        }
                    }
                }
                let sl: Vec<&str> = cfg.etcd_endpoints.iter().map(|s| s.as_str()).collect();
                let client = match etcd_client::Client::connect(&sl, None).await {
                    Ok(client) => client,
                    Err(err) => {
                        warn!(error = %err, "slave lease watch connect failed");
                        need_snapshot_sync = true;
                        tokio::time::sleep(backoff).await;
                        backoff = (backoff * 2).min(Duration::from_secs(3));
                        continue;
                    }
                };
                let mut wc = client.watch_client();
                let opts = WatchOptions::new()
                    .with_prefix()
                    .with_progress_notify()
                    .with_start_revision(watch_revision);
                let (_w, mut stream) = match wc
                    .watch(cfg.lease_backup_prefix.as_bytes().to_vec(), Some(opts))
                    .await
                {
                    Ok(stream) => stream,
                    Err(err) => {
                        warn!(error = %err, revision = watch_revision, "slave lease watch start failed");
                        need_snapshot_sync = true;
                        tokio::time::sleep(backoff).await;
                        backoff = (backoff * 2).min(Duration::from_secs(3));
                        continue;
                    }
                };
                backoff = Duration::from_millis(200);
                let mut idle_timeouts = 0_u64;
                loop {
                    let msg = match tokio::time::timeout(
                        Duration::from_secs(1),
                        stream.message(),
                    )
                    .await
                    {
                        Ok(Ok(m)) => m,
                        Ok(Err(err)) => {
                            warn!(error = %err, "slave lease watch stream failed");
                            need_snapshot_sync = true;
                            break;
                        }
                        Err(_) => {
                            idle_timeouts += 1;
                            if should_resync_after_idle_timeout(
                                idle_timeouts,
                                cfg.lease_watch_idle_resync_secs,
                            ) {
                                need_snapshot_sync = true;
                                break;
                            }
                            continue;
                        }
                    };
                    let Some(msg) = msg else {
                        need_snapshot_sync = true;
                        break;
                    };
                    idle_timeouts = 0;
                    if let Some(header) = msg.header() {
                        watch_revision = watch_revision.max(header.revision());
                    }
                    for ev in msg.events() {
                        let Some(kv) = ev.kv() else { continue };
                        let Ok(full) = std::str::from_utf8(kv.key()) else {
                            continue;
                        };
                        let Some(rest) = full.strip_prefix(&cfg.lease_backup_prefix) else {
                            continue;
                        };
                        let Ok(lease_id) = rest.parse::<i64>() else {
                            continue;
                        };
                        match ev.event_type() {
                            etcd_client::EventType::Put => {
                                let Ok((decoded_id, ttl)) =
                                    crate::backup::decode_persisted_lease(kv.value())
                                else {
                                    continue;
                                };
                                watch_revision = watch_revision.max(kv.mod_revision());
                                lease.apply_backup_put(decoded_id, ttl).await;
                            }
                            etcd_client::EventType::Delete => {
                                watch_revision = watch_revision.max(kv.mod_revision());
                                lease.apply_backup_delete(lease_id).await;
                            }
                        }
                    }
                }
                tokio::time::sleep(backoff).await;
                backoff = (backoff * 2).min(Duration::from_secs(3));
            }
        });
    }

    pub(crate) async fn hdr(&self) -> ResponseHeader {
        let rev = self.inner.kv.current_revision().await;
        ResponseHeader {
            cluster_id: self.inner.cfg.cluster_id,
            member_id: self.inner.cfg.member_id,
            revision: rev,
            raft_term: 1,
        }
    }

    pub(crate) fn master_write(&self) -> Result<(), Status> {
        if self.inner.cfg.role != MetaStoreRole::Master {
            return Err(Status::permission_denied("slave rejects writes"));
        }
        Ok(())
    }

    pub(crate) fn backup_put(&self, logical_key: Vec<u8>, kv: KeyValue) {
        if let Some(b) = &self.inner.backup {
            b.try_send(BackupOp::PutKv { logical_key, kv });
        }
    }

    pub(crate) fn backup_delete(&self, logical_key: Vec<u8>) {
        if let Some(b) = &self.inner.backup {
            b.try_send(BackupOp::DeleteKey { logical_key });
        }
    }

    pub(crate) async fn publish_all(&self, events: Vec<(Vec<u8>, crate::pb::mvccpb::Event)>) {
        let h = self.hdr().await;
        for (k, ev) in events {
            self.inner.watch.publish(&k, ev, h.clone());
        }
    }

    /// Serve etcd-compatible gRPC on `listener` until error or shutdown.
    pub async fn serve(
        self,
        listener: tokio::net::TcpListener,
    ) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
        let svc = self.clone();
        Server::builder()
            .add_service(KvServer::new(svc.clone()))
            .add_service(WatchServer::new(svc.clone()))
            .add_service(LeaseServer::new(svc.clone()))
            .add_service(MaintenanceServer::new(svc.clone()))
            .add_service(
                yr_proto::metastore::meta_store_service_server::MetaStoreServiceServer::new(svc),
            )
            .serve_with_incoming(TcpListenerStream::new(listener))
            .await?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::should_resync_after_idle_timeout;

    #[test]
    fn idle_timeout_requires_long_silence_before_resync() {
        assert!(!should_resync_after_idle_timeout(1, 30));
        assert!(!should_resync_after_idle_timeout(29, 30));
        assert!(should_resync_after_idle_timeout(30, 30));
    }
}

#[async_trait]
impl Kv for MetaStoreServer {
    async fn range(
        &self,
        request: Request<RangeRequest>,
    ) -> Result<Response<RangeResponse>, Status> {
        let r = request.into_inner();
        self.inner
            .kv
            .range(r)
            .await
            .map_err(|e| e.into_status())
            .map(Response::new)
    }

    async fn put(&self, request: Request<PutRequest>) -> Result<Response<PutResponse>, Status> {
        self.master_write()?;
        let r = request.into_inner();
        let (resp, evs) = self
            .inner
            .kv
            .put(r, Some(self.inner.lease_dyn.clone()))
            .await
            .map_err(|e| e.into_status())?;
        if let Some((k, ev)) = evs.first() {
            if let Some(kv) = &ev.kv {
                self.backup_put(k.clone(), kv.clone());
            }
        }
        self.publish_all(evs).await;
        Ok(Response::new(resp))
    }

    async fn delete_range(
        &self,
        request: Request<DeleteRangeRequest>,
    ) -> Result<Response<crate::pb::etcdserverpb::DeleteRangeResponse>, Status> {
        self.master_write()?;
        let r = request.into_inner();
        let keys: Vec<Vec<u8>> = {
            let st = self.inner.kv.get_state().await;
            let (s, e) = crate::kv_store::build_delete_range(&r);
            st.cache
                .keys()
                .filter(|k| crate::kv_store::key_in_range(k, &s, &e))
                .cloned()
                .collect()
        };
        let (resp, evs) = self
            .inner
            .kv
            .delete_range(r)
            .await
            .map_err(|e| e.into_status())?;
        for k in keys {
            self.backup_delete(k);
        }
        self.publish_all(evs).await;
        Ok(Response::new(resp))
    }

    async fn txn(&self, request: Request<TxnRequest>) -> Result<Response<TxnResponse>, Status> {
        self.master_write()?;
        let r = request.into_inner();
        let (resp, evs) = self
            .inner
            .kv
            .txn(r, Some(self.inner.lease_dyn.clone()))
            .await
            .map_err(|e| e.into_status())?;
        self.publish_all(evs).await;
        Ok(Response::new(resp))
    }

    async fn compact(
        &self,
        request: Request<CompactionRequest>,
    ) -> Result<Response<CompactionResponse>, Status> {
        self.master_write()?;
        let r = request.into_inner();
        let rev = self
            .inner
            .kv
            .compact(r.revision)
            .await
            .map_err(|e| e.into_status())?;
        Ok(Response::new(CompactionResponse {
            header: Some(ResponseHeader {
                cluster_id: self.inner.cfg.cluster_id,
                member_id: self.inner.cfg.member_id,
                revision: rev,
                raft_term: 1,
            }),
        }))
    }
}

#[async_trait]
impl Watch for MetaStoreServer {
    type WatchStream = ReceiverStream<Result<WatchResponse, Status>>;

    async fn watch(
        &self,
        request: Request<Streaming<WatchRequest>>,
    ) -> Result<Response<Self::WatchStream>, Status> {
        let mut inbound = request.into_inner();
        let (tx, rx) = mpsc::channel::<Result<WatchResponse, Status>>(256);
        let hub = self.inner.watch.clone();
        let kv = self.inner.kv.clone();
        let stream_id = hub.next_stream_id();
        let wid_gen = self.inner.next_watch_id.clone();
        let this = self.clone();

        tokio::spawn(async move {
            loop {
                let wr = match inbound.message().await {
                    Ok(m) => m,
                    Err(_) => break,
                };
                let Some(wr) = wr else { break };
                let hdr = this.hdr().await;
                match wr.request_union {
                    Some(RequestUnion::CreateRequest(c)) => {
                        let rev = kv.current_revision().await;
                        if c.start_revision > 0 && c.start_revision < rev {
                            let _ = tx
                                .send(Ok(WatchResponse {
                                    header: Some(hdr.clone()),
                                    watch_id: 0,
                                    created: false,
                                    canceled: true,
                                    compact_revision: rev,
                                    cancel_reason:
                                        "required revision compacted (no history)".into(),
                                    fragment: false,
                                    events: vec![],
                                }))
                                .await;
                            continue;
                        }
                        let watch_id = wid_gen.fetch_add(1, Ordering::SeqCst);
                        hub.add_watcher(stream_id, watch_id, c.clone(), tx.clone());
                        let _ = tx
                            .send(Ok(WatchResponse {
                                header: Some(hdr.clone()),
                                watch_id,
                                created: true,
                                canceled: false,
                                compact_revision: 0,
                                cancel_reason: String::new(),
                                fragment: false,
                                events: vec![],
                            }))
                            .await;
                    }
                    Some(RequestUnion::CancelRequest(WatchCancelRequest { watch_id })) => {
                        hub.flush_buffered(stream_id, &hdr);
                        hub.remove_watcher(stream_id, watch_id);
                        let _ = tx
                            .send(Ok(WatchResponse {
                                header: Some(hdr),
                                watch_id,
                                created: false,
                                canceled: true,
                                compact_revision: 0,
                                cancel_reason: String::new(),
                                fragment: false,
                                events: vec![],
                            }))
                            .await;
                    }
                    Some(RequestUnion::ProgressRequest(_)) => {
                        let _ = tx
                            .send(Ok(WatchResponse {
                                header: Some(hdr),
                                watch_id: 0,
                                created: false,
                                canceled: false,
                                compact_revision: 0,
                                cancel_reason: String::new(),
                                fragment: false,
                                events: vec![],
                            }))
                            .await;
                    }
                    None => {}
                }
            }
            hub.remove_stream(stream_id);
        });

        Ok(Response::new(ReceiverStream::new(rx)))
    }
}

#[async_trait]
impl Lease for MetaStoreServer {
    type LeaseKeepAliveStream = ReceiverStream<Result<LeaseKeepAliveResponse, Status>>;

    async fn lease_grant(
        &self,
        request: Request<LeaseGrantRequest>,
    ) -> Result<Response<LeaseGrantResponse>, Status> {
        let r = request.into_inner();
        let hdr = self.hdr().await;
        let resp = self
            .inner
            .lease
            .grant(r.ttl, r.id, hdr)
            .await?;
        Ok(Response::new(resp))
    }

    async fn lease_revoke(
        &self,
        request: Request<LeaseRevokeRequest>,
    ) -> Result<Response<LeaseRevokeResponse>, Status> {
        let r = request.into_inner();
        let hdr = self.hdr().await;
        let (resp, evs) = self
            .inner
            .lease
            .revoke(r.id, &self.inner.kv, hdr)
            .await?;
        self.publish_all(evs).await;
        Ok(Response::new(resp))
    }

    async fn lease_keep_alive(
        &self,
        request: Request<Streaming<LeaseKeepAliveRequest>>,
    ) -> Result<Response<Self::LeaseKeepAliveStream>, Status> {
        self.master_write()?;
        let mut inbound = request.into_inner();
        let (tx, rx) = mpsc::channel(32);
        let lease = self.inner.lease.clone();
        let this = self.clone();
        tokio::spawn(async move {
            while let Ok(Some(m)) = inbound.message().await {
                let hdr = this.hdr().await;
                let resp = lease.keep_alive(m.id, hdr).await;
                if tx.send(Ok(resp)).await.is_err() {
                    break;
                }
            }
        });
        Ok(Response::new(ReceiverStream::new(rx)))
    }

    async fn lease_time_to_live(
        &self,
        request: Request<LeaseTimeToLiveRequest>,
    ) -> Result<Response<LeaseTimeToLiveResponse>, Status> {
        let r = request.into_inner();
        let hdr = self.hdr().await;
        let out = self
            .inner
            .lease
            .time_to_live(r.id, r.keys, hdr, &self.inner.kv)
            .await;
        Ok(Response::new(out))
    }

    async fn lease_leases(
        &self,
        _request: Request<LeaseLeasesRequest>,
    ) -> Result<Response<LeaseLeasesResponse>, Status> {
        let hdr = self.hdr().await;
        let out = self.inner.lease.list_leases(hdr).await;
        Ok(Response::new(out))
    }
}

#[async_trait]
impl Maintenance for MetaStoreServer {
    type SnapshotStream = Pin<Box<dyn Stream<Item = Result<SnapshotResponse, Status>> + Send>>;

    async fn alarm(&self, _request: Request<AlarmRequest>) -> Result<Response<AlarmResponse>, Status> {
        Err(Status::unimplemented("alarm"))
    }

    async fn status(
        &self,
        _request: Request<StatusRequest>,
    ) -> Result<Response<StatusResponse>, Status> {
        let hdr = self.hdr().await;
        let mut errors = vec![];
        if let Some(b) = &self.inner.backup {
            if !b.health.healthy() {
                if let Some(m) = b.health.error_message() {
                    errors.push(m);
                } else {
                    errors.push("backup unhealthy".into());
                }
            }
        }
        Ok(Response::new(StatusResponse {
            header: Some(hdr.clone()),
            version: "yr-metastore-server".into(),
            db_size: 0,
            leader: self.inner.cfg.member_id,
            raft_index: hdr.revision as u64,
            raft_term: hdr.raft_term,
            raft_applied_index: hdr.revision as u64,
            errors,
            db_size_in_use: 0,
            is_learner: self.inner.cfg.role == MetaStoreRole::Slave,
        }))
    }

    async fn defragment(
        &self,
        _request: Request<DefragmentRequest>,
    ) -> Result<Response<DefragmentResponse>, Status> {
        Err(Status::unimplemented("defragment"))
    }

    async fn hash(&self, _request: Request<HashRequest>) -> Result<Response<HashResponse>, Status> {
        Err(Status::unimplemented("hash"))
    }

    async fn hash_kv(
        &self,
        _request: Request<HashKvRequest>,
    ) -> Result<Response<HashKvResponse>, Status> {
        Err(Status::unimplemented("hash_kv"))
    }

    async fn snapshot(
        &self,
        _request: Request<SnapshotRequest>,
    ) -> Result<Response<Self::SnapshotStream>, Status> {
        Err(Status::unimplemented("snapshot"))
    }

    async fn move_leader(
        &self,
        _request: Request<MoveLeaderRequest>,
    ) -> Result<Response<MoveLeaderResponse>, Status> {
        Err(Status::unimplemented("move_leader"))
    }
}
