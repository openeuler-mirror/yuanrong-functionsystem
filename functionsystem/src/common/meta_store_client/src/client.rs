//! MetaStore client: direct etcd and optional MetaStore gRPC routing.

use std::collections::HashMap;
use std::pin::Pin;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use async_stream::try_stream;
use etcd_client::{
    CampaignResponse as EtcdCampaignResponse, Compare, DeleteOptions, DeleteResponse, Event,
    GetOptions, GetResponse as EtcdGetResponse, LeaseGrantResponse, LeaseKeepAliveResponse,
    LeaseKeepAliveStream, LeaseKeeper, LeaderResponse as EtcdLeaderResponse, ObserveStream,
    PutOptions, PutResponse, ResignOptions, ResignResponse as EtcdResignResponse, StatusResponse,
    TxnOp, TxnResponse, WatchOptions, Watcher,
};
use etcd_client::proto::{
    PbDeleteResponse, PbLeaseGrantResponse, PbLeaseKeepAliveResponse, PbLeaseRevokeResponse,
    PbPutResponse,
};
use futures::Stream;
use tokio::sync::Mutex;
use tonic::transport::{Channel, Endpoint};
use tracing::warn;
use yr_common::etcd_keys::with_prefix;
use yr_proto::metastore::{
    meta_store_service_client::MetaStoreServiceClient, DeleteKvRequest, DeletePrefixRequest,
    GetKvRequest, GetPrefixRequest, GrantLeaseRequest, HealthCheckRequest, KeepAliveOnceRequest,
    MsKeyValue, PutKvRequest, RevokeLeaseRequest, WatchClientMsg, WatchCreate, WatchEventMsg,
};
use yr_proto::metastore::{watch_client_msg, WatchEventType as ProtoWatchKind};

use crate::config::MetaStoreClientConfig;
use crate::error::{MetaStoreError, Result};
use crate::health::MetaStoreHealthyObserver;
use crate::types::{GetResponse, KeyValue, WatchEvent, WatchEventType};

pub use etcd_client::{CompareOp, Txn};

/// High-level MetaStore / etcd client.
pub struct MetaStoreClient {
    cfg: MetaStoreClientConfig,
    etcd: etcd_client::Client,
    grpc: Option<Channel>,
    connected: Arc<AtomicBool>,
    health_observer: Arc<Mutex<Option<Arc<dyn MetaStoreHealthyObserver>>>>,
    watch_slots: Arc<Mutex<HashMap<i64, Watcher>>>,
}

impl MetaStoreClient {
    pub async fn connect(cfg: MetaStoreClientConfig) -> Result<Self> {
        let eps: Vec<String> = cfg.etcd_endpoints();
        if eps.is_empty() {
            return Err(MetaStoreError::Config(
                "MetaStoreClientConfig.etcd_address must list at least one endpoint".into(),
            ));
        }
        let ep_refs: Vec<&str> = eps.iter().map(|s| s.as_str()).collect();
        let etcd = etcd_client::Client::connect(&ep_refs, None)
            .await
            .map_err(MetaStoreError::Etcd)?;

        let grpc = if cfg.enable_meta_store {
            let addr = cfg.meta_store_address.trim();
            if addr.is_empty() {
                return Err(MetaStoreError::Config(
                    "meta_store_address is required when enable_meta_store is true".into(),
                ));
            }
            let uri = if addr.starts_with("http://") || addr.starts_with("https://") {
                addr.to_string()
            } else {
                format!("http://{addr}")
            };
            let ch = Endpoint::from_shared(uri)
                .map_err(|e| MetaStoreError::Config(e.to_string()))?
                .connect()
                .await
                .map_err(|e| MetaStoreError::Config(format!("gRPC connect: {e}")))?;
            Some(ch)
        } else {
            None
        };

        Ok(Self {
            cfg,
            etcd,
            grpc,
            connected: Arc::new(AtomicBool::new(true)),
            health_observer: Arc::new(Mutex::new(None)),
            watch_slots: Arc::new(Mutex::new(HashMap::new())),
        })
    }

    pub async fn connect_direct(
        endpoints_csv: &str,
        etcd_table_prefix: impl Into<String>,
    ) -> Result<Self> {
        Self::connect(MetaStoreClientConfig::direct_etcd(
            endpoints_csv,
            etcd_table_prefix,
        ))
        .await
    }

    pub fn config(&self) -> &MetaStoreClientConfig {
        &self.cfg
    }

    pub async fn set_health_observer(&self, o: Option<Arc<dyn MetaStoreHealthyObserver>>) {
        *self.health_observer.lock().await = o;
    }

    fn notify_health(&self, ok: bool) {
        self.connected.store(ok, Ordering::SeqCst);
        if let Ok(g) = self.health_observer.try_lock() {
            if let Some(o) = g.as_ref() {
                o.on_metastore_healthy_changed(ok);
            }
        }
    }

    pub fn is_connected(&self) -> bool {
        self.connected.load(Ordering::SeqCst)
    }

    pub fn inner_mut(&mut self) -> &mut etcd_client::Client {
        &mut self.etcd
    }

    fn physical_key(&self, logical: &str) -> String {
        with_prefix(self.cfg.etcd_table_prefix.trim_end_matches('/'), logical)
    }

    fn strip_to_logical(&self, key: &[u8]) -> Vec<u8> {
        let p = self.cfg.etcd_table_prefix.trim_end_matches('/').as_bytes();
        if p.is_empty() {
            return key.to_vec();
        }
        if key.starts_with(p) {
            key[p.len()..].to_vec()
        } else {
            key.to_vec()
        }
    }

    fn excluded(&self, logical: &str) -> bool {
        self.cfg
            .excluded_keys
            .iter()
            .any(|p| logical.starts_with(p.as_str()))
    }

    fn route_kv_grpc(&self, logical: &str) -> bool {
        self.cfg.enable_meta_store && self.grpc.is_some() && !self.excluded(logical)
    }

    fn route_lease_grpc(&self) -> bool {
        self.cfg.enable_meta_store && self.grpc.is_some()
    }

    fn election_via_grpc(&self) -> bool {
        self.cfg.enable_meta_store && self.cfg.is_passthrough && self.grpc.is_some()
    }

    fn map_etcd_get(&self, mut r: EtcdGetResponse) -> GetResponse {
        let header_revision = r.header().map(|h| h.revision()).unwrap_or(0);
        let kvs: Vec<KeyValue> = r
            .take_kvs()
            .into_iter()
            .map(|kv| KeyValue {
                key: self.strip_to_logical(kv.key()),
                value: kv.value().to_vec(),
                create_revision: kv.create_revision(),
                mod_revision: kv.mod_revision(),
                version: kv.version(),
                lease: kv.lease(),
            })
            .collect();
        GetResponse {
            more: r.more(),
            count: r.count(),
            kvs,
            header_revision,
        }
    }

    fn map_grpc_get(&self, r: yr_proto::metastore::GetKvResponse) -> GetResponse {
        let kvs = r.kvs.into_iter().map(|kv| self.ms_kv_to_logical(kv)).collect();
        GetResponse {
            kvs,
            more: r.more,
            count: r.count,
            header_revision: r.header_revision,
        }
    }

    async fn etcd_put_retry(
        &self,
        key: String,
        value: Vec<u8>,
        lease: Option<i64>,
    ) -> Result<PutResponse> {
        let mut backoff = Duration::from_millis(100);
        for attempt in 0..5 {
            let mut cli = self.etcd.clone();
            let opts = lease.map(|id| PutOptions::new().with_lease(id));
            match cli.put(key.clone(), value.clone(), opts).await {
                Ok(r) => {
                    self.notify_health(true);
                    return Ok(r);
                }
                Err(e) => {
                    self.notify_health(false);
                    if attempt == 4 {
                        return Err(MetaStoreError::Etcd(e));
                    }
                    tokio::time::sleep(backoff).await;
                    backoff = (backoff * 2).min(Duration::from_secs(5));
                }
            }
        }
        unreachable!()
    }

    async fn etcd_get_retry(&self, key: String, with_prefix: bool) -> Result<EtcdGetResponse> {
        let mut backoff = Duration::from_millis(100);
        for attempt in 0..5 {
            let mut cli = self.etcd.clone();
            let res = if with_prefix {
                cli
                    .get(key.clone(), Some(GetOptions::new().with_prefix()))
                    .await
            } else {
                cli.get(key.clone(), None).await
            };
            match res {
                Ok(r) => {
                    self.notify_health(true);
                    return Ok(r);
                }
                Err(e) => {
                    self.notify_health(false);
                    if attempt == 4 {
                        return Err(MetaStoreError::Etcd(e));
                    }
                    tokio::time::sleep(backoff).await;
                    backoff = (backoff * 2).min(Duration::from_secs(5));
                }
            }
        }
        unreachable!()
    }

    async fn etcd_delete_retry(&self, key: String, with_prefix: bool) -> Result<DeleteResponse> {
        let mut backoff = Duration::from_millis(100);
        for attempt in 0..5 {
            let mut cli = self.etcd.clone();
            let res = if with_prefix {
                cli
                    .delete(key.clone(), Some(DeleteOptions::new().with_prefix()))
                    .await
            } else {
                cli.delete(key.clone(), None).await
            };
            match res {
                Ok(r) => {
                    self.notify_health(true);
                    return Ok(r);
                }
                Err(e) => {
                    self.notify_health(false);
                    if attempt == 4 {
                        return Err(MetaStoreError::Etcd(e));
                    }
                    tokio::time::sleep(backoff).await;
                    backoff = (backoff * 2).min(Duration::from_secs(5));
                }
            }
        }
        unreachable!()
    }

    fn ms_kv_to_logical(&self, kv: MsKeyValue) -> KeyValue {
        KeyValue {
            key: self.strip_to_logical(&kv.key),
            value: kv.value,
            create_revision: kv.create_revision,
            mod_revision: kv.mod_revision,
            version: kv.version,
            lease: kv.lease,
        }
    }

    fn synthetic_put() -> PutResponse {
        PutResponse(PbPutResponse::default())
    }

    fn synthetic_delete(deleted: i64) -> DeleteResponse {
        DeleteResponse(PbDeleteResponse {
            header: None,
            deleted,
            prev_kvs: vec![],
        })
    }

    // ---- KV ----

    pub async fn put(&mut self, key: impl AsRef<str>, value: &[u8]) -> Result<PutResponse> {
        let logical = key.as_ref();
        let phys = self.physical_key(logical);
        if self.route_kv_grpc(logical) {
            let ch = self.grpc.as_ref().ok_or_else(|| {
                MetaStoreError::msg("gRPC channel not initialized")
            })?;
            let mut c = grpc_client(ch);
            c.put(PutKvRequest {
                key: phys.into_bytes(),
                value: value.to_vec(),
                lease: 0,
            })
            .await
            .map_err(|e| {
                self.notify_health(false);
                MetaStoreError::Grpc(e)
            })?;
            self.notify_health(true);
            return Ok(Self::synthetic_put());
        }
        let r = self
            .etcd_put_retry(phys, value.to_vec(), None)
            .await?;
        Ok(r)
    }

    pub async fn put_with_lease(
        &mut self,
        key: impl AsRef<str>,
        value: &[u8],
        lease_id: i64,
    ) -> Result<PutResponse> {
        let logical = key.as_ref();
        let phys = self.physical_key(logical);
        if self.route_kv_grpc(logical) {
            let ch = self.grpc.as_ref().ok_or_else(|| {
                MetaStoreError::msg("gRPC channel not initialized")
            })?;
            let mut c = grpc_client(ch);
            c.put(PutKvRequest {
                key: phys.into_bytes(),
                value: value.to_vec(),
                lease: lease_id,
            })
            .await
            .map_err(|e| {
                self.notify_health(false);
                MetaStoreError::Grpc(e)
            })?;
            self.notify_health(true);
            return Ok(Self::synthetic_put());
        }
        let r = self
            .etcd_put_retry(phys, value.to_vec(), Some(lease_id))
            .await?;
        Ok(r)
    }

    pub async fn get(&mut self, key: impl AsRef<str>) -> Result<GetResponse> {
        let logical = key.as_ref();
        let phys = self.physical_key(logical);
        if self.route_kv_grpc(logical) {
            let ch = self.grpc.as_ref().ok_or_else(|| {
                MetaStoreError::msg("gRPC channel not initialized")
            })?;
            let mut c = grpc_client(ch);
            let r = c
                .get(GetKvRequest {
                    key: phys.into_bytes(),
                })
                .await
                .map_err(|e| {
                    self.notify_health(false);
                    MetaStoreError::Grpc(e)
                })?
                .into_inner();
            self.notify_health(true);
            return Ok(self.map_grpc_get(r));
        }
        let r = self.etcd_get_retry(phys, false).await?;
        Ok(self.map_etcd_get(r))
    }

    pub async fn get_prefix(&mut self, prefix: impl AsRef<str>) -> Result<GetResponse> {
        let logical = prefix.as_ref();
        let phys = self.physical_key(logical);
        if self.route_kv_grpc(logical) {
            let ch = self.grpc.as_ref().ok_or_else(|| {
                MetaStoreError::msg("gRPC channel not initialized")
            })?;
            let mut c = grpc_client(ch);
            let r = c
                .get_prefix(GetPrefixRequest {
                    prefix: phys.into_bytes(),
                    limit: 0,
                })
                .await
                .map_err(|e| {
                    self.notify_health(false);
                    MetaStoreError::Grpc(e)
                })?
                .into_inner();
            self.notify_health(true);
            return Ok(self.map_grpc_get(r));
        }
        let r = self.etcd_get_retry(phys, true).await?;
        Ok(self.map_etcd_get(r))
    }

    pub async fn delete(&mut self, key: impl AsRef<str>) -> Result<DeleteResponse> {
        let logical = key.as_ref();
        let phys = self.physical_key(logical);
        if self.route_kv_grpc(logical) {
            let ch = self.grpc.as_ref().ok_or_else(|| {
                MetaStoreError::msg("gRPC channel not initialized")
            })?;
            let mut c = grpc_client(ch);
            let r = c
                .delete(DeleteKvRequest {
                    key: phys.into_bytes(),
                })
                .await
                .map_err(|e| {
                    self.notify_health(false);
                    MetaStoreError::Grpc(e)
                })?
                .into_inner();
            self.notify_health(true);
            return Ok(Self::synthetic_delete(r.deleted));
        }
        let r = self.etcd_delete_retry(phys, false).await?;
        Ok(r)
    }

    pub async fn delete_prefix(&mut self, prefix: impl AsRef<str>) -> Result<DeleteResponse> {
        let logical = prefix.as_ref();
        let phys = self.physical_key(logical);
        if self.route_kv_grpc(logical) {
            let ch = self.grpc.as_ref().ok_or_else(|| {
                MetaStoreError::msg("gRPC channel not initialized")
            })?;
            let mut c = grpc_client(ch);
            let r = c
                .delete_prefix(DeletePrefixRequest {
                    prefix: phys.into_bytes(),
                })
                .await
                .map_err(|e| {
                    self.notify_health(false);
                    MetaStoreError::Grpc(e)
                })?
                .into_inner();
            self.notify_health(true);
            return Ok(Self::synthetic_delete(r.deleted));
        }
        let r = self.etcd_delete_retry(phys, true).await?;
        Ok(r)
    }

    pub async fn txn(&mut self, txn: Txn) -> Result<TxnResponse> {
        let mut backoff = Duration::from_millis(100);
        for attempt in 0..5 {
            let mut cli = self.etcd.clone();
            match cli.txn(txn.clone()).await {
                Ok(r) => {
                    self.notify_health(true);
                    return Ok(r);
                }
                Err(e) => {
                    self.notify_health(false);
                    if attempt == 4 {
                        return Err(MetaStoreError::Etcd(e));
                    }
                    tokio::time::sleep(backoff).await;
                    backoff = (backoff * 2).min(Duration::from_secs(5));
                }
            }
        }
        unreachable!()
    }

    pub async fn txn_put_if_version(
        &mut self,
        key: impl AsRef<str>,
        value: &[u8],
        expected_mod_revision: i64,
    ) -> Result<bool> {
        let phys = self.physical_key(key.as_ref());
        let mut cli = self.etcd.clone();
        let resp = cli
            .txn(
                Txn::new()
                    .when([Compare::mod_revision(
                        phys.clone(),
                        CompareOp::Equal,
                        expected_mod_revision,
                    )])
                    .and_then([TxnOp::put(phys, value, None)])
                    .or_else([]),
            )
            .await
            .map_err(|e| {
                self.notify_health(false);
                MetaStoreError::Etcd(e)
            })?;
        self.notify_health(true);
        Ok(resp.succeeded())
    }

    fn map_etcd_watch_event(ev: &Event, strip: impl Fn(&[u8]) -> Vec<u8>) -> Option<WatchEvent> {
        let et = match ev.event_type() {
            etcd_client::EventType::Put => WatchEventType::Put,
            etcd_client::EventType::Delete => WatchEventType::Delete,
        };
        let kv = ev.kv()?;
        let prev = ev.prev_kv().map(|p| strip(p.key()));
        Some(WatchEvent {
            event_type: et,
            key: strip(kv.key()),
            value: kv.value().to_vec(),
            prev_value: prev,
            mod_revision: kv.mod_revision(),
        })
    }

    pub fn watch(
        &mut self,
        key: impl Into<String>,
    ) -> Pin<Box<dyn Stream<Item = Result<WatchEvent>> + Send>> {
        self.watch_inner(key.into(), false)
    }

    pub fn watch_prefix(
        &mut self,
        prefix: impl Into<String>,
    ) -> Pin<Box<dyn Stream<Item = Result<WatchEvent>> + Send>> {
        self.watch_inner(prefix.into(), true)
    }

    fn watch_inner(
        &mut self,
        logical: String,
        is_prefix: bool,
    ) -> Pin<Box<dyn Stream<Item = Result<WatchEvent>> + Send>> {
        let phys = self.physical_key(&logical);
        let route_grpc = self.route_kv_grpc(&logical);
        let grpc_ch = self.grpc.clone();
        let etcd = self.etcd.clone();
        let pfx = self.cfg.etcd_table_prefix.clone();
        let strip = move |k: &[u8]| {
            let pb = pfx.trim_end_matches('/').as_bytes();
            if pb.is_empty() {
                k.to_vec()
            } else if k.starts_with(pb) {
                k[pb.len()..].to_vec()
            } else {
                k.to_vec()
            }
        };
        let slots = self.watch_slots.clone();

        if route_grpc {
            if let Some(ch) = grpc_ch {
                return Box::pin(Self::grpc_watch_stream(
                    phys,
                    is_prefix,
                    0,
                    strip,
                    ch,
                ));
            }
        }

        let mut backoff = Duration::from_millis(200);
        Box::pin(try_stream! {
            loop {
                let key_bytes = phys.clone().into_bytes();
                let mut cli = etcd.clone();
                let opts = {
                    let mut o = WatchOptions::new().with_prev_key();
                    if is_prefix {
                        o = o.with_prefix();
                    }
                    o
                };
                let wres = cli.watch(key_bytes.clone(), Some(opts)).await;
                let (watcher, mut stream) = match wres {
                    Ok(w) => w,
                    Err(e) => {
                        warn!(error = %e, "watch: connect failed, retrying");
                        tokio::time::sleep(backoff).await;
                        backoff = (backoff * 2).min(Duration::from_secs(30));
                        continue;
                    }
                };
                backoff = Duration::from_millis(200);
                let wid = watcher.watch_id();
                slots.lock().await.insert(wid, watcher);

                loop {
                    match stream.message().await {
                        Ok(Some(resp)) => {
                            if resp.canceled() {
                                let reason = resp.cancel_reason().to_string();
                                slots.lock().await.remove(&wid);
                                Err(MetaStoreError::msg(format!("watch canceled: {reason}")))?;
                            }
                            for ev in resp.events() {
                                if let Some(we) = Self::map_etcd_watch_event(ev, &strip) {
                                    yield we;
                                }
                            }
                        }
                        Ok(None) => {
                            slots.lock().await.remove(&wid);
                            warn!("watch: stream ended, reconnecting");
                            break;
                        }
                        Err(e) => {
                            slots.lock().await.remove(&wid);
                            warn!(error = %e, "watch: stream error, reconnecting");
                            tokio::time::sleep(backoff).await;
                            backoff = (backoff * 2).min(Duration::from_secs(30));
                            break;
                        }
                    }
                }
            }
        })
    }

    fn grpc_watch_stream(
        phys: String,
        is_prefix: bool,
        start_revision: i64,
        strip: impl Fn(&[u8]) -> Vec<u8> + Send + Sync + Clone + 'static,
        ch: Channel,
    ) -> impl Stream<Item = Result<WatchEvent>> {
        try_stream! {
            let (tx, rx) = tokio::sync::mpsc::channel::<WatchClientMsg>(8);
            let create = WatchClientMsg {
                union: Some(watch_client_msg::Union::Create(WatchCreate {
                    key: phys.into_bytes(),
                    prefix: is_prefix,
                    prev_kv: true,
                    start_revision,
                })),
            };
            tx.send(create)
                .await
                .map_err(|e| MetaStoreError::msg(e.to_string()))?;
            let mut client = MetaStoreServiceClient::new(ch);
            let mut inbound = client
                .watch(tokio_stream::wrappers::ReceiverStream::new(rx))
                .await
                .map_err(MetaStoreError::Grpc)?
                .into_inner();
            while let Some(msg) = inbound.message().await.map_err(MetaStoreError::Grpc)? {
                if msg.canceled {
                    Err(MetaStoreError::msg(msg.cancel_reason))?;
                }
                if let Some(ev) = msg.event {
                    if let Some(we) = map_grpc_watch_event(ev, &strip) {
                        yield we;
                    }
                }
            }
        }
    }

    pub async fn get_and_watch(
        &mut self,
        prefix: impl AsRef<str>,
    ) -> Result<(
        GetResponse,
        Pin<Box<dyn Stream<Item = Result<WatchEvent>> + Send>>,
    )> {
        let initial = self.get_prefix(prefix.as_ref()).await?;
        let rev = initial.header_revision;
        let logical = prefix.as_ref().to_string();
        let stream = self.watch_prefix_from_revision(logical, rev.saturating_add(1));
        Ok((initial, stream))
    }

    fn watch_prefix_from_revision(
        &mut self,
        logical: String,
        start_revision: i64,
    ) -> Pin<Box<dyn Stream<Item = Result<WatchEvent>> + Send>> {
        let phys = self.physical_key(&logical);
        let route_grpc = self.route_kv_grpc(&logical);
        let grpc_ch = self.grpc.clone();
        let etcd = self.etcd.clone();
        let pfx = self.cfg.etcd_table_prefix.clone();
        let strip = move |k: &[u8]| {
            let pb = pfx.trim_end_matches('/').as_bytes();
            if pb.is_empty() {
                k.to_vec()
            } else if k.starts_with(pb) {
                k[pb.len()..].to_vec()
            } else {
                k.to_vec()
            }
        };
        let slots = self.watch_slots.clone();

        if route_grpc {
            if let Some(ch) = grpc_ch {
                return Box::pin(Self::grpc_watch_stream(
                    phys,
                    true,
                    start_revision,
                    strip,
                    ch,
                ));
            }
        }

        let mut backoff = Duration::from_millis(200);
        Box::pin(try_stream! {
            loop {
                let key_bytes = phys.clone().into_bytes();
                let mut cli = etcd.clone();
                let opts = WatchOptions::new()
                    .with_prefix()
                    .with_prev_key()
                    .with_start_revision(start_revision);
                let wres = cli.watch(key_bytes, Some(opts)).await;
                let (watcher, mut stream) = match wres {
                    Ok(w) => w,
                    Err(e) => {
                        warn!(error = %e, "watch(from_rev): connect failed, retrying");
                        tokio::time::sleep(backoff).await;
                        backoff = (backoff * 2).min(Duration::from_secs(30));
                        continue;
                    }
                };
                backoff = Duration::from_millis(200);
                let wid = watcher.watch_id();
                slots.lock().await.insert(wid, watcher);
                loop {
                    match stream.message().await {
                        Ok(Some(resp)) => {
                            if resp.canceled() {
                                let reason = resp.cancel_reason().to_string();
                                slots.lock().await.remove(&wid);
                                Err(MetaStoreError::msg(format!("watch canceled: {reason}")))?;
                            }
                            for ev in resp.events() {
                                if let Some(we) = MetaStoreClient::map_etcd_watch_event(ev, &strip) {
                                    yield we;
                                }
                            }
                        }
                        Ok(None) => {
                            slots.lock().await.remove(&wid);
                            break;
                        }
                        Err(e) => {
                            slots.lock().await.remove(&wid);
                            warn!(error = %e, "watch(from_rev): reconnecting");
                            tokio::time::sleep(backoff).await;
                            backoff = (backoff * 2).min(Duration::from_secs(30));
                            break;
                        }
                    }
                }
            }
        })
    }

    pub async fn cancel_watch(&mut self, watch_id: i64) -> Result<()> {
        let mut g = self.watch_slots.lock().await;
        if let Some(mut w) = g.remove(&watch_id) {
            w.cancel().await.map_err(MetaStoreError::Etcd)?;
        }
        Ok(())
    }

    // ---- Lease ----

    pub async fn grant_lease(&mut self, ttl: i64) -> Result<LeaseGrantResponse> {
        if self.route_lease_grpc() {
            let ch = self.grpc.as_ref().ok_or_else(|| {
                MetaStoreError::msg("gRPC channel not initialized")
            })?;
            let mut c = grpc_client(ch);
            let r = c
                .grant_lease(GrantLeaseRequest { ttl })
                .await
                .map_err(MetaStoreError::Grpc)?
                .into_inner();
            self.notify_health(true);
            return Ok(LeaseGrantResponse(PbLeaseGrantResponse {
                header: None,
                id: r.id,
                ttl: r.ttl,
                error: String::new(),
            }));
        }
        let mut cli = self.etcd.clone();
        let r = cli.lease_grant(ttl, None).await.map_err(|e| {
            self.notify_health(false);
            MetaStoreError::Etcd(e)
        })?;
        self.notify_health(true);
        Ok(r)
    }

    pub async fn revoke_lease(&mut self, lease_id: i64) -> Result<etcd_client::LeaseRevokeResponse> {
        if self.route_lease_grpc() {
            let ch = self.grpc.as_ref().ok_or_else(|| {
                MetaStoreError::msg("gRPC channel not initialized")
            })?;
            let mut c = grpc_client(ch);
            c.revoke_lease(RevokeLeaseRequest { id: lease_id })
                .await
                .map_err(MetaStoreError::Grpc)?;
            self.notify_health(true);
            return Ok(etcd_client::LeaseRevokeResponse(PbLeaseRevokeResponse::default()));
        }
        let mut cli = self.etcd.clone();
        let r = cli.lease_revoke(lease_id).await.map_err(|e| {
            self.notify_health(false);
            MetaStoreError::Etcd(e)
        })?;
        self.notify_health(true);
        Ok(r)
    }

    pub async fn keep_alive_once(&mut self, lease_id: i64) -> Result<LeaseKeepAliveResponse> {
        if self.route_lease_grpc() {
            let ch = self.grpc.as_ref().ok_or_else(|| {
                MetaStoreError::msg("gRPC channel not initialized")
            })?;
            let mut c = grpc_client(ch);
            let r = c
                .keep_alive_once(KeepAliveOnceRequest { id: lease_id })
                .await
                .map_err(MetaStoreError::Grpc)?
                .into_inner();
            self.notify_health(true);
            return Ok(LeaseKeepAliveResponse(PbLeaseKeepAliveResponse {
                id: r.id,
                ttl: r.ttl,
                ..Default::default()
            }));
        }
        let mut cli = self.etcd.clone();
        let (mut keeper, mut stream) = cli
            .lease_keep_alive(lease_id)
            .await
            .map_err(MetaStoreError::Etcd)?;
        keeper.keep_alive().await.map_err(MetaStoreError::Etcd)?;
        stream
            .message()
            .await
            .map_err(MetaStoreError::Etcd)?
            .ok_or_else(|| MetaStoreError::msg("keep_alive_once: empty stream"))
    }

    /// Bidirectional lease keep-alive: use [`LeaseKeeper::keep_alive`] to ping and read [`LeaseKeepAliveStream`].
    pub async fn keep_alive(
        &mut self,
        lease_id: i64,
    ) -> Result<(LeaseKeeper, LeaseKeepAliveStream)> {
        if self.route_lease_grpc() {
            return Err(MetaStoreError::msg(
                "keep_alive stream via MetaStore gRPC is not implemented; use direct etcd mode",
            ));
        }
        let mut cli = self.etcd.clone();
        cli.lease_keep_alive(lease_id)
            .await
            .map_err(MetaStoreError::Etcd)
    }

    // ---- Election ----

    pub async fn campaign(
        &mut self,
        name: impl Into<Vec<u8>>,
        value: impl Into<Vec<u8>>,
        lease_id: i64,
    ) -> Result<EtcdCampaignResponse> {
        if self.election_via_grpc() {
            return Err(MetaStoreError::msg(
                "election via MetaStore gRPC (passthrough) is not implemented yet",
            ));
        }
        let mut cli = self.etcd.clone();
        cli.campaign(name, value, lease_id)
            .await
            .map_err(MetaStoreError::Etcd)
    }

    pub async fn leader(&mut self, name: impl Into<Vec<u8>>) -> Result<EtcdLeaderResponse> {
        if self.election_via_grpc() {
            return Err(MetaStoreError::msg(
                "election via MetaStore gRPC (passthrough) is not implemented yet",
            ));
        }
        let mut cli = self.etcd.clone();
        cli.leader(name).await.map_err(MetaStoreError::Etcd)
    }

    pub async fn resign(
        &mut self,
        name: impl Into<Vec<u8>>,
        key: impl Into<Vec<u8>>,
        rev: i64,
        lease_id: i64,
    ) -> Result<EtcdResignResponse> {
        if self.election_via_grpc() {
            return Err(MetaStoreError::msg(
                "election via MetaStore gRPC (passthrough) is not implemented yet",
            ));
        }
        let lk = etcd_client::LeaderKey::new()
            .with_name(name)
            .with_key(key)
            .with_rev(rev)
            .with_lease(lease_id);
        let mut cli = self.etcd.clone();
        cli.resign(Some(ResignOptions::new().with_leader(lk)))
            .await
            .map_err(MetaStoreError::Etcd)
    }

    pub async fn observe(&mut self, name: impl Into<Vec<u8>>) -> Result<ObserveStream> {
        if self.election_via_grpc() {
            return Err(MetaStoreError::msg(
                "election via MetaStore gRPC (passthrough) is not implemented yet",
            ));
        }
        let mut cli = self.etcd.clone();
        cli.observe(name).await.map_err(MetaStoreError::Etcd)
    }

    pub async fn health_check(&mut self) -> Result<StatusResponse> {
        if self.cfg.enable_meta_store {
            if let Some(ch) = &self.grpc {
                let mut c = grpc_client(ch);
                let ok = c
                    .health_check(HealthCheckRequest {})
                    .await
                    .map(|r| r.into_inner().ok)
                    .unwrap_or(false);
                if !ok {
                    self.notify_health(false);
                    return Err(MetaStoreError::msg(
                        "MetaStore gRPC health_check returned ok=false",
                    ));
                }
            }
        }
        let mut cli = self.etcd.clone();
        let s = cli.status().await.map_err(|e| {
            self.notify_health(false);
            MetaStoreError::Etcd(e)
        })?;
        self.notify_health(true);
        Ok(s)
    }
}

fn grpc_client(ch: &Channel) -> MetaStoreServiceClient<Channel> {
    MetaStoreServiceClient::new(ch.clone())
}

fn map_grpc_watch_event(
    ev: WatchEventMsg,
    strip: &impl Fn(&[u8]) -> Vec<u8>,
) -> Option<WatchEvent> {
    let kind = ProtoWatchKind::try_from(ev.event_type).unwrap_or(ProtoWatchKind::WatchPut);
    let et = match kind {
        ProtoWatchKind::WatchPut => WatchEventType::Put,
        ProtoWatchKind::WatchDelete => WatchEventType::Delete,
    };
    Some(WatchEvent {
        event_type: et,
        key: strip(&ev.key),
        value: ev.value,
        prev_value: if ev.prev_value.is_empty() {
            None
        } else {
            Some(strip(&ev.prev_value))
        },
        mod_revision: ev.mod_revision,
    })
}
