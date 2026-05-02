use std::collections::HashMap;
use std::time::{Duration, Instant};

use base64::Engine;
use dashmap::DashMap;
use parking_lot::Mutex;
use prost::Message;
use serde::Serialize;
use serde_json::{Map as JsonMap, Value as JsonValue};
use tracing::warn;
use yr_proto::resources::ResourceUnit as ProtoResourceUnit;

/// Logical resources for one worker: capacity, reported used, in-flight reservations.
#[derive(Debug, Clone, Default, Serialize)]
pub struct ResourceUnit {
    pub capacity: HashMap<String, f64>,
    pub used: HashMap<String, f64>,
    pub allocatable: HashMap<String, f64>,
    pub labels: HashMap<String, String>,
    pub vectors: JsonMap<String, JsonValue>,
    pub instances: JsonMap<String, JsonValue>,
}

impl ResourceUnit {
    pub fn available(&self, inflight_total: &HashMap<String, f64>) -> HashMap<String, f64> {
        let mut out = HashMap::new();
        let limits = if self.allocatable.is_empty() {
            &self.capacity
        } else {
            &self.allocatable
        };
        for (k, cap) in limits {
            let u = self.used.get(k).copied().unwrap_or(0.0);
            let infl = inflight_total.get(k).copied().unwrap_or(0.0);
            out.insert(k.clone(), (cap - u - infl).max(0.0));
        }
        out
    }

    pub fn has_room(
        &self,
        need: &HashMap<String, f64>,
        inflight_total: &HashMap<String, f64>,
    ) -> bool {
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
        self.inflight
            .retain(|_, e| now.duration_since(e.created) < e.ttl);
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

    pub fn upsert_node_resource_unit(&self, node_id: &str, unit: ResourceUnit) {
        self.nodes
            .entry(node_id.to_string())
            .or_insert_with(|| {
                Mutex::new(NodeResourceState {
                    unit: ResourceUnit::default(),
                    inflight: HashMap::new(),
                })
            })
            .lock()
            .unit = unit;
    }

    pub fn upsert_node_resources(
        &self,
        node_id: &str,
        capacity: HashMap<String, f64>,
        used: HashMap<String, f64>,
    ) {
        self.upsert_node_resource_unit(
            node_id,
            ResourceUnit {
                capacity,
                used,
                ..ResourceUnit::default()
            },
        );
    }

    pub fn apply_resource_json(&self, node_id: &str, resource_json: &str) {
        let unit = parse_resource_json(resource_json);
        if unit.capacity.is_empty()
            && unit.used.is_empty()
            && unit.allocatable.is_empty()
            && unit.labels.is_empty()
            && unit.vectors.is_empty()
            && unit.instances.is_empty()
        {
            return;
        }
        self.upsert_node_resource_unit(node_id, unit);
    }

    pub fn apply_resource_unit_proto(&self, node_id: &str, resource_unit: &ProtoResourceUnit) {
        self.upsert_node_resource_unit(node_id, resource_unit_from_proto(resource_unit));
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
                "allocatable": g.unit.allocatable,
                "labels": g.unit.labels,
                "vectors": g.unit.vectors,
                "instances": g.unit.instances,
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

fn parse_resource_json(s: &str) -> ResourceUnit {
    let v: serde_json::Value = match serde_json::from_str(s) {
        Ok(x) => x,
        Err(_) => return ResourceUnit::default(),
    };
    let obj = match v.as_object() {
        Some(o) => o,
        None => return ResourceUnit::default(),
    };
    let mut unit = ResourceUnit {
        capacity: json_f64_map(obj.get("capacity").unwrap_or(&JsonValue::Null)),
        used: json_f64_map(obj.get("used").unwrap_or(&JsonValue::Null)),
        allocatable: json_f64_map(obj.get("allocatable").unwrap_or(&JsonValue::Null)),
        labels: json_string_map(obj.get("labels").unwrap_or(&JsonValue::Null)),
        vectors: json_object_map(obj.get("vectors").unwrap_or(&JsonValue::Null)),
        instances: json_object_map(obj.get("instances").unwrap_or(&JsonValue::Null)),
    };
    if unit.capacity.is_empty() && unit.used.is_empty() {
        if let Some(resources) = obj.get("resources").and_then(|v| v.as_object()) {
            unit.capacity = resources
                .iter()
                .map(|(name, value)| (name.clone(), scalar_from_resource_entry(value)))
                .collect();
        } else {
            unit.capacity = json_f64_map(&JsonValue::Object(obj.clone()));
        }
    }
    unit
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

fn json_string_map(v: &serde_json::Value) -> HashMap<String, String> {
    let mut m = HashMap::new();
    let Some(o) = v.as_object() else {
        return m;
    };
    for (k, val) in o {
        if let Some(s) = val.as_str() {
            m.insert(k.clone(), s.to_string());
        }
    }
    m
}

fn json_object_map(v: &serde_json::Value) -> JsonMap<String, JsonValue> {
    v.as_object().cloned().unwrap_or_default()
}

fn scalar_from_resource_entry(v: &JsonValue) -> f64 {
    let Some(obj) = v.as_object() else {
        return v.as_f64().unwrap_or_default();
    };
    let Some(scalar) = obj.get("scalar").and_then(|scalar| scalar.as_object()) else {
        return 0.0;
    };
    scalar
        .get("value")
        .and_then(|value| value.as_f64())
        .or_else(|| {
            scalar
                .get("value")
                .and_then(|value| value.as_i64())
                .map(|n| n as f64)
        })
        .unwrap_or_default()
}

pub fn merge_topology_resource(node_id: &str, resource_json: &str, view: &ResourceView) {
    if resource_json.is_empty() {
        warn!(%node_id, "empty resource_json for node");
        return;
    }
    view.apply_resource_json(node_id, resource_json);
}

pub fn merge_topology_resource_unit_b64(
    node_id: &str,
    resource_unit_b64: &str,
    view: &ResourceView,
) {
    let Some(bytes) = base64::engine::general_purpose::STANDARD
        .decode(resource_unit_b64)
        .ok()
    else {
        return;
    };
    let Ok(resource_unit) = ProtoResourceUnit::decode(bytes.as_slice()) else {
        return;
    };
    view.apply_resource_unit_proto(node_id, &resource_unit);
}

fn resource_unit_from_proto(resource_unit: &ProtoResourceUnit) -> ResourceUnit {
    ResourceUnit {
        capacity: proto_resources_to_scalar_map(resource_unit.capacity.as_ref()),
        used: proto_resources_to_scalar_map(resource_unit.actual_use.as_ref()),
        allocatable: proto_resources_to_scalar_map(resource_unit.allocatable.as_ref()),
        labels: proto_labels_to_string_map(&resource_unit.node_labels),
        vectors: proto_resources_to_vector_map(
            resource_unit.allocatable.as_ref(),
            resource_unit.capacity.as_ref(),
            resource_unit.actual_use.as_ref(),
        ),
        instances: proto_instances_to_json_map(&resource_unit.instances),
    }
}

fn proto_resources_to_scalar_map(
    resources: Option<&yr_proto::resources::Resources>,
) -> HashMap<String, f64> {
    let mut out = HashMap::new();
    let Some(resources) = resources else {
        return out;
    };
    for (name, resource) in &resources.resources {
        if let Some(scalar) = resource.scalar.as_ref() {
            out.insert(name.clone(), scalar.value);
        }
    }
    out
}

fn proto_labels_to_string_map(
    labels: &std::collections::HashMap<String, yr_proto::resources::value::Counter>,
) -> HashMap<String, String> {
    labels
        .iter()
        .filter_map(|(key, counter)| {
            let mut values = counter.items.keys().cloned().collect::<Vec<_>>();
            values.sort();
            values.into_iter().next().map(|value| (key.clone(), value))
        })
        .collect()
}

fn proto_resources_to_vector_map(
    primary: Option<&yr_proto::resources::Resources>,
    secondary: Option<&yr_proto::resources::Resources>,
    tertiary: Option<&yr_proto::resources::Resources>,
) -> JsonMap<String, JsonValue> {
    let mut out = JsonMap::new();
    for resources in [primary, secondary, tertiary].into_iter().flatten() {
        for (name, resource) in &resources.resources {
            if let Some(vectors) = resource.vectors.as_ref() {
                out.entry(name.clone())
                    .or_insert_with(|| proto_resource_to_json(resource, vectors));
            }
        }
    }
    out
}

fn proto_resource_to_json(
    resource: &yr_proto::resources::Resource,
    vectors: &yr_proto::resources::value::Vectors,
) -> JsonValue {
    JsonValue::Object(JsonMap::from_iter([
        (
            "values".to_string(),
            JsonValue::Object(JsonMap::from_iter(vectors.values.iter().map(
                |(category, value)| {
                    (
                        category.clone(),
                        JsonValue::Object(JsonMap::from_iter([(
                            "vectors".to_string(),
                            JsonValue::Object(JsonMap::from_iter(value.vectors.iter().map(
                                |(uuid, vector)| {
                                    (
                                        uuid.clone(),
                                        JsonValue::Object(JsonMap::from_iter([(
                                            "values".to_string(),
                                            JsonValue::Array(
                                                vector
                                                    .values
                                                    .iter()
                                                    .map(|value| JsonValue::from(*value))
                                                    .collect(),
                                            ),
                                        )])),
                                    )
                                },
                            ))),
                        )])),
                    )
                },
            ))),
        ),
        (
            "heterogeneousInfo".to_string(),
            JsonValue::Object(JsonMap::from_iter(
                resource
                    .heterogeneous_info
                    .iter()
                    .map(|(key, value)| (key.clone(), JsonValue::String(value.clone()))),
            )),
        ),
    ]))
}

fn proto_instances_to_json_map(
    instances: &std::collections::HashMap<String, yr_proto::resources::InstanceInfo>,
) -> JsonMap<String, JsonValue> {
    instances
        .iter()
        .map(|(instance_id, instance)| {
            let resources = instance
                .actual_use
                .as_ref()
                .map(|actual_use| {
                    JsonValue::Object(JsonMap::from_iter(actual_use.resources.iter().map(
                        |(name, resource)| (name.clone(), proto_resource_entry_to_json(resource)),
                    )))
                })
                .unwrap_or_else(|| JsonValue::Object(JsonMap::new()));
            (
                instance_id.clone(),
                JsonValue::Object(JsonMap::from_iter([
                    (
                        "instanceid".to_string(),
                        JsonValue::String(instance.instance_id.clone()),
                    ),
                    (
                        "actualUse".to_string(),
                        JsonValue::Object(JsonMap::from_iter([(
                            "resources".to_string(),
                            resources,
                        )])),
                    ),
                ])),
            )
        })
        .collect()
}

fn proto_resource_entry_to_json(resource: &yr_proto::resources::Resource) -> JsonValue {
    let mut out =
        JsonMap::from_iter([("name".to_string(), JsonValue::String(resource.name.clone()))]);
    if let Some(scalar) = resource.scalar.as_ref() {
        out.insert(
            "scalar".to_string(),
            JsonValue::Object(JsonMap::from_iter([
                ("value".to_string(), JsonValue::from(scalar.value)),
                ("limit".to_string(), JsonValue::from(scalar.limit)),
            ])),
        );
    }
    if let Some(vectors) = resource.vectors.as_ref() {
        if let JsonValue::Object(extra) = proto_resource_to_json(resource, vectors) {
            out.extend(extra);
        }
    }
    JsonValue::Object(out)
}
