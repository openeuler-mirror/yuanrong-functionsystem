//! Instance / metadata watches (`instance_manager_actor.cpp` analogue).

use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::Arc;

use dashmap::DashMap;
use futures::StreamExt;
use serde_json::Value;
use tokio_util::sync::CancellationToken;
use tracing::warn;
use yr_metastore_client::{MetaStoreClient, WatchEvent, WatchEventType};

use crate::snapshot::{maybe_record_snapshot_transition, SnapshotManager};

/// Family-based caches for instance keys, function metadata, busproxy, and abnormal schedulers.
pub struct InstanceManager {
    leader: Arc<AtomicBool>,
    generation: AtomicU64,
    instances: DashMap<String, Value>,
    func_meta: DashMap<String, Value>,
    busproxy: DashMap<String, Value>,
    abnormal: DashMap<String, ()>,
    snapshots: Arc<SnapshotManager>,
    /// When set, leader upserts mirror instance JSON back to MetaStore (same key as watch path).
    metastore: Option<Arc<tokio::sync::Mutex<MetaStoreClient>>>,
}

impl InstanceManager {
    pub fn new(
        leader: Arc<AtomicBool>,
        snapshots: Arc<SnapshotManager>,
        metastore: Option<Arc<tokio::sync::Mutex<MetaStoreClient>>>,
    ) -> Self {
        Self {
            leader,
            generation: AtomicU64::new(0),
            instances: DashMap::new(),
            func_meta: DashMap::new(),
            busproxy: DashMap::new(),
            abnormal: DashMap::new(),
            snapshots,
            metastore,
        }
    }

    pub fn generation(&self) -> u64 {
        self.generation.load(Ordering::SeqCst)
    }

    pub fn on_metastore_reconnect(&self) {
        self.generation.fetch_add(1, Ordering::SeqCst);
    }

    fn apply_event(&self, prefix_label: &str, ev: WatchEvent) {
        let key = String::from_utf8_lossy(&ev.key).to_string();
        match ev.event_type {
            WatchEventType::Put => {
                let v: Value = serde_json::from_slice(&ev.value).unwrap_or_else(|_| {
                    Value::String(String::from_utf8_lossy(&ev.value).into_owned())
                });
                match prefix_label {
                    "instance" => self.upsert_instance(&key, v),
                    "func_meta" => {
                        self.func_meta.insert(key, v);
                    }
                    "busproxy" => {
                        self.busproxy.insert(key, v);
                    }
                    "abnormal" => {
                        let id = String::from_utf8_lossy(&ev.value).to_string();
                        self.abnormal.insert(id, ());
                    }
                    _ => {}
                }
            }
            WatchEventType::Delete => match prefix_label {
                "instance" => self.remove_instance_key(&key),
                "func_meta" => {
                    self.func_meta.remove(&key);
                }
                "busproxy" => {
                    self.busproxy.remove(&key);
                }
                "abnormal" => {
                    if let Some(pv) = ev.prev_value {
                        let id = String::from_utf8_lossy(&pv).to_string();
                        self.abnormal.remove(&id);
                    }
                }
                _ => {}
            },
        }
    }

    pub fn upsert_instance(&self, key: &str, value: Value) {
        let id = extract_instance_id(key);
        let old = self.instances.get(&id).map(|e| e.value().clone());
        self.instances.insert(id, value.clone());
        maybe_record_snapshot_transition(&self.snapshots, old.as_ref(), &value);
        if self.leader.load(Ordering::SeqCst) {
            if let Some(ms) = self.metastore.clone() {
                let key = key.to_string();
                let payload = value.clone();
                tokio::spawn(async move {
                    let bytes = serde_json::to_vec(&payload).unwrap_or_default();
                    let mut g = ms.lock().await;
                    if let Err(e) = g.put(&key, &bytes).await {
                        warn!(error = %e, %key, "instances: metastore mirror put failed");
                    }
                });
            }
        }
    }

    pub fn remove_instance_key(&self, key: &str) {
        let id = extract_instance_id(key);
        self.instances.remove(&id);
        if self.leader.load(Ordering::SeqCst) {
            if let Some(ms) = self.metastore.clone() {
                let key = key.to_string();
                tokio::spawn(async move {
                    let mut g = ms.lock().await;
                    if let Err(e) = g.delete(&key).await {
                        warn!(error = %e, %key, "instances: metastore mirror delete failed");
                    }
                });
            }
        }
    }

    pub fn list_json(&self) -> String {
        let mut pairs: Vec<(String, Value)> = self
            .instances
            .iter()
            .map(|e| (e.key().clone(), e.value().clone()))
            .collect();
        pairs.sort_by(|a, b| a.0.cmp(&b.0));
        let obj: serde_json::Map<String, Value> = pairs.into_iter().collect();
        serde_json::to_string(&obj).unwrap_or_else(|_| "{}".into())
    }

    pub fn count(&self) -> usize {
        self.instances.len()
    }

    /// Instances belonging to a scheduling group (`group_id` / `groupID` / `groupId` in JSON).
    pub fn query_by_group(&self, group_id: &str) -> Vec<Value> {
        if group_id.is_empty() {
            return vec![];
        }
        self.instances
            .iter()
            .filter(|e| {
                extract_group_id(e.value())
                    .map(|g| g == group_id)
                    .unwrap_or(false)
            })
            .map(|e| e.value().clone())
            .collect()
    }

    /// Rough lifecycle phase string derived from instance JSON (C++ state machine analogue for HTTP/debug).
    pub fn lifecycle_phase(value: &Value) -> &'static str {
        let Some(o) = value.as_object() else {
            return "unknown";
        };
        let status = o
            .get("status")
            .or_else(|| o.get("phase"))
            .or_else(|| o.get("state"));
        let s = status
            .and_then(|v| v.as_str())
            .unwrap_or("")
            .to_ascii_lowercase();
        match s.as_str() {
            "" => "unspecified",
            "pending" | "scheduling" | "scheduled" => "pending",
            "running" | "active" => "running",
            "stopping" | "terminating" => "stopping",
            "stopped" | "completed" | "failed" | "error" => "terminal",
            _ => "other",
        }
    }

    pub fn query_by_tenant(&self, tenant_id: &str, instance_id: Option<&str>) -> (Vec<Value>, usize) {
        let matched: Vec<Value> = self
            .instances
            .iter()
            .filter(|e| {
                let v = e.value();
                let tid = v.get("tenant").and_then(|t| t.as_str()).unwrap_or("");
                if tid != tenant_id {
                    return false;
                }
                if let Some(iid) = instance_id {
                    let eid = v.get("id").and_then(|i| i.as_str()).unwrap_or("");
                    return eid == iid;
                }
                true
            })
            .map(|e| e.value().clone())
            .collect();
        let count = matched.len();
        (matched, count)
    }

    pub fn query_named_instances(&self, request_id: &str) -> (Vec<Value>, String) {
        let named: Vec<Value> = self
            .instances
            .iter()
            .filter(|e| {
                e.value()
                    .get("designated_instance_id")
                    .and_then(|v| v.as_str())
                    .map(|s| !s.is_empty())
                    .unwrap_or(false)
            })
            .map(|e| e.value().clone())
            .collect();
        (named, request_id.to_string())
    }

    pub fn query_debug_instances(&self) -> Vec<Value> {
        self.instances
            .iter()
            .map(|e| {
                let v = e.value().clone();
                let mut obj = v.as_object().cloned().unwrap_or_default();
                obj.insert("key".into(), serde_json::Value::String(e.key().clone()));
                serde_json::Value::Object(obj)
            })
            .collect()
    }

    pub fn is_scheduler_abnormal(&self, proxy_id: &str) -> bool {
        self.abnormal.contains_key(proxy_id)
    }

    /// Master-only mutation path placeholder (kill / forward).
    pub fn try_forward_or_kill(&self, _instance_id: &str) -> bool {
        self.leader.load(Ordering::SeqCst)
    }

    fn spawn_prefix_watch(
        this: Arc<Self>,
        metastore: Arc<tokio::sync::Mutex<MetaStoreClient>>,
        physical_prefix: String,
        label: &'static str,
        cancel: CancellationToken,
    ) {
        tokio::spawn(async move {
            loop {
                tokio::select! {
                    _ = cancel.cancelled() => break,
                    _ = async {
                        let stream = {
                            let mut g = metastore.lock().await;
                            g.watch_prefix(physical_prefix.clone())
                        };
                        let mut stream = stream;
                        while let Some(item) = stream.next().await {
                            match item {
                                Ok(ev) => this.apply_event(label, ev),
                                Err(e) => {
                                    warn!(error = %e, prefix = %physical_prefix, "instances: watch error, reconnecting");
                                    break;
                                }
                            }
                        }
                    } => {}
                }
            }
        });
    }

    /// Start watches for instance, function metadata, busproxy, and abnormal scheduler prefixes.
    pub fn spawn_meta_watches(
        this: Arc<Self>,
        metastore: Arc<tokio::sync::Mutex<MetaStoreClient>>,
        prefixes: Vec<(String, &'static str)>,
        cancel: CancellationToken,
    ) {
        for (pfx, label) in prefixes {
            Self::spawn_prefix_watch(this.clone(), metastore.clone(), pfx, label, cancel.child_token());
        }
    }
}

fn extract_instance_id(key: &str) -> String {
    key.rsplit('/').next().unwrap_or(key).to_string()
}

fn extract_group_id(v: &Value) -> Option<String> {
    let o = v.as_object()?;
    for k in ["group_id", "groupID", "groupId", "r_group_id", "rGroupId"] {
        if let Some(Value::String(s)) = o.get(k) {
            if !s.is_empty() {
                return Some(s.clone());
            }
        }
    }
    None
}
