use std::collections::HashMap;
use std::time::{Duration, Instant};

use dashmap::DashMap;
use parking_lot::Mutex;
use serde::Serialize;
use tracing::warn;

/// Logical resources for one worker: capacity, reported used, in-flight reservations.
#[derive(Debug, Clone, Default, Serialize)]
pub struct ResourceUnit {
    pub capacity: HashMap<String, f64>,
    pub used: HashMap<String, f64>,
}

impl ResourceUnit {
    pub fn available(&self, inflight_total: &HashMap<String, f64>) -> HashMap<String, f64> {
        let mut out = HashMap::new();
        for (k, cap) in &self.capacity {
            let u = self.used.get(k).copied().unwrap_or(0.0);
            let infl = inflight_total.get(k).copied().unwrap_or(0.0);
            out.insert(k.clone(), (cap - u - infl).max(0.0));
        }
        out
    }

    pub fn has_room(&self, need: &HashMap<String, f64>, inflight_total: &HashMap<String, f64>) -> bool {
        let avail = self.available(inflight_total);
        for (k, v) in need {
            if *v <= 0.0 {
                continue;
            }
            if avail.get(k).copied().unwrap_or(0.0) + 1e-9 < *v {
                return false;
            }
        }
        true
    }

    pub fn free_score(&self, inflight_total: &HashMap<String, f64>) -> f64 {
        self.available(inflight_total).values().sum()
    }
}

struct InflightEntry {
    amounts: HashMap<String, f64>,
    created: Instant,
    ttl: Duration,
}

struct NodeResourceState {
    unit: ResourceUnit,
    inflight: HashMap<String, InflightEntry>,
}

impl NodeResourceState {
    fn inflight_totals(&self) -> HashMap<String, f64> {
        let mut t = HashMap::new();
        for e in self.inflight.values() {
            for (k, v) in &e.amounts {
                *t.entry(k.clone()).or_insert(0.0) += v;
            }
        }
        t
    }

    fn purge_expired(&mut self, now: Instant) {
        self.inflight.retain(|_, e| now.duration_since(e.created) < e.ttl);
    }
}

/// Per-node resource tracking with in-flight schedule reservations.
pub struct ResourceView {
    nodes: DashMap<String, Mutex<NodeResourceState>>,
}

impl ResourceView {
    pub fn new() -> Self {
        Self {
            nodes: DashMap::new(),
        }
    }

    pub fn upsert_node_resources(&self, node_id: &str, capacity: HashMap<String, f64>, used: HashMap<String, f64>) {
        self.nodes
            .entry(node_id.to_string())
            .or_insert_with(|| {
                Mutex::new(NodeResourceState {
                    unit: ResourceUnit::default(),
                    inflight: HashMap::new(),
                })
            })
            .lock()
            .unit = ResourceUnit { capacity, used };
    }

    pub fn apply_resource_json(&self, node_id: &str, resource_json: &str) {
        let (cap, used) = parse_resource_json(resource_json);
        if cap.is_empty() && used.is_empty() {
            return;
        }
        self.upsert_node_resources(node_id, cap, used);
    }

    pub fn remove_node(&self, node_id: &str) {
        self.nodes.remove(node_id);
    }

    pub fn snapshot_unit(&self, node_id: &str) -> Option<ResourceUnit> {
        self.nodes.get(node_id).map(|e| e.lock().unit.clone())
    }

    pub fn domain_summary(&self) -> serde_json::Value {
        let mut nodes = Vec::new();
        for r in self.nodes.iter() {
            let g = r.lock();
            let infl = g.inflight_totals();
            let avail = g.unit.available(&infl);
            nodes.push(serde_json::json!({
                "node_id": r.key(),
                "capacity": g.unit.capacity,
                "used": g.unit.used,
                "inflight": infl,
                "available": avail,
            }));
        }
        serde_json::json!({ "nodes": nodes })
    }

    pub fn try_reserve(
        &self,
        node_id: &str,
        request_id: &str,
        amounts: &HashMap<String, f64>,
        ttl: Duration,
    ) -> bool {
        let Some(entry) = self.nodes.get(node_id) else {
            return false;
        };
        let mut g = entry.lock();
        g.purge_expired(Instant::now());
        let infl = g.inflight_totals();
        if !g.unit.has_room(amounts, &infl) {
            return false;
        }
        g.inflight.insert(
            request_id.to_string(),
            InflightEntry {
                amounts: amounts.clone(),
                created: Instant::now(),
                ttl,
            },
        );
        true
    }

    pub fn release_reservation(&self, node_id: &str, request_id: &str) {
        let Some(entry) = self.nodes.get(node_id) else {
            return;
        };
        entry.lock().inflight.remove(request_id);
    }

    pub fn commit_reservation(&self, node_id: &str, request_id: &str) {
        let Some(entry) = self.nodes.get(node_id) else {
            return;
        };
        let mut g = entry.lock();
        let Some(taken) = g.inflight.remove(request_id) else {
            return;
        };
        for (k, v) in taken.amounts {
            *g.unit.used.entry(k).or_insert(0.0) += v;
        }
    }

    pub fn release_expired_inflight(&self) {
        let now = Instant::now();
        for e in self.nodes.iter() {
            e.lock().purge_expired(now);
        }
    }

    pub fn node_ids(&self) -> Vec<String> {
        self.nodes.iter().map(|e| e.key().clone()).collect()
    }

    pub fn has_room_for(&self, node_id: &str, need: &HashMap<String, f64>) -> bool {
        let Some(entry) = self.nodes.get(node_id) else {
            return false;
        };
        let g = entry.lock();
        let infl = g.inflight_totals();
        g.unit.has_room(need, &infl)
    }

    pub fn free_score(&self, node_id: &str) -> f64 {
        let Some(entry) = self.nodes.get(node_id) else {
            return f64::NEG_INFINITY;
        };
        let g = entry.lock();
        let infl = g.inflight_totals();
        g.unit.free_score(&infl)
    }

    /// Hook after instance eviction; usage is normally refreshed via worker `resource_json` updates.
    pub fn release_instance_usage(&self, _node_id: &str, _instance_id: &str) {
        // No per-instance accounting in this view yet; keep API for PreemptionController parity.
    }
}

impl Default for ResourceView {
    fn default() -> Self {
        Self::new()
    }
}

fn parse_resource_json(s: &str) -> (HashMap<String, f64>, HashMap<String, f64>) {
    let v: serde_json::Value = match serde_json::from_str(s) {
        Ok(x) => x,
        Err(_) => return (HashMap::new(), HashMap::new()),
    };
    let obj = match v.as_object() {
        Some(o) => o,
        None => return (HashMap::new(), HashMap::new()),
    };
    if let (Some(c), Some(u)) = (obj.get("capacity"), obj.get("used")) {
        return (json_f64_map(c), json_f64_map(u));
    }
    (json_f64_map(&serde_json::Value::Object(obj.clone())), HashMap::new())
}

fn json_f64_map(v: &serde_json::Value) -> HashMap<String, f64> {
    let mut m = HashMap::new();
    let Some(o) = v.as_object() else {
        return m;
    };
    for (k, val) in o {
        if let Some(n) = val.as_f64() {
            m.insert(k.clone(), n);
        } else if let Some(n) = val.as_i64() {
            m.insert(k.clone(), n as f64);
        }
    }
    m
}

pub fn merge_topology_resource(node_id: &str, resource_json: &str, view: &ResourceView) {
    if resource_json.is_empty() {
        warn!(%node_id, "empty resource_json for node");
        return;
    }
    view.apply_resource_json(node_id, resource_json);
}
