//! Etcd watches: bus-proxy registrations, instance routes, and compaction recovery hooks.

use crate::AppContext;
use futures::StreamExt;
use std::sync::Arc;
use tracing::{info, warn};
use yr_common::etcd_keys::{gen_busproxy_node_prefix, INSTANCE_ROUTE_PATH_PREFIX};
use yr_metastore_client::{MetaStoreClient, WatchEventType};

fn route_instance_id_from_key(logical_key: &[u8]) -> Option<String> {
    let s = std::str::from_utf8(logical_key).ok()?;
    let rest = s.strip_prefix(INSTANCE_ROUTE_PATH_PREFIX)?;
    let rest = rest.strip_prefix('/')?;
    if rest.is_empty() {
        return None;
    }
    Some(rest.to_string())
}

fn peer_node_id_from_busproxy_key(logical_key: &[u8], prefix_dir: &str) -> Option<String> {
    let s = std::str::from_utf8(logical_key).ok()?;
    let tail = s.strip_prefix(prefix_dir)?;
    let tail = tail.trim_start_matches('/');
    if tail.is_empty() {
        return None;
    }
    Some(tail.to_string())
}

/// Watches `INSTANCE_ROUTE_PATH_PREFIX` and updates the in-memory [`BusProxyCoordinator`] route table.
pub struct InstanceInfoSyncer {
    ctx: Arc<AppContext>,
}

impl InstanceInfoSyncer {
    pub fn new(ctx: Arc<AppContext>) -> Self {
        Self { ctx }
    }

    pub async fn reconcile_all(&self, store: &mut MetaStoreClient) {
        let prefix = format!("{}/", INSTANCE_ROUTE_PATH_PREFIX.trim_end_matches('/'));
        let res = match store.get_prefix(&prefix).await {
            Ok(r) => r,
            Err(e) => {
                warn!(error = %e, "instance route reconcile get_prefix");
                return;
            }
        };
        for kv in res.kvs {
            if let Some(id) = route_instance_id_from_key(&kv.key) {
                self.ctx
                    .bus
                    .apply_instance_route_put(&id, &kv.value);
            }
        }
    }
}

/// Watches bus-proxy keys and peer endpoints.
pub struct BusProxySyncer {
    ctx: Arc<AppContext>,
}

impl BusProxySyncer {
    pub fn new(ctx: Arc<AppContext>) -> Self {
        Self { ctx }
    }

    pub async fn reconcile_peers(&self, store: &mut MetaStoreClient) {
        let prefix = gen_busproxy_node_prefix(&self.ctx.config.busproxy_tenant_segment);
        let res = match store.get_prefix(&prefix).await {
            Ok(r) => r,
            Err(e) => {
                warn!(error = %e, "busproxy reconcile get_prefix");
                return;
            }
        };
        for kv in res.kvs {
            if let Some(node) = peer_node_id_from_busproxy_key(&kv.key, &prefix) {
                self.ctx.bus.upsert_peer_from_json(&node, &kv.value);
            }
        }
    }
}

/// One-shot initial reconciliation: called from main BEFORE gRPC serve.
/// Populates routes and peers so the first SDK connection sees a consistent state.
pub async fn initial_sync(ctx: Arc<AppContext>) {
    let Some(etcd) = ctx.etcd.clone() else {
        return;
    };
    let i_sync = InstanceInfoSyncer::new(ctx.clone());
    let b_sync = BusProxySyncer::new(ctx.clone());
    let mut c = etcd.lock().await;
    i_sync.reconcile_all(&mut c).await;
    b_sync.reconcile_peers(&mut c).await;
    info!("initial_sync: routes + peers reconciled from etcd");
}

/// Long-running watch loops: called from a spawned task AFTER initial_sync completes.
/// Handles incremental updates and re-reconciles on watch errors (compaction, disconnect).
pub async fn run_watch_loops(ctx: Arc<AppContext>) {
    let Some(etcd) = ctx.etcd.clone() else {
        return;
    };

    let route_prefix = format!("{}/", INSTANCE_ROUTE_PATH_PREFIX.trim_end_matches('/'));
    let bus_prefix = gen_busproxy_node_prefix(&ctx.config.busproxy_tenant_segment);

    let ctx_r = ctx.clone();
    let etcd_r = etcd.clone();
    tokio::spawn(async move {
        loop {
            let mut stream = {
                let mut c = etcd_r.lock().await;
                c.watch_prefix(&route_prefix)
            };
            loop {
                match stream.next().await {
                    Some(Ok(ev)) => {
                        if let Some(id) = route_instance_id_from_key(&ev.key) {
                            match ev.event_type {
                                WatchEventType::Put => {
                                    ctx_r.bus.apply_instance_route_put(&id, &ev.value);
                                }
                                WatchEventType::Delete => {
                                    ctx_r.bus.apply_instance_route_delete(&id);
                                }
                            }
                        }
                    }
                    Some(Err(e)) => {
                        warn!(error = %e, "instance route watch");
                        let mut c2 = etcd_r.lock().await;
                        InstanceInfoSyncer::new(ctx_r.clone())
                            .reconcile_all(&mut c2)
                            .await;
                        break;
                    }
                    None => {
                        warn!("instance route watch ended");
                        tokio::time::sleep(std::time::Duration::from_secs(1)).await;
                        break;
                    }
                }
            }
        }
    });

    let ctx_b = ctx.clone();
    let etcd_b = etcd.clone();
    tokio::spawn(async move {
        loop {
            let mut stream = {
                let mut c = etcd_b.lock().await;
                c.watch_prefix(&bus_prefix)
            };
            loop {
                match stream.next().await {
                    Some(Ok(ev)) => {
                        if let Some(node) = peer_node_id_from_busproxy_key(&ev.key, &bus_prefix) {
                            match ev.event_type {
                                WatchEventType::Put => {
                                    ctx_b.bus.upsert_peer_from_json(&node, &ev.value);
                                }
                                WatchEventType::Delete => {
                                    ctx_b.bus.remove_peer(&node);
                                }
                            }
                        }
                    }
                    Some(Err(e)) => {
                        warn!(error = %e, "busproxy watch");
                        let mut c2 = etcd_b.lock().await;
                        BusProxySyncer::new(ctx_b.clone())
                            .reconcile_peers(&mut c2)
                            .await;
                        break;
                    }
                    None => {
                        warn!("busproxy watch ended");
                        tokio::time::sleep(std::time::Duration::from_secs(1)).await;
                        break;
                    }
                }
            }
        }
    });

    info!("etcd watch loops started (routes + busproxy)");
}
