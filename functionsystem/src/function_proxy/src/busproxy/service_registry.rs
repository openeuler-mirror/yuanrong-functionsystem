//! Lease-backed bus-proxy registration in MetaStore (etcd).

use crate::config::Config;
use serde::Serialize;
use std::sync::Arc;
use tokio::sync::Mutex;
use tracing::{info, warn};
use yr_common::etcd_keys::gen_busproxy_node_key;
use yr_metastore_client::MetaStoreClient;

#[derive(Debug, Serialize)]
pub struct BusProxyRegistration {
    pub aid: String,
    pub node: String,
    pub ak: String,
    /// Advertised InnerService / primary gRPC URL for peer forwarding (`http://host:port`).
    #[serde(skip_serializing_if = "String::is_empty")]
    pub grpc: String,
}

pub async fn run_busproxy_registration(
    etcd: Arc<Mutex<MetaStoreClient>>,
    config: Arc<Config>,
) {
    let aid = if config.proxy_aid.trim().is_empty() {
        config.node_id.clone()
    } else {
        config.proxy_aid.clone()
    };
    let key = gen_busproxy_node_key(&config.busproxy_tenant_segment, &config.node_id);
    let val = match serde_json::to_vec(&BusProxyRegistration {
        aid,
        node: config.node_id.clone(),
        ak: config.proxy_access_key.clone(),
        grpc: config.advertise_grpc_endpoint(),
    }) {
        Ok(v) => v,
        Err(e) => {
            warn!(error = %e, "busproxy registration serialize");
            return;
        }
    };

    loop {
        let lease_ttl = config.busproxy_lease_ttl_sec.max(5) as i64;
        let lease_id = {
            let mut c = etcd.lock().await;
            match c.grant_lease(lease_ttl).await {
                Ok(r) => r.id(),
                Err(e) => {
                    warn!(error = %e, "busproxy grant lease");
                    tokio::time::sleep(std::time::Duration::from_secs(2)).await;
                    continue;
                }
            }
        };

        {
            let mut c = etcd.lock().await;
            if let Err(e) = c.put_with_lease(&key, &val, lease_id).await {
                warn!(error = %e, %key, "busproxy put");
                tokio::time::sleep(std::time::Duration::from_secs(2)).await;
                continue;
            }
        }

        info!(%key, lease_id, "busproxy registered in MetaStore");

        let tick = std::time::Duration::from_secs((lease_ttl as u64 / 3).max(1));
        let mut interval = tokio::time::interval(tick);
        let mut alive = true;
        while alive {
            interval.tick().await;
            let mut c = etcd.lock().await;
            match c.keep_alive_once(lease_id).await {
                Ok(_) => {}
                Err(e) => {
                    warn!(error = %e, "busproxy keepalive");
                    alive = false;
                }
            }
        }
    }
}
