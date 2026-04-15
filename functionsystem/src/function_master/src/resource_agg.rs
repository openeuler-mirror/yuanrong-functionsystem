//! Aggregates per-proxy `resource_json` for HTTP `/resources` enrichment and scheduler hints.

use serde_json::{json, Value};

use yr_common::schedule::types::ResourceViewInfo;
use yr_common::schedule_plugin::resource::{CPU_RESOURCE_NAME, MEMORY_RESOURCE_NAME};

use crate::topology::TopologyManager;

pub struct ResourceAggregator;

impl ResourceAggregator {
    /// Merge every local proxy record into one JSON blob (C++ `ResourceAggregator` style summary).
    pub fn merged_json(topology: &TopologyManager) -> Value {
        let rows = topology.locals_snapshot();
        let mut nodes = Vec::new();
        for r in &rows {
            let parsed: Value = serde_json::from_str(&r.resource_json).unwrap_or_else(|_| json!({}));
            nodes.push(json!({
                "node_id": r.node_id,
                "address": r.address,
                "domain_id": r.domain_id,
                "domain_address": r.domain_address,
                "resources": parsed,
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
            Self::accumulate_scalar_resources(&r.resource_json, &mut total_cpu, &mut total_mem);
        }
        let n = rows.len();
        ResourceViewInfo {
            label: format!(
                "proxies={n};sum_cpu={total_cpu:.4};sum_mem={total_mem:.4}"
            ),
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
        *cpu += scalar_from_entry(map.get(CPU_RESOURCE_NAME));
        *mem += scalar_from_entry(map.get(MEMORY_RESOURCE_NAME));
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
        .or_else(|| scalar.get("value").and_then(|x| x.as_i64()).map(|i| i as f64))
        .unwrap_or(0.0)
}
