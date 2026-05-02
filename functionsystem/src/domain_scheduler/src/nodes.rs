use std::collections::HashMap;
use std::sync::atomic::{AtomicI64, Ordering};
use std::sync::Arc;
use std::time::Duration;

use dashmap::DashMap;
use parking_lot::RwLock;
use serde::Deserialize;
use tonic::transport::{Channel, Endpoint};
use tonic::{Request, Status};
use tracing::{info, warn};
use yr_proto::internal::local_scheduler_service_client::LocalSchedulerServiceClient;
use yr_proto::internal::{
    global_scheduler_service_client::GlobalSchedulerServiceClient, RegisterResponse,
};
use yr_proto::internal::{
    EvictInstancesRequest, RegisterRequest, ScheduleRequest, ScheduleResponse,
};

use crate::config::DomainSchedulerConfig;
use crate::resource_view::{
    merge_topology_resource, merge_topology_resource_unit_b64, ResourceView,
};
use crate::scheduler_framework::NodeInfo;

#[derive(Debug, Clone, Deserialize)]
#[allow(dead_code)]
struct DomainRecord {
    id: String,
    address: String,
    local_count: u32,
}

#[derive(Debug, Clone, Deserialize)]
struct LocalNodeRecord {
    node_id: String,
    address: String,
    #[allow(dead_code)]
    domain_id: String,
    domain_address: String,
    resource_json: String,
    #[serde(default)]
    resource_unit_b64: String,
    #[allow(dead_code)]
    agent_info_json: String,
    #[allow(dead_code)]
    last_seen_ms: i64,
}

#[derive(Debug, Clone, Deserialize)]
struct TopologySnapshot {
    #[allow(dead_code)]
    domains: Vec<DomainRecord>,
    locals: Vec<LocalNodeRecord>,
}

struct NodeEntry {
    address: String,
    last_heartbeat: AtomicI64,
    healthy: RwLock<bool>,
    labels: RwLock<std::collections::HashMap<String, String>>,
    failure_domain: RwLock<Option<String>>,
}

pub struct LocalNodeManager {
    resource_view: Arc<ResourceView>,
    nodes: DashMap<String, NodeEntry>,
    clients: DashMap<String, LocalSchedulerServiceClient<Channel>>,
}

impl LocalNodeManager {
    pub fn new(resource_view: Arc<ResourceView>) -> Self {
        Self {
            resource_view,
            nodes: DashMap::new(),
            clients: DashMap::new(),
        }
    }

    pub fn upsert_local(
        &self,
        node_id: String,
        address: String,
        resource_json: &str,
        resource_unit_b64: Option<&str>,
    ) {
        if let Some(resource_unit_b64) = resource_unit_b64.filter(|value| !value.is_empty()) {
            merge_topology_resource_unit_b64(&node_id, resource_unit_b64, &self.resource_view);
        } else {
            merge_topology_resource(&node_id, resource_json, &self.resource_view);
        }
        let (json_labels, mut failure_domain) = parse_node_labels_and_domain(resource_json);
        let mut labels = self
            .resource_view
            .snapshot_unit(&node_id)
            .map(|unit| unit.labels)
            .unwrap_or_default();
        for (key, value) in json_labels {
            labels.entry(key).or_insert(value);
        }
        if failure_domain.is_none() {
            failure_domain = labels.get("zone").cloned();
        }
        self.nodes.insert(
            node_id.clone(),
            NodeEntry {
                address,
                last_heartbeat: AtomicI64::new(now_ms()),
                healthy: RwLock::new(true),
                labels: RwLock::new(labels),
                failure_domain: RwLock::new(failure_domain),
            },
        );
        info!(%node_id, "local node registered / updated");
    }

    pub fn remove_local(&self, node_id: &str) {
        self.nodes.remove(node_id);
        self.clients.remove(node_id);
        self.resource_view.remove_node(node_id);
    }

    pub fn on_heartbeat(&self, node_id: &str) {
        if let Some(n) = self.nodes.get(node_id) {
            n.last_heartbeat.store(now_ms(), Ordering::SeqCst);
            *n.healthy.write() = true;
        }
    }

    pub fn mark_unhealthy(&self, node_id: &str) {
        if let Some(n) = self.nodes.get(node_id) {
            *n.healthy.write() = false;
            warn!(%node_id, "local node marked unhealthy (missed heartbeats)");
        }
    }

    pub fn notify_worker_status(&self, node_id: &str, status: &str, reason: &str) {
        match status {
            "healthy" | "up" => {
                self.on_heartbeat(node_id);
            }
            "unhealthy" | "down" => self.mark_unhealthy(node_id),
            _ => {}
        }
        if !reason.is_empty() && (reason.starts_with('{') || reason.starts_with('[')) {
            self.resource_view.apply_resource_json(node_id, reason);
            merge_node_labels_from_worker_json(node_id, reason, &self.nodes);
        }
    }

    pub fn healthy_node_infos(&self) -> Vec<NodeInfo> {
        self.nodes
            .iter()
            .filter(|e| *e.healthy.read())
            .map(|e| NodeInfo {
                node_id: e.key().clone(),
                address: e.address.clone(),
                labels: e.labels.read().clone(),
                failure_domain: e.failure_domain.read().clone(),
            })
            .collect()
    }

    pub async fn get_client(
        &self,
        node_id: &str,
    ) -> Result<LocalSchedulerServiceClient<Channel>, Status> {
        if let Some(c) = self.clients.get(node_id) {
            return Ok(c.clone());
        }
        let addr = self
            .nodes
            .get(node_id)
            .map(|n| n.address.clone())
            .ok_or_else(|| Status::not_found(format!("unknown node_id {node_id}")))?;
        let uri = normalize_grpc_uri(&addr);
        let channel = Endpoint::from_shared(uri)
            .map_err(|e| Status::internal(format!("invalid address: {e}")))?
            .connect_timeout(Duration::from_secs(5))
            .connect()
            .await
            .map_err(|e| Status::unavailable(format!("connect failed: {e}")))?;
        let client = LocalSchedulerServiceClient::new(channel);
        self.clients.insert(node_id.to_string(), client.clone());
        Ok(client)
    }

    pub async fn forward_schedule(
        &self,
        node_id: &str,
        request: ScheduleRequest,
    ) -> Result<ScheduleResponse, Status> {
        let mut client = self.get_client(node_id).await?;
        let resp = client.schedule(Request::new(request)).await?;
        Ok(resp.into_inner())
    }

    pub async fn evict_instances(
        &self,
        node_id: &str,
        instance_ids: &[String],
        reason: &str,
    ) -> Result<yr_proto::internal::EvictInstancesResponse, Status> {
        if instance_ids.is_empty() {
            return Ok(yr_proto::internal::EvictInstancesResponse {
                success: true,
                evicted_ids: vec![],
            });
        }
        let mut client = self.get_client(node_id).await?;
        let resp = client
            .evict_instances(Request::new(EvictInstancesRequest {
                instance_ids: instance_ids.to_vec(),
                reason: reason.to_string(),
            }))
            .await?;
        Ok(resp.into_inner())
    }

    /// Apply topology returned by global scheduler; keep locals assigned to this domain's address.
    pub fn apply_topology_json(&self, topology_json: &str, domain_advertise_addr: &str) {
        let snap: TopologySnapshot = match serde_json::from_str(topology_json) {
            Ok(s) => s,
            Err(e) => {
                warn!(error = %e, "topology json parse failed");
                return;
            }
        };
        for l in snap.locals {
            if l.domain_address == domain_advertise_addr {
                self.upsert_local(
                    l.node_id,
                    l.address,
                    &l.resource_json,
                    Some(&l.resource_unit_b64),
                );
            }
        }
    }

    /// Marks stale nodes unhealthy; returns node_ids that transitioned to unhealthy.
    pub fn check_heartbeat_staleness(&self, max_age_ms: i64) -> Vec<String> {
        let now = now_ms();
        let mut newly = Vec::new();
        for e in self.nodes.iter() {
            let last = e.last_heartbeat.load(Ordering::SeqCst);
            if now.saturating_sub(last) > max_age_ms {
                let mut h = e.healthy.write();
                if *h {
                    *h = false;
                    newly.push(e.key().clone());
                    warn!(node_id = %e.key(), "local node marked unhealthy (missed heartbeats)");
                }
            }
        }
        newly
    }

    pub fn list_nodes_summary(&self) -> Vec<serde_json::Value> {
        self.nodes
            .iter()
            .map(|e| {
                serde_json::json!({
                    "node_id": e.key(),
                    "address": e.address,
                    "healthy": *e.healthy.read(),
                    "last_heartbeat_ms": e.last_heartbeat.load(Ordering::SeqCst),
                })
            })
            .collect()
    }
}

fn parse_node_labels_and_domain(resource_json: &str) -> (HashMap<String, String>, Option<String>) {
    let Ok(v) = serde_json::from_str::<serde_json::Value>(resource_json) else {
        return (HashMap::new(), None);
    };
    let Some(obj) = v.as_object() else {
        return (HashMap::new(), None);
    };
    let mut labels = HashMap::new();
    if let Some(l) = obj.get("labels").and_then(|x| x.as_object()) {
        for (k, val) in l {
            if let Some(s) = val.as_str() {
                labels.insert(k.clone(), s.to_string());
            }
        }
    }
    let fd = obj
        .get("failure_domain")
        .or_else(|| obj.get("zone"))
        .and_then(|x| x.as_str())
        .map(|s| s.to_string());
    (labels, fd)
}

fn merge_node_labels_from_worker_json(
    node_id: &str,
    json: &str,
    nodes: &DashMap<String, NodeEntry>,
) {
    let Ok(v) = serde_json::from_str::<serde_json::Value>(json) else {
        return;
    };
    let Some(n) = nodes.get(node_id) else {
        return;
    };
    if let Some(l) = v.get("labels").and_then(|x| x.as_object()) {
        let mut g = n.labels.write();
        for (k, val) in l {
            if let Some(s) = val.as_str() {
                g.insert(k.clone(), s.to_string());
            }
        }
    }
    if let Some(fd) = v
        .get("failure_domain")
        .or_else(|| v.get("zone"))
        .and_then(|x| x.as_str())
    {
        *n.failure_domain.write() = Some(fd.to_string());
    }
}

fn now_ms() -> i64 {
    use std::time::SystemTime;
    SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

fn normalize_grpc_uri(addr: &str) -> String {
    let a = addr.trim();
    if a.starts_with("http://") || a.starts_with("https://") {
        a.to_string()
    } else {
        format!("http://{a}")
    }
}

/// Register this domain scheduler with the global scheduler and merge returned topology.
pub async fn register_with_global(
    config: &DomainSchedulerConfig,
    nodes: &LocalNodeManager,
) -> anyhow::Result<Option<RegisterResponse>> {
    let addr = config.global_scheduler_address.trim();
    if addr.is_empty() {
        return Ok(None);
    }
    let uri = normalize_grpc_uri(addr);
    let channel = Endpoint::from_shared(uri)?
        .connect_timeout(Duration::from_secs(5))
        .connect()
        .await?;
    let mut client = GlobalSchedulerServiceClient::new(channel);
    let agent = serde_json::json!({
        "role": "domain_scheduler",
        "node_id": config.node_id,
    })
    .to_string();
    let req = RegisterRequest {
        node_id: config.node_id.clone(),
        address: config.advertise_grpc_addr(),
        resource_json: "{}".into(),
        agent_info_json: agent,
        resource_unit: None,
    };
    let resp = client
        .register(Request::new(req))
        .await
        .map_err(|e| anyhow::anyhow!("global register: {e}"))?;
    let inner = resp.into_inner();
    if !inner.topology.is_empty() {
        nodes.apply_topology_json(&inner.topology, &config.advertise_grpc_addr());
    }
    Ok(Some(inner))
}
