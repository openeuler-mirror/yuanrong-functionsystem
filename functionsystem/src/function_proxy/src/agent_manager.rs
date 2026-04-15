use dashmap::DashMap;
use serde::{Deserialize, Serialize};
use std::sync::Arc;
use tracing::warn;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AgentRecord {
    pub agent_id: String,
    pub pid: Option<u32>,
    pub last_heartbeat_ms: i64,
    pub status: String,
}

/// Tracks function agents co-located on this worker node.
#[derive(Debug)]
pub struct AgentManager {
    agents: DashMap<String, AgentRecord>,
}

impl AgentManager {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            agents: DashMap::new(),
        })
    }

    pub fn upsert(&self, rec: AgentRecord) {
        self.agents.insert(rec.agent_id.clone(), rec);
    }

    pub fn remove(&self, agent_id: &str) {
        self.agents.remove(agent_id);
    }

    pub fn list_json(&self) -> String {
        let v: Vec<_> = self
            .agents
            .iter()
            .map(|e| e.value().clone())
            .collect();
        serde_json::to_string(&v).unwrap_or_else(|_| "[]".into())
    }

    pub fn handle_deploy_command(&self, agent_id: &str, payload_hint: &str) -> Result<(), String> {
        warn!(%agent_id, %payload_hint, "agent deploy command not implemented");
        Err("agent deploy is not implemented".into())
    }

    pub fn handle_kill_command(&self, agent_id: &str, reason: &str) -> Result<(), String> {
        warn!(%agent_id, %reason, "agent kill command not implemented");
        Err("agent kill is not implemented".into())
    }
}

impl Default for AgentManager {
    fn default() -> Self {
        Self {
            agents: DashMap::new(),
        }
    }
}
