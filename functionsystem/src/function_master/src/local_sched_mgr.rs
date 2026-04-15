//! Local scheduler registry and eviction (`local_sched_mgr_actor.cpp` analogue).

use std::time::Duration;

use dashmap::DashMap;
use tracing::debug;

/// Registered local scheduler (`node_id` → gRPC / LiteBus address).
pub struct LocalSchedMgr {
    locals: DashMap<String, String>,
    evict_timeout: Duration,
    evict_retries: u32,
}

impl LocalSchedMgr {
    pub fn new(evict_timeout: Duration, evict_retries: u32) -> Self {
        Self {
            locals: DashMap::new(),
            evict_timeout,
            evict_retries: evict_retries.max(1),
        }
    }

    pub fn register(&self, node_id: impl Into<String>, address: impl Into<String>) {
        let node_id = node_id.into();
        let address = address.into();
        debug!(%node_id, %address, "local_sched_mgr: register");
        self.locals.insert(node_id, address);
    }

    pub fn unregister(&self, node_id: &str) {
        self.locals.remove(node_id);
    }

    pub fn address_of(&self, node_id: &str) -> Option<String> {
        self.locals.get(node_id).map(|e| e.clone())
    }

    /// Evict with grace wait + retry placeholder until `LocalSchedulerService` RPC is wired.
    /// `grace` mirrors C++ `timeoutsec` before dropping the local registration.
    pub async fn evict_agent_with_ack(
        &self,
        node_id: &str,
        _reason: &str,
        grace: Duration,
    ) -> Result<(), String> {
        if self.address_of(node_id).is_none() {
            return Err("local scheduler not found".into());
        }
        let wait = grace.max(self.evict_timeout);
        for _ in 0..self.evict_retries {
            tokio::time::sleep(wait).await;
        }
        self.locals.remove(node_id);
        Ok(())
    }

    pub fn disconnect_all(&self) {
        self.locals.clear();
    }
}
