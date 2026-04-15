//! `yr.internal.metastore.MetaStoreService` façade over the same in-memory KV / watch / lease as etcd gRPC.

use std::pin::Pin;
use std::sync::atomic::Ordering;

use async_trait::async_trait;
use futures::Stream;
use tokio::sync::mpsc;
use tokio_stream::wrappers::ReceiverStream;
use tonic::{Request, Response, Status, Streaming};

use crate::kv_store::{self};
use crate::pb::etcdserverpb::{
    DeleteRangeRequest, LeaseKeepAliveResponse, PutRequest, RangeRequest, WatchCreateRequest,
    WatchResponse,
};
use crate::pb::mvccpb::{self, Event};
use crate::server::MetaStoreServer;

use yr_proto::metastore::meta_store_service_server::MetaStoreService;
use yr_proto::metastore::{
    watch_client_msg, CampaignRequest, CampaignResponse, DeleteKvRequest, DeleteKvResponse,
    DeletePrefixRequest, DeletePrefixResponse, GetKvRequest, GetKvResponse, GetPrefixRequest,
    GrantLeaseRequest, GrantLeaseResponse, HealthCheckRequest, HealthCheckResponse,
    KeepAliveClientMsg, KeepAliveOnceRequest, KeepAliveOnceResponse, KeepAliveServerMsg,
    LeaderRequest, LeaderResponse, MsKeyValue, ObserveRequest, PutKvRequest, PutKvResponse,
    RevokeLeaseRequest, RevokeLeaseResponse, WatchClientMsg, WatchEventMsg, WatchEventType,
    WatchServerMsg,
};

fn ms_kv(kv: &mvccpb::KeyValue) -> MsKeyValue {
    MsKeyValue {
        key: kv.key.clone(),
        value: kv.value.clone(),
        create_revision: kv.create_revision,
        mod_revision: kv.mod_revision,
        version: kv.version,
        lease: kv.lease,
    }
}

fn etcd_event_to_watch_msg(ev: &Event) -> Option<WatchEventMsg> {
    let kv = ev.kv.as_ref()?;
    let event_type = if ev.r#type == mvccpb::event::EventType::Put as i32 {
        WatchEventType::WatchPut as i32
    } else if ev.r#type == mvccpb::event::EventType::Delete as i32 {
        WatchEventType::WatchDelete as i32
    } else {
        WatchEventType::WatchPut as i32
    };
    let prev_value = ev
        .prev_kv
        .as_ref()
        .map(|p| p.value.clone())
        .unwrap_or_default();
    Some(WatchEventMsg {
        event_type,
        key: kv.key.clone(),
        value: kv.value.clone(),
        prev_value,
        mod_revision: kv.mod_revision,
    })
}

#[async_trait]
impl MetaStoreService for MetaStoreServer {
    async fn put(
        &self,
        request: Request<PutKvRequest>,
    ) -> Result<Response<PutKvResponse>, Status> {
        self.master_write()?;
        let r = request.into_inner();
        let put = PutRequest {
            key: r.key,
            value: r.value,
            lease: r.lease,
            ..Default::default()
        };
        let (resp, evs) = self
            .inner
            .kv
            .put(put, Some(self.inner.lease_dyn.clone()))
            .await
            .map_err(|e| e.into_status())?;
        if let Some((k, ev)) = evs.first() {
            if let Some(kv) = &ev.kv {
                self.backup_put(k.clone(), kv.clone());
            }
        }
        self.publish_all(evs).await;
        let _ = resp;
        Ok(Response::new(PutKvResponse {}))
    }

    async fn get(
        &self,
        request: Request<GetKvRequest>,
    ) -> Result<Response<GetKvResponse>, Status> {
        let r = request.into_inner();
        if r.key.is_empty() {
            return Err(Status::invalid_argument("empty key"));
        }
        let rr = RangeRequest {
            key: r.key,
            ..Default::default()
        };
        let range = self.inner.kv.range(rr).await.map_err(|e| e.into_status())?;
        let header_revision = range
            .header
            .as_ref()
            .map(|h| h.revision)
            .unwrap_or(0);
        let kvs: Vec<MsKeyValue> = range.kvs.iter().map(ms_kv).collect();
        Ok(Response::new(GetKvResponse {
            kvs,
            more: range.more,
            count: range.count,
            header_revision,
        }))
    }

    async fn get_prefix(
        &self,
        request: Request<GetPrefixRequest>,
    ) -> Result<Response<GetKvResponse>, Status> {
        let r = request.into_inner();
        if r.prefix.is_empty() {
            return Err(Status::invalid_argument("empty prefix"));
        }
        let end = kv_store::get_prefix(&r.prefix);
        let rr = RangeRequest {
            key: r.prefix,
            range_end: end,
            limit: r.limit,
            ..Default::default()
        };
        let range = self.inner.kv.range(rr).await.map_err(|e| e.into_status())?;
        let header_revision = range
            .header
            .as_ref()
            .map(|h| h.revision)
            .unwrap_or(0);
        let kvs: Vec<MsKeyValue> = range.kvs.iter().map(ms_kv).collect();
        Ok(Response::new(GetKvResponse {
            kvs,
            more: range.more,
            count: range.count,
            header_revision,
        }))
    }

    async fn delete(
        &self,
        request: Request<DeleteKvRequest>,
    ) -> Result<Response<DeleteKvResponse>, Status> {
        self.master_write()?;
        let r = request.into_inner();
        if r.key.is_empty() {
            return Err(Status::invalid_argument("empty key"));
        }
        let dr = DeleteRangeRequest {
            key: r.key.clone(),
            ..Default::default()
        };
        let keys: Vec<Vec<u8>> = {
            let st = self.inner.kv.get_state().await;
            let (s, e) = kv_store::build_delete_range(&dr);
            st.cache
                .keys()
                .filter(|k| kv_store::key_in_range(k, &s, &e))
                .cloned()
                .collect()
        };
        let (resp, evs) = self
            .inner
            .kv
            .delete_range(dr)
            .await
            .map_err(|e| e.into_status())?;
        for k in keys {
            self.backup_delete(k);
        }
        self.publish_all(evs).await;
        Ok(Response::new(DeleteKvResponse {
            deleted: resp.deleted,
        }))
    }

    async fn delete_prefix(
        &self,
        request: Request<DeletePrefixRequest>,
    ) -> Result<Response<DeletePrefixResponse>, Status> {
        self.master_write()?;
        let r = request.into_inner();
        if r.prefix.is_empty() {
            return Err(Status::invalid_argument("empty prefix"));
        }
        let end = kv_store::get_prefix(&r.prefix);
        let dr = DeleteRangeRequest {
            key: r.prefix.clone(),
            range_end: end,
            ..Default::default()
        };
        let keys: Vec<Vec<u8>> = {
            let st = self.inner.kv.get_state().await;
            let (s, e) = kv_store::build_delete_range(&dr);
            st.cache
                .keys()
                .filter(|k| kv_store::key_in_range(k, &s, &e))
                .cloned()
                .collect()
        };
        let (resp, evs) = self
            .inner
            .kv
            .delete_range(dr)
            .await
            .map_err(|e| e.into_status())?;
        for k in keys {
            self.backup_delete(k);
        }
        self.publish_all(evs).await;
        Ok(Response::new(DeletePrefixResponse {
            deleted: resp.deleted,
        }))
    }

    type WatchStream = Pin<Box<dyn Stream<Item = Result<WatchServerMsg, Status>> + Send + 'static>>;

    async fn watch(
        &self,
        request: Request<Streaming<WatchClientMsg>>,
    ) -> Result<Response<Self::WatchStream>, Status> {
        let mut inbound = request.into_inner();
        let hub = self.inner.watch.clone();
        let kv = self.inner.kv.clone();
        let stream_id = hub.next_stream_id();
        let wid_gen = self.inner.next_watch_id.clone();
        let this = self.clone();

        let (out_tx, out_rx) = mpsc::channel::<Result<WatchServerMsg, Status>>(256);
        let out_to_inbound = out_tx.clone();

        tokio::spawn(async move {
            let (etcd_tx, mut etcd_rx) = mpsc::channel::<Result<WatchResponse, Status>>(256);
            let hub2 = hub.clone();
            let kv2 = kv.clone();
            let this2 = this.clone();
            tokio::spawn(async move {
                loop {
                    let msg = match inbound.message().await {
                        Ok(m) => m,
                        Err(e) => {
                            let _ = etcd_tx.send(Err(e)).await;
                            break;
                        }
                    };
                    let Some(msg) = msg else { break };
                    let hdr = this2.hdr().await;
                    match msg.union {
                        Some(watch_client_msg::Union::Create(c)) => {
                            let rev = kv2.current_revision().await;
                            if c.start_revision > 0 && c.start_revision < rev {
                                let _ = out_to_inbound
                                    .send(Ok(WatchServerMsg {
                                        watch_id: 0,
                                        event: None,
                                        canceled: true,
                                        cancel_reason:
                                            "required revision compacted (no history)".into(),
                                    }))
                                    .await;
                                continue;
                            }
                            let range_end = if c.prefix {
                                kv_store::get_prefix(&c.key)
                            } else {
                                vec![]
                            };
                            let etcd_create = WatchCreateRequest {
                                key: c.key,
                                range_end,
                                start_revision: c.start_revision,
                                prev_kv: c.prev_kv,
                                ..Default::default()
                            };
                            let watch_id = wid_gen.fetch_add(1, Ordering::SeqCst);
                            hub2.add_watcher(stream_id, watch_id, etcd_create, etcd_tx.clone());
                            let _ = etcd_tx
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
                        Some(watch_client_msg::Union::CancelWatchId(watch_id)) => {
                            hub2.flush_buffered(stream_id, &hdr);
                            hub2.remove_watcher(stream_id, watch_id);
                            let _ = etcd_tx
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
                        None => {}
                    }
                }
                hub2.remove_stream(stream_id);
            });

            while let Some(item) = etcd_rx.recv().await {
                match item {
                    Ok(wr) => {
                        if wr.canceled {
                            let _ = out_tx
                                .send(Ok(WatchServerMsg {
                                    watch_id: wr.watch_id,
                                    event: None,
                                    canceled: true,
                                    cancel_reason: wr.cancel_reason,
                                }))
                                .await;
                            continue;
                        }
                        if wr.created && wr.events.is_empty() {
                            continue;
                        }
                        for ev in wr.events {
                            if let Some(event) = etcd_event_to_watch_msg(&ev) {
                                let _ = out_tx
                                    .send(Ok(WatchServerMsg {
                                        watch_id: wr.watch_id,
                                        event: Some(event),
                                        canceled: false,
                                        cancel_reason: String::new(),
                                    }))
                                    .await;
                            }
                        }
                    }
                    Err(e) => {
                        let _ = out_tx.send(Err(e)).await;
                        break;
                    }
                }
            }
        });

        Ok(Response::new(Box::pin(ReceiverStream::new(out_rx))))
    }

    async fn grant_lease(
        &self,
        request: Request<GrantLeaseRequest>,
    ) -> Result<Response<GrantLeaseResponse>, Status> {
        let r = request.into_inner();
        let hdr = self.hdr().await;
        let resp = self.inner.lease.grant(r.ttl, 0, hdr).await?;
        Ok(Response::new(GrantLeaseResponse {
            id: resp.id,
            ttl: resp.ttl,
        }))
    }

    async fn revoke_lease(
        &self,
        request: Request<RevokeLeaseRequest>,
    ) -> Result<Response<RevokeLeaseResponse>, Status> {
        let r = request.into_inner();
        let hdr = self.hdr().await;
        let (resp, evs) = self
            .inner
            .lease
            .revoke(r.id, &self.inner.kv, hdr)
            .await?;
        self.publish_all(evs).await;
        let _ = resp;
        Ok(Response::new(RevokeLeaseResponse {}))
    }

    async fn keep_alive_once(
        &self,
        request: Request<KeepAliveOnceRequest>,
    ) -> Result<Response<KeepAliveOnceResponse>, Status> {
        let r = request.into_inner();
        let hdr = self.hdr().await;
        let resp: LeaseKeepAliveResponse = self.inner.lease.keep_alive(r.id, hdr).await;
        Ok(Response::new(KeepAliveOnceResponse {
            id: resp.id,
            ttl: resp.ttl,
        }))
    }

    type KeepAliveStream =
        Pin<Box<dyn Stream<Item = Result<KeepAliveServerMsg, Status>> + Send + 'static>>;

    async fn keep_alive(
        &self,
        _request: Request<Streaming<KeepAliveClientMsg>>,
    ) -> Result<Response<Self::KeepAliveStream>, Status> {
        Err(Status::unimplemented(
            "MetaStore KeepAlive stream not implemented on embedded server",
        ))
    }

    async fn campaign(
        &self,
        _request: Request<CampaignRequest>,
    ) -> Result<Response<CampaignResponse>, Status> {
        Err(Status::unimplemented(
            "MetaStore election RPC not implemented on embedded server",
        ))
    }

    async fn leader(
        &self,
        _request: Request<LeaderRequest>,
    ) -> Result<Response<LeaderResponse>, Status> {
        Err(Status::unimplemented(
            "MetaStore election RPC not implemented on embedded server",
        ))
    }

    async fn resign(
        &self,
        _request: Request<yr_proto::metastore::ResignRequest>,
    ) -> Result<Response<yr_proto::metastore::ResignResponse>, Status> {
        Err(Status::unimplemented(
            "MetaStore election RPC not implemented on embedded server",
        ))
    }

    type ObserveStream =
        Pin<Box<dyn Stream<Item = Result<LeaderResponse, Status>> + Send + 'static>>;

    async fn observe(
        &self,
        _request: Request<ObserveRequest>,
    ) -> Result<Response<Self::ObserveStream>, Status> {
        Err(Status::unimplemented(
            "MetaStore election RPC not implemented on embedded server",
        ))
    }

    async fn health_check(
        &self,
        _request: Request<HealthCheckRequest>,
    ) -> Result<Response<HealthCheckResponse>, Status> {
        Ok(Response::new(HealthCheckResponse {
            ok: true,
            detail: String::new(),
        }))
    }
}
