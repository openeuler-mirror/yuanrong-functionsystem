//! Instance metadata and transitions using the canonical C++ state graph from `yr_common::types`.

use serde::{Deserialize, Deserializer, Serialize, Serializer};
use std::collections::HashMap;
use std::str::FromStr;
use std::time::{SystemTime, UNIX_EPOCH};
use yr_common::types::{need_persistence_state, need_update_route_state, transition_allowed};

pub use yr_common::types::InstanceState;

fn ser_instance_state<S>(s: &InstanceState, ser: S) -> Result<S::Ok, S::Error>
where
    S: Serializer,
{
    ser.serialize_str(&s.to_string())
}

fn de_instance_state<'de, D>(de: D) -> Result<InstanceState, D::Error>
where
    D: Deserializer<'de>,
{
    let raw = String::deserialize(de)?;
    InstanceState::from_str(raw.trim())
        .map_err(|_| serde::de::Error::custom(format!("unknown InstanceState {:?}", raw.trim())))
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct InstanceMetadata {
    pub id: String,
    pub function_name: String,
    pub tenant: String,
    pub node_id: String,
    pub runtime_id: String,
    pub runtime_port: i32,
    #[serde(
        serialize_with = "ser_instance_state",
        deserialize_with = "de_instance_state"
    )]
    pub state: InstanceState,
    pub created_at_ms: i64,
    pub updated_at_ms: i64,
    #[serde(default)]
    pub group_id: Option<String>,
    #[serde(default)]
    pub trace_id: String,
    #[serde(default)]
    pub resources: HashMap<String, f64>,
    /// Last known etcd KV `version` after persist (optional CAS / reconciliation).
    #[serde(default)]
    pub etcd_kv_version: Option<i64>,
    /// Last known etcd `mod_revision` for the instance record.
    #[serde(default)]
    pub etcd_mod_revision: Option<i64>,
}

impl InstanceMetadata {
    pub fn now_ms() -> i64 {
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_millis() as i64)
            .unwrap_or(0)
    }

    /// Apply `next` using [`types::instance_state_transition_map`], with a small shim for
    /// runtime teardown paths that land in `EXITED` (not expressed as edges in the sparse map).
    pub fn transition(&mut self, next: InstanceState) -> Result<(), &'static str> {
        if self.state == next {
            return Ok(());
        }
        if transition_allowed(self.state, next) {
            self.state = next;
            self.updated_at_ms = Self::now_ms();
            return Ok(());
        }
        // Shim: normal proxy/runtime shutdown often records EXITED directly.
        if matches!(
            (self.state, next),
            (InstanceState::Running, InstanceState::Exited)
                | (InstanceState::Exiting, InstanceState::Exited)
        ) {
            self.state = next;
            self.updated_at_ms = Self::now_ms();
            return Ok(());
        }
        Err("invalid state transition")
    }

    /// States that should be written to etcd when MetaStore is enabled (see `yr_common::types` helpers).
    pub fn should_persist_state(&self) -> bool {
        need_persistence_state(self.state)
    }

    /// Whether route rows should be updated for this state (depends on MetaStore mode).
    pub fn should_update_route(&self, meta_store_enabled: bool) -> bool {
        need_update_route_state(self.state, meta_store_enabled)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn uses_global_transition_table() {
        assert!(transition_allowed(
            InstanceState::New,
            InstanceState::Scheduling
        ));
        assert!(!transition_allowed(
            InstanceState::New,
            InstanceState::Running
        ));
    }
}
