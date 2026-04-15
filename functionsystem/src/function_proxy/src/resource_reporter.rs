//! Aggregate node + per-instance resource view for master / global scheduler updates.

use serde_json::json;
use std::collections::HashMap;
use std::sync::Arc;

use crate::instance_ctrl::InstanceController;
use crate::resource_view::ResourceView;
use crate::state_machine::InstanceMetadata;
use yr_common::types::InstanceState;

/// Build JSON for [`crate::registration::push_resources`]: capacity snapshot plus instance breakdown.
pub fn build_resource_report_json(
    node_id: &str,
    resource_view: &ResourceView,
    instance_ctrl: &InstanceController,
) -> String {
    let base: serde_json::Value =
        serde_json::from_str(&resource_view.snapshot_json()).unwrap_or_else(|_| json!({}));

    let mut cpu_by_state: HashMap<String, f64> = HashMap::new();
    #[derive(Default)]
    struct FnAgg {
        cpu: f64,
        memory: f64,
        npu: f64,
        count: u64,
    }
    let mut by_function: HashMap<String, FnAgg> = HashMap::new();
    let mut inst_cpu = 0.0f64;
    let mut inst_mem = 0.0f64;
    let mut inst_npu = 0.0f64;

    for e in instance_ctrl.instances().iter() {
        let m: InstanceMetadata = e.value().clone();
        if !matches!(
            m.state,
            InstanceState::Running | InstanceState::SubHealth | InstanceState::Suspend
        ) {
            continue;
        }
        let cpu = *m.resources.get("cpu").unwrap_or(&0.0);
        let memory = *m.resources.get("memory").unwrap_or(&0.0);
        let npu = m
            .resources
            .get("npu")
            .or_else(|| m.resources.get("ascend"))
            .copied()
            .unwrap_or(0.0);
        inst_cpu += cpu;
        inst_mem += memory;
        inst_npu += npu;

        let st = m.state.to_string();
        *cpu_by_state.entry(st).or_insert(0.0) += cpu;

        let fe = by_function.entry(m.function_name.clone()).or_default();
        fe.cpu += cpu;
        fe.memory += memory;
        fe.npu += npu;
        fe.count += 1;
    }

    let by_function_json: serde_json::Map<String, serde_json::Value> = by_function
        .into_iter()
        .map(|(k, v)| {
            (
                k,
                json!({
                    "cpu": v.cpu,
                    "memory": v.memory,
                    "npu": v.npu,
                    "count": v.count,
                }),
            )
        })
        .collect();

    let merged = json!({
        "node_id": node_id,
        "resource_view": base,
        "instances": {
            "count": instance_ctrl.instances().len(),
            "aggregate_active_resources": {
                "cpu": inst_cpu,
                "memory": inst_mem,
                "npu": inst_npu,
            },
            "cpu_by_state": cpu_by_state,
            "by_function": by_function_json,
        }
    });
    merged.to_string()
}

/// Convenience when only `Arc` handles are available (heartbeat loop).
pub fn build_resource_report_json_arc(
    node_id: &str,
    resource_view: &Arc<ResourceView>,
    instance_ctrl: &Arc<InstanceController>,
) -> String {
    build_resource_report_json(node_id, resource_view.as_ref(), instance_ctrl.as_ref())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::Config;
    use crate::resource_view::ResourceVector;
    use clap::Parser;
    use std::collections::HashMap;

    #[test]
    fn report_includes_instance_aggregates() {
        let cfg = Arc::new(
            Config::try_parse_from(["yr-proxy", "--node-id", "nid", "--grpc-listen-port", "1"]).unwrap(),
        );
        let rv = ResourceView::new(ResourceVector {
            cpu: 8.0,
            memory: 64.0,
            npu: 0.0,
        });
        let ctrl = InstanceController::new(cfg, rv.clone(), None, None);
        let now = InstanceMetadata::now_ms();
        ctrl.insert_metadata(InstanceMetadata {
            id: "i1".into(),
            function_name: "hello".into(),
            tenant: "t".into(),
            node_id: "nid".into(),
            runtime_id: "r".into(),
            runtime_port: 1,
            state: InstanceState::Running,
            created_at_ms: now,
            updated_at_ms: now,
            group_id: None,
            trace_id: String::new(),
            resources: HashMap::from([("cpu".into(), 2.0), ("memory".into(), 1024.0)]),
            etcd_kv_version: None,
            etcd_mod_revision: None,
        });
        let j = build_resource_report_json("nid", rv.as_ref(), &ctrl);
        let v: serde_json::Value = serde_json::from_str(&j).unwrap();
        assert_eq!(v["node_id"], "nid");
        assert_eq!(
            v["instances"]["aggregate_active_resources"]["cpu"].as_f64(),
            Some(2.0)
        );
        assert_eq!(v["instances"]["count"].as_u64(), Some(1));
    }
}
