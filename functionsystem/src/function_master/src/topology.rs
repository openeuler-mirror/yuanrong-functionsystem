use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

use dashmap::DashMap;
use parking_lot::Mutex;
use serde::{Deserialize, Serialize};
use tokio::sync::mpsc;
use tracing::{debug, warn};
use yr_metastore_client::MetaStoreClient;

use crate::config::{AssignmentStrategy, MasterConfig};
use crate::sched_node::NodeInfo;
use crate::sched_tree::{RecoverError, SchedTree};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LocalNodeRecord {
    pub node_id: String,
    pub address: String,
    pub domain_id: String,
    pub domain_address: String,
    pub resource_json: String,
    pub agent_info_json: String,
    pub last_seen_ms: i64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct TopologySnapshot {
    pub locals: Vec<LocalNodeRecord>,
}

/// Scheduler topology + agent metadata, persisted as protobuf `SchedulerNode` at `SCHEDULER_TOPOLOGY`.
pub struct TopologyManager {
    config: Arc<MasterConfig>,
    metastore: Option<Arc<tokio::sync::Mutex<MetaStoreClient>>>,
    tree: Arc<SchedTree>,
    locals: DashMap<String, LocalNodeRecord>,
    domain_seq: AtomicU64,
    persist_tx: Mutex<Option<mpsc::UnboundedSender<Vec<u8>>>>,
}

impl TopologyManager {
    pub fn new(
        config: Arc<MasterConfig>,
        metastore: Option<Arc<tokio::sync::Mutex<MetaStoreClient>>>,
    ) -> Self {
        let tree = Arc::new(SchedTree::new(
            config.max_locals_per_domain as usize,
            config.max_domain_sched_per_domain as usize,
        ));
        let mut persist_tx = None;
        if let Some(ms) = metastore.clone() {
            let (tx, rx) = mpsc::unbounded_channel::<Vec<u8>>();
            let key = MasterConfig::topology_logical_key().to_string();
            tokio::spawn(async move { Self::persist_worker(rx, ms, key).await });
            persist_tx = Some(tx);
        }
        Self {
            config,
            metastore,
            tree,
            locals: DashMap::new(),
            domain_seq: AtomicU64::new(0),
            persist_tx: Mutex::new(persist_tx),
        }
    }

    pub fn sched_tree(&self) -> Arc<SchedTree> {
        self.tree.clone()
    }

    async fn persist_worker(
        mut rx: mpsc::UnboundedReceiver<Vec<u8>>,
        ms: Arc<tokio::sync::Mutex<MetaStoreClient>>,
        key: String,
    ) {
        while let Some(mut bytes) = rx.recv().await {
            while let Ok(more) = rx.try_recv() {
                bytes = more;
            }
            let mut g = ms.lock().await;
            if let Err(e) = g.put(&key, &bytes).await {
                warn!(error = %e, key = %key, "topology: protobuf put failed");
            }
        }
    }

    fn queue_persist(&self) {
        let bytes = self.tree.serialize_as_bytes();
        let tx_opt = self.persist_tx.lock();
        if let Some(tx) = tx_opt.as_ref() {
            let _ = tx.send(bytes);
        }
    }

    pub async fn load_from_etcd(&self) {
        let Some(ms) = &self.metastore else {
            return;
        };
        let key = MasterConfig::topology_logical_key();
        let data = {
            let mut g = ms.lock().await;
            match g.get(key).await {
                Ok(resp) => resp.kvs.into_iter().next().map(|kv| kv.value),
                Err(e) => {
                    warn!(error = %e, key = key, "topology: etcd get failed");
                    return;
                }
            }
        };
        let Some(bytes) = data else {
            debug!(key = key, "topology: no snapshot in etcd");
            return;
        };
        if let Err(e) = self.tree.recover_from_bytes(&bytes) {
            warn!(?e, "topology: protobuf recover failed");
            return;
        }
        self.rebuild_locals_index_from_tree();
    }

    /// Apply remote watch update (same format as persistence).
    pub fn apply_topology_bytes(&self, bytes: &[u8]) {
        if bytes.is_empty() {
            return;
        }
        if self.tree.recover_from_bytes(bytes).is_err() {
            warn!("topology: watch apply decode failed");
            return;
        }
        self.rebuild_locals_index_from_tree();
    }

    fn rebuild_locals_index_from_tree(&self) {
        let leaves = self.tree.find_nodes(0);
        for (name, node) in leaves {
            let parent = match node.parent() {
                Some(p) => p,
                None => continue,
            };
            let rec = LocalNodeRecord {
                node_id: name.clone(),
                address: node.address(),
                domain_id: parent.name(),
                domain_address: parent.address(),
                resource_json: "{}".into(),
                agent_info_json: "{}".into(),
                last_seen_ms: now_ms(),
            };
            self.locals.insert(name, rec);
        }
    }

    fn ensure_domain_layer(&self) {
        if self.tree.get_root_node().is_some() {
            return;
        }
        let n = self.domain_seq.fetch_add(1, Ordering::SeqCst);
        self.tree.add_non_leaf_node(NodeInfo {
            name: format!("slot-{n}"),
            address: self.config.default_domain_address.clone(),
        });
    }

    fn add_sibling_domain(&self) {
        let n = self.domain_seq.fetch_add(1, Ordering::SeqCst);
        self.tree.add_non_leaf_node(NodeInfo {
            name: format!("slot-{n}"),
            address: self.config.default_domain_address.clone(),
        });
    }

    pub async fn register_local(
        &self,
        node_id: String,
        address: String,
        resource_json: String,
        agent_info_json: String,
    ) -> (String, LocalNodeRecord) {
        let now = now_ms();
        self.ensure_domain_layer();

        if let Some(mut existing) = self.locals.get_mut(&node_id) {
            existing.address = address.clone();
            existing.resource_json = resource_json;
            existing.agent_info_json = agent_info_json;
            existing.last_seen_ms = now;
            let rec = existing.clone();
            let dom_addr = rec.domain_address.clone();
            drop(existing);
            if let Some(leaf) = self.tree.find_leaf_node(&node_id) {
                leaf.set_node_info(NodeInfo {
                    name: node_id.clone(),
                    address: address.clone(),
                });
            }
            self.queue_persist();
            return (dom_addr, rec);
        }

        let info = NodeInfo {
            name: node_id.clone(),
            address: address.clone(),
        };

        // One `add_non_leaf` may only promote the root; keep adding domains until a leaf slot exists
        // (matches C++ domain activation + multi-level `SchedTree` behavior).
        let mut leaf = None;
        for _ in 0..32 {
            leaf = self.tree.add_leaf_node(info.clone());
            if leaf.is_some() {
                break;
            }
            match self.config.assignment_strategy {
                AssignmentStrategy::RoundRobin | AssignmentStrategy::LeastLoaded => {
                    self.add_sibling_domain();
                }
            }
        }
        let Some(leaf) = leaf else {
            let rec = LocalNodeRecord {
                node_id: node_id.clone(),
                address: info.address.clone(),
                domain_id: String::new(),
                domain_address: String::new(),
                resource_json: resource_json.clone(),
                agent_info_json: agent_info_json.clone(),
                last_seen_ms: now,
            };
            return (String::new(), rec);
        };
        let Some(parent) = leaf.parent() else {
            warn!(
                %node_id,
                "topology: leaf has no parent domain — treating as unassigned"
            );
            let rec = LocalNodeRecord {
                node_id: node_id.clone(),
                address: info.address.clone(),
                domain_id: String::new(),
                domain_address: String::new(),
                resource_json: resource_json.clone(),
                agent_info_json: agent_info_json.clone(),
                last_seen_ms: now,
            };
            return (String::new(), rec);
        };

        let rec = LocalNodeRecord {
            node_id: node_id.clone(),
            address,
            domain_id: parent.name(),
            domain_address: parent.address(),
            resource_json,
            agent_info_json,
            last_seen_ms: now,
        };
        self.locals.insert(node_id, rec.clone());
        self.queue_persist();
        (rec.domain_address.clone(), rec)
    }

    pub async fn update_resources(&self, node_id: &str, resource_json: String) -> bool {
        let Some(mut v) = self.locals.get_mut(node_id) else {
            return false;
        };
        v.resource_json = resource_json;
        v.last_seen_ms = now_ms();
        drop(v);
        self.queue_persist();
        true
    }

    pub async fn evict(&self, node_id: &str) -> bool {
        let removed = self.tree.remove_leaf_node(node_id);
        if removed.is_none() && self.locals.get(node_id).is_none() {
            return false;
        }
        self.locals.remove(node_id);
        self.queue_persist();
        true
    }

    pub fn list_agents_json(&self, filter: &str) -> String {
        let mut rows: Vec<LocalNodeRecord> = self
            .locals
            .iter()
            .map(|e| e.value().clone())
            .collect();
        rows.sort_by(|a, b| a.node_id.cmp(&b.node_id));
        let rows: Vec<LocalNodeRecord> = if filter.is_empty() {
            rows
        } else {
            rows
                .into_iter()
                .filter(|r| {
                    r.node_id.contains(filter)
                        || r.address.contains(filter)
                        || r.domain_id.contains(filter)
                })
                .collect()
        };
        serde_json::to_string(&rows).unwrap_or_else(|_| "[]".into())
    }

    pub fn agent_count(&self) -> i64 {
        self.locals.len() as i64
    }

    /// Snapshot of registered proxy (local scheduler) rows for resource aggregation / node health.
    pub fn locals_snapshot(&self) -> Vec<LocalNodeRecord> {
        let mut rows: Vec<LocalNodeRecord> = self
            .locals
            .iter()
            .map(|e| e.value().clone())
            .collect();
        rows.sort_by(|a, b| a.node_id.cmp(&b.node_id));
        rows
    }

    pub fn topology_json(&self) -> String {
        let snap = TopologySnapshot {
            locals: self
                .locals
                .iter()
                .map(|e| e.value().clone())
                .collect(),
        };
        serde_json::to_string_pretty(&snap).unwrap_or_else(|_| "{}".to_string())
    }

    pub fn resource_summary_json(&self) -> serde_json::Value {
        use crate::resource_agg::ResourceAggregator;
        let mut base = ResourceAggregator::merged_json(self);
        if let Some(obj) = base.as_object_mut() {
            let mut total_nodes = 0i64;
            let mut sample: Vec<serde_json::Value> = Vec::new();
            for e in self.locals.iter() {
                total_nodes += 1;
                if sample.len() < 32 {
                    sample.push(serde_json::json!({
                        "node_id": e.node_id,
                        "resource_json": e.resource_json,
                    }));
                }
            }
            obj.insert("node_count".into(), serde_json::json!(total_nodes));
            obj.insert("nodes".into(), serde_json::json!(sample));
        }
        base
    }

    pub fn root_domain(&self) -> Option<(String, String)> {
        self.tree
            .get_root_node()
            .map(|n| (n.name(), n.address()))
    }

    pub fn recover_tree_from_bytes(&self, bytes: &[u8]) -> Result<(), RecoverError> {
        self.tree.recover_from_bytes(bytes)
    }
}

fn now_ms() -> i64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

/// Watch `with_prefix(prefix, SCHEDULER_TOPOLOGY)` via MetaStore and refresh the in-memory [`SchedTree`].
pub fn spawn_metastore_topology_watch(
    topology: Arc<TopologyManager>,
    metastore: Arc<tokio::sync::Mutex<MetaStoreClient>>,
    cancel: tokio_util::sync::CancellationToken,
) {
    let logical = MasterConfig::topology_logical_key().to_string();
    tokio::spawn(async move {
        loop {
            tokio::select! {
                _ = cancel.cancelled() => break,
                _ = async {
                    use futures::StreamExt;
                    use yr_metastore_client::WatchEventType;
                    let stream = {
                        let mut g = metastore.lock().await;
                        g.watch_prefix(logical.clone())
                    };
                    let mut stream = stream;
                    while let Some(item) = stream.next().await {
                        match item {
                            Ok(ev) if matches!(ev.event_type, WatchEventType::Put) => {
                                topology.apply_topology_bytes(&ev.value);
                            }
                            Ok(_) => {}
                            Err(e) => {
                                warn!(error = %e, "topology watch: stream error, reconnecting");
                                break;
                            }
                        }
                    }
                } => {}
            }
        }
    });
}
