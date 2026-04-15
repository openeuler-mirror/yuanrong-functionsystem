//! Per-instance proxy shells; routing table lives on [`super::BusProxyCoordinator`].

use super::instance_proxy::InstanceProxy;
use dashmap::DashMap;
use serde::Deserialize;
use std::sync::Arc;

#[derive(Debug, Clone, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RouteJson {
    #[serde(default)]
    pub node_id: Option<String>,
    #[serde(default)]
    pub node: Option<String>,
    #[serde(default)]
    pub address: Option<String>,
    #[serde(default)]
    pub proxy_address: Option<String>,
}

impl RouteJson {
    pub fn owner_node(&self) -> Option<String> {
        self.node_id
            .clone()
            .or_else(|| self.node.clone())
            .filter(|s| !s.is_empty())
    }

    pub fn endpoint(&self) -> Option<String> {
        self.proxy_address
            .clone()
            .or_else(|| self.address.clone())
            .filter(|s| !s.is_empty())
    }
}

#[derive(Debug, Clone, Default)]
pub struct InstanceRouteRecord {
    pub owner_node_id: String,
    pub proxy_endpoint: Option<String>,
}

#[derive(Debug)]
pub struct InstanceView {
    local_node: String,
    proxies: DashMap<String, Arc<InstanceProxy>>,
}

impl InstanceView {
    pub fn new(local_node: impl Into<String>) -> Self {
        Self {
            local_node: local_node.into(),
            proxies: DashMap::new(),
        }
    }

    pub fn local_node(&self) -> &str {
        &self.local_node
    }

    pub fn ensure_proxy(&self, instance_id: &str) -> Arc<InstanceProxy> {
        self.proxies
            .entry(instance_id.to_string())
            .or_insert_with(|| Arc::new(InstanceProxy::new(instance_id.to_string())))
            .clone()
    }

    pub fn mark_route_ready(&self, instance_id: &str) {
        let p = self.ensure_proxy(instance_id);
        p.dispatcher.set_route_ready(true);
    }

    pub fn remove_proxy(&self, instance_id: &str) {
        self.proxies.remove(instance_id);
    }

    pub(crate) fn proxies(&self) -> &DashMap<String, Arc<InstanceProxy>> {
        &self.proxies
    }
}
