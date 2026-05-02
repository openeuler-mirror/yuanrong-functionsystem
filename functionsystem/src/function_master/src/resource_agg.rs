//! Aggregates per-proxy `resource_json` for HTTP `/resources` enrichment and scheduler hints.

use base64::Engine;
use prost::Message;
use serde_json::{json, Value};

use yr_common::schedule::types::ResourceViewInfo;
use yr_common::schedule_plugin::resource::{CPU_RESOURCE_NAME, MEMORY_RESOURCE_NAME};
use yr_proto::resources::ResourceUnit as ProtoResourceUnit;

use crate::topology::TopologyManager;

pub struct ResourceAggregator;

impl ResourceAggregator {
    /// Merge every local proxy record into one JSON blob (C++ `ResourceAggregator` style summary).
    pub fn merged_json(topology: &TopologyManager) -> Value {
        let rows = topology.locals_snapshot();
        let mut nodes = Vec::new();
        for r in &rows {
            let parsed: Value =
                serde_json::from_str(&r.resource_json).unwrap_or_else(|_| json!({}));
            nodes.push(json!({
                "node_id": r.node_id,
                "address": r.address,
                "domain_id": r.domain_id,
                "domain_address": r.domain_address,
                "resources": parsed,
                "resource_unit_b64": r.resource_unit_b64,
                "last_seen_ms": r.last_seen_ms,
            }));
        }
        json!({
            "proxy_node_count": rows.len(),
            "proxies": nodes,
        })
    }

    /// Sum allocatable CPU / memory hints from each proxy's `resource_json` for `PriorityScheduler::handle_resource_info_update`.
    pub fn resource_view_for_scheduler(topology: &TopologyManager) -> ResourceViewInfo {
        let rows = topology.locals_snapshot();
        let mut total_cpu = 0.0_f64;
        let mut total_mem = 0.0_f64;
        for r in &rows {
            if !Self::accumulate_proto_allocatable(
                &r.resource_unit_b64,
                &mut total_cpu,
                &mut total_mem,
            ) {
                Self::accumulate_scalar_resources(&r.resource_json, &mut total_cpu, &mut total_mem);
            }
        }
        let n = rows.len();
        ResourceViewInfo {
            label: format!("proxies={n};sum_cpu={total_cpu:.4};sum_mem={total_mem:.4}"),
        }
    }

    fn accumulate_scalar_resources(json_str: &str, cpu: &mut f64, mem: &mut f64) {
        let Ok(v) = serde_json::from_str::<Value>(json_str) else {
            return;
        };
        let map = if let Some(m) = v.get("resources").and_then(|x| x.as_object()) {
            m
        } else if let Some(m) = v.as_object() {
            m
        } else {
            return;
        };
        *cpu += scalar_from_entry(map.get(CPU_RESOURCE_NAME).or_else(|| map.get("cpu")));
        *mem += scalar_from_entry(map.get(MEMORY_RESOURCE_NAME).or_else(|| map.get("memory")));
    }

    fn accumulate_proto_allocatable(resource_unit_b64: &str, cpu: &mut f64, mem: &mut f64) -> bool {
        let Some(unit) = decode_resource_unit(resource_unit_b64) else {
            return false;
        };
        let Some(allocatable) = unit.allocatable.as_ref() else {
            return false;
        };
        *cpu += scalar_from_proto_resource(
            allocatable
                .resources
                .get(CPU_RESOURCE_NAME)
                .or_else(|| allocatable.resources.get("cpu")),
        );
        *mem += scalar_from_proto_resource(
            allocatable
                .resources
                .get(MEMORY_RESOURCE_NAME)
                .or_else(|| allocatable.resources.get("memory")),
        );
        true
    }
}

fn scalar_from_entry(v: Option<&Value>) -> f64 {
    let Some(v) = v else {
        return 0.0;
    };
    let obj = match v.as_object() {
        Some(o) => o,
        None => return 0.0,
    };
    let scalar = match obj.get("scalar").and_then(|s| s.as_object()) {
        Some(s) => s,
        None => return 0.0,
    };
    scalar
        .get("value")
        .and_then(|x| x.as_f64())
        .or_else(|| {
            scalar
                .get("value")
                .and_then(|x| x.as_i64())
                .map(|i| i as f64)
        })
        .unwrap_or(0.0)
}

fn decode_resource_unit(resource_unit_b64: &str) -> Option<ProtoResourceUnit> {
    if resource_unit_b64.trim().is_empty() {
        return None;
    }
    let bytes = base64::engine::general_purpose::STANDARD
        .decode(resource_unit_b64)
        .ok()?;
    ProtoResourceUnit::decode(bytes.as_slice()).ok()
}

fn scalar_from_proto_resource(v: Option<&yr_proto::resources::Resource>) -> f64 {
    v.and_then(|resource| resource.scalar.as_ref().map(|scalar| scalar.value))
        .unwrap_or(0.0)
}
