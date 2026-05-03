//! In-memory instance snapshots (C++ `SnapshotManager` analogue).

use std::collections::HashMap;
use std::str::FromStr;
use std::sync::Arc;

use parking_lot::RwLock;
use prost::Message;
use serde::Serialize;
use serde_json::Value;
use yr_common::types::InstanceState;
use yr_proto::messages::SnapshotMetadata;
use yr_proto::resources::{
    value::{Scalar, Type as ValueType},
    InstanceInfo, InstanceStatus, Resource, Resources, SnapshotInfo,
};

/// Metadata captured when an instance reaches a snapshot-worthy terminal state.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct InstanceSnapshot {
    #[serde(rename = "instanceID")]
    pub instance_id: String,
    #[serde(rename = "functionName")]
    pub function_name: String,
    #[serde(rename = "tenantID")]
    pub tenant_id: String,
    pub state: String,
    #[serde(rename = "createTime")]
    pub create_time: u64,
    #[serde(rename = "exitTime")]
    pub exit_time: u64,
    #[serde(rename = "exitReason")]
    pub exit_reason: String,
    #[serde(rename = "resourceCpu")]
    pub resource_cpu: u64,
    #[serde(rename = "resourceMemory")]
    pub resource_memory: u64,
    #[serde(rename = "nodeID")]
    pub node_id: String,
    #[serde(rename = "proxyID")]
    pub proxy_id: String,
    /// Same as `instance_id`; kept for clients that expect a snapshot id field.
    #[serde(rename = "snapshotID")]
    pub snapshot_id: String,
}

/// Thread-safe in-memory snapshot store keyed by `instance_id`.
#[derive(Debug, Default)]
pub struct SnapshotManager {
    inner: RwLock<HashMap<String, InstanceSnapshot>>,
}

impl SnapshotManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            inner: RwLock::new(HashMap::new()),
        })
    }

    /// Insert or replace the snapshot for `snapshot.instance_id`.
    pub fn create_snapshot(&self, snapshot: InstanceSnapshot) {
        let mut g = self.inner.write();
        g.insert(snapshot.instance_id.clone(), snapshot);
    }

    pub fn get(&self, instance_id: &str) -> Option<InstanceSnapshot> {
        self.inner.read().get(instance_id).cloned()
    }

    pub fn remove(&self, instance_id: &str) -> Option<InstanceSnapshot> {
        self.inner.write().remove(instance_id)
    }

    pub fn list_by_function_and_tenant(
        &self,
        function_name: &str,
        tenant_id: Option<&str>,
    ) -> Vec<InstanceSnapshot> {
        let g = self.inner.read();
        let mut out: Vec<InstanceSnapshot> = g
            .values()
            .filter(|s| s.function_name == function_name)
            .filter(|s| {
                tenant_id.map_or(true, |t| t.is_empty() || s.tenant_id == t)
            })
            .cloned()
            .collect();
        out.sort_by(|a, b| b.exit_time.cmp(&a.exit_time));
        out
    }
}

pub fn snapshot_to_proto(snapshot: &InstanceSnapshot) -> SnapshotMetadata {
    let state_code = InstanceState::from_str(snapshot.state.as_str())
        .map(|st| st.as_i32())
        .unwrap_or_default();
    let resources = snapshot_resources(snapshot);

    SnapshotMetadata {
        instance_info: Some(InstanceInfo {
            instance_id: snapshot.instance_id.clone(),
            function_proxy_id: snapshot.proxy_id.clone(),
            function: snapshot.function_name.clone(),
            tenant_id: snapshot.tenant_id.clone(),
            resources,
            instance_status: Some(InstanceStatus {
                code: state_code,
                msg: snapshot.exit_reason.clone(),
                ..Default::default()
            }),
            ..Default::default()
        }),
        snapshot_info: Some(SnapshotInfo {
            checkpoint_id: snapshot.snapshot_id.clone(),
            create_time: snapshot.create_time.to_string(),
            ..Default::default()
        }),
    }
}

fn snapshot_resources(snapshot: &InstanceSnapshot) -> Option<Resources> {
    let mut resources = HashMap::new();
    if snapshot.resource_cpu > 0 {
        resources.insert(
            "cpu".into(),
            Resource {
                name: "cpu".into(),
                r#type: ValueType::Scalar as i32,
                scalar: Some(Scalar {
                    value: snapshot.resource_cpu as f64,
                    limit: 0.0,
                }),
                ..Default::default()
            },
        );
    }
    if snapshot.resource_memory > 0 {
        resources.insert(
            "memory".into(),
            Resource {
                name: "memory".into(),
                r#type: ValueType::Scalar as i32,
                scalar: Some(Scalar {
                    value: snapshot.resource_memory as f64,
                    limit: 0.0,
                }),
                ..Default::default()
            },
        );
    }
    if resources.is_empty() {
        None
    } else {
        Some(Resources { resources })
    }
}

pub fn snapshots_to_proto_bytes(snapshots: &[InstanceSnapshot]) -> Vec<u8> {
    let mut out = Vec::new();
    for snapshot in snapshots {
        snapshot_to_proto(snapshot)
            .encode(&mut out)
            .expect("Vec<u8> should not fail protobuf encoding");
    }
    out
}

/// States that trigger a master-side snapshot (aligns with C++ snap-on-terminal behaviour).
pub fn should_record_snapshot_state(st: InstanceState) -> bool {
    matches!(
        st,
        InstanceState::Failed | InstanceState::Exited | InstanceState::Evicted
    )
}

fn json_pick_str(obj: &serde_json::Map<String, Value>, keys: &[&str]) -> String {
    for k in keys {
        if let Some(Value::String(s)) = obj.get(*k) {
            return s.clone();
        }
    }
    String::new()
}

fn json_pick_i64(obj: &serde_json::Map<String, Value>, keys: &[&str]) -> Option<i64> {
    for k in keys {
        if let Some(v) = obj.get(*k) {
            if let Some(i) = v.as_i64() {
                return Some(i);
            }
            if let Some(f) = v.as_f64() {
                return Some(f as i64);
            }
        }
    }
    None
}

fn json_pick_u64(obj: &serde_json::Map<String, Value>, keys: &[&str]) -> u64 {
    json_pick_i64(obj, keys).map(|v| v.max(0) as u64).unwrap_or(0)
}

fn instance_state_from_json_value(v: &Value) -> Option<InstanceState> {
    if let Some(s) = v.as_str() {
        return InstanceState::from_str(s.trim()).ok();
    }
    let code = v.as_i64().or_else(|| v.as_f64().map(|f| f as i64))?;
    match code {
        0 => Some(InstanceState::New),
        1 => Some(InstanceState::Scheduling),
        2 => Some(InstanceState::Creating),
        3 => Some(InstanceState::Running),
        4 => Some(InstanceState::Failed),
        5 => Some(InstanceState::Exiting),
        6 => Some(InstanceState::Fatal),
        7 => Some(InstanceState::ScheduleFailed),
        8 => Some(InstanceState::Exited),
        9 => Some(InstanceState::Evicting),
        10 => Some(InstanceState::Evicted),
        11 => Some(InstanceState::SubHealth),
        12 => Some(InstanceState::Suspend),
        _ => None,
    }
}

fn resource_scalar_u64(resources: &serde_json::Map<String, Value>, name: &str) -> u64 {
    let Some(entry) = resources.get(name) else {
        return 0;
    };
    let Some(obj) = entry.as_object() else {
        return 0;
    };
    let Some(scalar) = obj.get("scalar").and_then(|s| s.as_object()) else {
        return 0;
    };
    let Some(val) = scalar.get("value") else {
        return 0;
    };
    if let Some(i) = val.as_i64() {
        return i.max(0) as u64;
    }
    val.as_f64().map(|f| f.max(0.0) as u64).unwrap_or(0)
}

fn exit_reason_from_value(v: &Value) -> String {
    let Some(status) = v
        .get("instanceStatus")
        .or_else(|| v.get("instance_status"))
    else {
        return String::new();
    };
    let Some(o) = status.as_object() else {
        return String::new();
    };
    json_pick_str(o, &["msg", "message"])
}

/// Build a snapshot row from a metastore / instance-manager style JSON value.
pub fn snapshot_from_instance_json(v: &Value) -> Option<InstanceSnapshot> {
    let o = v.as_object()?;
    let instance_id = json_pick_str(o, &["id", "instanceID", "instance_id"]);
    if instance_id.is_empty() {
        return None;
    }
    let state_val = o.get("state")?;
    let st = instance_state_from_json_value(state_val)?;
    if !should_record_snapshot_state(st) {
        return None;
    }

    let function_name = json_pick_str(o, &["function_name", "function"]);
    let tenant_id = json_pick_str(o, &["tenant", "tenantID", "tenant_id"]);
    let node_id = json_pick_str(o, &["node_id", "nodeID"]);
    let proxy_id = json_pick_str(o, &["function_proxy_id", "functionProxyID"]);

    let create_time = json_pick_u64(o, &["created_at_ms", "createTime", "createdAtMs"]);
    let exit_time = json_pick_u64(o, &["updated_at_ms", "exitTime", "updatedAtMs"]);

    let (resource_cpu, resource_memory) = if let Some(Value::Object(res_map)) = o.get("resources") {
        let cpu = resource_scalar_u64(res_map, "cpu")
            .max(resource_scalar_u64(res_map, "CPU"));
        let mem = resource_scalar_u64(res_map, "memory")
            .max(resource_scalar_u64(res_map, "MEMORY"));
        (cpu, mem)
    } else {
        (0, 0)
    };

    let exit_reason = exit_reason_from_value(v);

    Some(InstanceSnapshot {
        snapshot_id: instance_id.clone(),
        instance_id,
        function_name,
        tenant_id,
        state: st.to_string(),
        create_time,
        exit_time,
        exit_reason,
        resource_cpu,
        resource_memory,
        node_id,
        proxy_id,
    })
}

/// If `new_value` enters a snapshot-worthy state from a different prior state, record a snapshot.
pub fn maybe_record_snapshot_transition(
    snapshots: &SnapshotManager,
    old: Option<&Value>,
    new_value: &Value,
) {
    let Some(snap) = snapshot_from_instance_json(new_value) else {
        return;
    };
    let same_as_before = old.and_then(|pv| snapshot_from_instance_json(pv)).as_ref() == Some(&snap);
    if same_as_before {
        return;
    }
    snapshots.create_snapshot(snap);
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_exited_json(id: &str, function: &str, tenant: &str) -> Value {
        serde_json::json!({
            "id": id,
            "function_name": function,
            "tenant": tenant,
            "state": "EXITED",
            "created_at_ms": 1000,
            "updated_at_ms": 2000,
            "node_id": "node-1",
            "function_proxy_id": "proxy-1",
            "resources": {
                "cpu": { "scalar": { "value": 2.0 } },
                "memory": { "scalar": { "value": 512.0 } }
            },
            "instanceStatus": { "msg": "normal exit" }
        })
    }

    #[test]
    fn snapshot_manager_crud() {
        let m = SnapshotManager::new();
        let s = InstanceSnapshot {
            instance_id: "i1".into(),
            snapshot_id: "i1".into(),
            function_name: "f".into(),
            tenant_id: "t".into(),
            state: "EXITED".into(),
            create_time: 1,
            exit_time: 2,
            exit_reason: String::new(),
            resource_cpu: 0,
            resource_memory: 0,
            node_id: String::new(),
            proxy_id: String::new(),
        };
        m.create_snapshot(s.clone());
        assert_eq!(m.get("i1"), Some(s.clone()));

        let listed = m.list_by_function_and_tenant("f", Some("t"));
        assert_eq!(listed.len(), 1);

        assert_eq!(m.remove("i1"), Some(s));
        assert!(m.get("i1").is_none());
    }

    #[test]
    fn transition_creates_snapshot_once_per_distinct_row() {
        let m = SnapshotManager::new();
        let v1 = sample_exited_json("a", "fn", "ten");
        maybe_record_snapshot_transition(&m, None, &v1);
        assert!(m.get("a").is_some());

        maybe_record_snapshot_transition(&m, Some(&v1), &v1);
        assert_eq!(m.list_by_function_and_tenant("fn", None).len(), 1);

        let mut v2 = v1.clone();
        v2["updated_at_ms"] = serde_json::json!(3000);
        maybe_record_snapshot_transition(&m, Some(&v1), &v2);
        assert_eq!(m.get("a").unwrap().exit_time, 3000);
    }

    #[test]
    fn running_does_not_snapshot() {
        let m = SnapshotManager::new();
        let v = serde_json::json!({
            "id": "r",
            "function_name": "f",
            "tenant": "t",
            "state": "RUNNING",
            "created_at_ms": 1,
            "updated_at_ms": 2,
        });
        maybe_record_snapshot_transition(&m, None, &v);
        assert!(m.get("r").is_none());
    }
}
