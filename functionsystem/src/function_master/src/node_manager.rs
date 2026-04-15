//! Proxy node registry, heartbeat timing, and failure tracking (C++ node / heartbeat manager analogue).

use std::sync::Arc;

use dashmap::DashMap;
use serde_json::json;

/// Per-proxy registration snapshot for health APIs.
#[derive(Debug, Clone)]
pub struct ProxyNodeRecord {
    pub node_id: String,
    pub address: String,
    pub domain_id: String,
    pub last_seen_ms: i64,
    pub failure_count: u32,
}

/// Tracks proxy registrations and last heartbeat for `/resources` and diagnostics.
pub struct NodeManager {
    proxies: DashMap<String, ProxyNodeRecord>,
}

impl NodeManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            proxies: DashMap::new(),
        })
    }

    pub fn on_proxy_register(
        &self,
        node_id: impl Into<String>,
        address: impl Into<String>,
        domain_id: impl Into<String>,
    ) {
        let node_id = node_id.into();
        let now = now_ms();
        let mut entry = self
            .proxies
            .entry(node_id.clone())
            .or_insert(ProxyNodeRecord {
                node_id: node_id.clone(),
                address: String::new(),
                domain_id: String::new(),
                last_seen_ms: now,
                failure_count: 0,
            });
        entry.address = address.into();
        entry.domain_id = domain_id.into();
        entry.last_seen_ms = now;
        entry.failure_count = 0;
    }

    pub fn touch(&self, node_id: &str) {
        let now = now_ms();
        if let Some(mut e) = self.proxies.get_mut(node_id) {
            e.last_seen_ms = now;
            e.failure_count = 0;
        }
    }

    pub fn record_failure(&self, node_id: &str) -> u32 {
        if let Some(mut e) = self.proxies.get_mut(node_id) {
            e.failure_count = e.failure_count.saturating_add(1);
            return e.failure_count;
        }
        let now = now_ms();
        self.proxies.insert(
            node_id.to_string(),
            ProxyNodeRecord {
                node_id: node_id.to_string(),
                address: String::new(),
                domain_id: String::new(),
                last_seen_ms: now,
                failure_count: 1,
            },
        );
        1
    }

    pub fn remove(&self, node_id: &str) {
        self.proxies.remove(node_id);
    }

    pub fn len(&self) -> usize {
        self.proxies.len()
    }

    /// Proxies with no heartbeat within `timeout_ms` (wall clock).
    pub fn stale_nodes(&self, timeout_ms: i64) -> Vec<String> {
        let now = now_ms();
        self.proxies
            .iter()
            .filter(|e| now.saturating_sub(e.last_seen_ms) > timeout_ms)
            .map(|e| e.node_id.clone())
            .collect()
    }

    /// Nodes whose failure count meets or exceeds `max_failures`.
    pub fn unhealthy_nodes(&self, max_failures: u32) -> Vec<String> {
        if max_failures == 0 {
            return vec![];
        }
        self.proxies
            .iter()
            .filter(|e| e.failure_count >= max_failures)
            .map(|e| e.node_id.clone())
            .collect()
    }

    pub fn summary_json(&self) -> serde_json::Value {
        let mut rows: Vec<ProxyNodeRecord> =
            self.proxies.iter().map(|e| e.value().clone()).collect();
        rows.sort_by(|a, b| a.node_id.cmp(&b.node_id));
        json!({
            "proxy_count": rows.len(),
            "proxies": rows.into_iter().map(|r| json!({
                "node_id": r.node_id,
                "address": r.address,
                "domain_id": r.domain_id,
                "last_seen_ms": r.last_seen_ms,
                "failure_count": r.failure_count,
            })).collect::<Vec<_>>(),
        })
    }
}

fn now_ms() -> i64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}
