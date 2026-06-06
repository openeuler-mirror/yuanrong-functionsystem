//! Aggregated sandbox state — port of C++ `runtime_manager/executor/sandbox/
//! runtime_state_manager.{h,cpp}`.
//!
//! Owns and enforces sandbox-state invariants behind a single lock so
//! `register`/`unregister` are atomic across all internal maps (the C++ code got
//! this from its single actor thread; in Rust we use one `Mutex<Inner>`).
//! WarmUp state and active-sandbox state are orthogonal: a runtime_id is in at
//! most one of them.

use std::collections::{HashMap, HashSet};

use parking_lot::Mutex;
use yr_proto::messages::RuntimeInstanceInfo;
use yr_proto::runtime::v1::FunctionRuntime;

/// Aggregated state for one sandbox instance (C++ `SandboxInfo`).
#[derive(Debug, Clone, Default, PartialEq)]
pub struct SandboxInfo {
    pub runtime_id: String,
    pub sandbox_id: String,
    pub checkpoint_id: String,
    pub port_mappings_json: String,
    pub instance_info: Option<RuntimeInstanceInfo>,
}

#[derive(Default)]
struct Inner {
    sandboxes: HashMap<String, SandboxInfo>,
    in_progress: HashSet<String>,
    pending_deletes: HashSet<String>,
    warm_up: HashMap<String, FunctionRuntime>,
}

#[derive(Default)]
pub struct RuntimeStateManager {
    inner: Mutex<Inner>,
}

impl RuntimeStateManager {
    pub fn new() -> Self {
        Self::default()
    }

    // ── Registration (atomic) ──────────────────────────────────────────────
    pub fn register(&self, info: SandboxInfo) {
        let mut g = self.inner.lock();
        g.sandboxes.insert(info.runtime_id.clone(), info);
    }

    pub fn unregister(&self, runtime_id: &str) {
        let mut g = self.inner.lock();
        g.sandboxes.remove(runtime_id);
        g.in_progress.remove(runtime_id);
        g.pending_deletes.remove(runtime_id);
        g.warm_up.remove(runtime_id);
    }

    // ── Queries ─────────────────────────────────────────────────────────────
    pub fn find(&self, runtime_id: &str) -> Option<SandboxInfo> {
        self.inner.lock().sandboxes.get(runtime_id).cloned()
    }

    pub fn has_sandbox(&self, runtime_id: &str) -> bool {
        self.inner.lock().sandboxes.contains_key(runtime_id)
    }

    pub fn get_sandbox_id(&self, runtime_id: &str) -> String {
        self.inner
            .lock()
            .sandboxes
            .get(runtime_id)
            .map(|s| s.sandbox_id.clone())
            .unwrap_or_default()
    }

    pub fn get_checkpoint_id(&self, runtime_id: &str) -> String {
        self.inner
            .lock()
            .sandboxes
            .get(runtime_id)
            .map(|s| s.checkpoint_id.clone())
            .unwrap_or_default()
    }

    pub fn get_port_mappings_json(&self, runtime_id: &str) -> String {
        self.inner
            .lock()
            .sandboxes
            .get(runtime_id)
            .map(|s| s.port_mappings_json.clone())
            .unwrap_or_default()
    }

    /// Reverse lookup: runtime_id owning a given sandbox_id (empty if none).
    pub fn find_runtime_id_by_sandbox_id(&self, sandbox_id: &str) -> String {
        self.inner
            .lock()
            .sandboxes
            .values()
            .find(|s| s.sandbox_id == sandbox_id)
            .map(|s| s.runtime_id.clone())
            .unwrap_or_default()
    }

    pub fn all_sandboxes(&self) -> HashMap<String, SandboxInfo> {
        self.inner.lock().sandboxes.clone()
    }

    pub fn active_count(&self) -> usize {
        self.inner.lock().sandboxes.len()
    }

    // ── Partial updates ───────────────────────────────────────────────────────
    pub fn update_sandbox_id(&self, runtime_id: &str, sandbox_id: &str) {
        if let Some(s) = self.inner.lock().sandboxes.get_mut(runtime_id) {
            s.sandbox_id = sandbox_id.to_string();
        }
    }

    pub fn update_checkpoint(&self, runtime_id: &str, checkpoint_id: &str) {
        if let Some(s) = self.inner.lock().sandboxes.get_mut(runtime_id) {
            s.checkpoint_id = checkpoint_id.to_string();
        }
    }

    pub fn clear_checkpoint_id(&self, runtime_id: &str) {
        if let Some(s) = self.inner.lock().sandboxes.get_mut(runtime_id) {
            s.checkpoint_id.clear();
        }
    }

    pub fn update_port_mappings(&self, runtime_id: &str, port_mappings_json: &str) {
        if let Some(s) = self.inner.lock().sandboxes.get_mut(runtime_id) {
            s.port_mappings_json = port_mappings_json.to_string();
        }
    }

    // ── In-progress start tracking (dedup) ─────────────────────────────────────
    pub fn mark_start_in_progress(&self, runtime_id: &str) {
        self.inner.lock().in_progress.insert(runtime_id.to_string());
    }

    pub fn mark_start_done(&self, runtime_id: &str) {
        self.inner.lock().in_progress.remove(runtime_id);
    }

    pub fn is_start_in_progress(&self, runtime_id: &str) -> bool {
        self.inner.lock().in_progress.contains(runtime_id)
    }

    // ── Pending-delete tracking (Stop arriving mid-Start) ──────────────────────
    pub fn mark_pending_delete(&self, runtime_id: &str) {
        self.inner
            .lock()
            .pending_deletes
            .insert(runtime_id.to_string());
    }

    pub fn clear_pending_delete(&self, runtime_id: &str) {
        self.inner.lock().pending_deletes.remove(runtime_id);
    }

    pub fn is_pending_delete(&self, runtime_id: &str) -> bool {
        self.inner.lock().pending_deletes.contains(runtime_id)
    }

    // ── Warm-up state (orthogonal to active sandboxes) ─────────────────────────
    pub fn register_warm_up(&self, runtime_id: &str, proto: FunctionRuntime) {
        self.inner
            .lock()
            .warm_up
            .insert(runtime_id.to_string(), proto);
    }

    pub fn unregister_warm_up(&self, runtime_id: &str) {
        self.inner.lock().warm_up.remove(runtime_id);
    }

    pub fn is_warm_up(&self, runtime_id: &str) -> bool {
        self.inner.lock().warm_up.contains_key(runtime_id)
    }

    pub fn get_warm_up(&self, runtime_id: &str) -> Option<FunctionRuntime> {
        self.inner.lock().warm_up.get(runtime_id).cloned()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn info(id: &str) -> SandboxInfo {
        SandboxInfo {
            runtime_id: id.to_string(),
            sandbox_id: format!("sb-{id}"),
            ..Default::default()
        }
    }

    #[test]
    fn register_find_unregister() {
        let m = RuntimeStateManager::new();
        m.register(info("r1"));
        assert!(m.has_sandbox("r1"));
        assert_eq!(m.get_sandbox_id("r1"), "sb-r1");
        assert_eq!(m.find_runtime_id_by_sandbox_id("sb-r1"), "r1");
        m.unregister("r1");
        assert!(!m.has_sandbox("r1"));
        assert_eq!(m.find_runtime_id_by_sandbox_id("sb-r1"), "");
    }

    #[test]
    fn unregister_clears_all_associated_state_atomically() {
        let m = RuntimeStateManager::new();
        m.register(info("r1"));
        m.mark_start_in_progress("r1");
        m.mark_pending_delete("r1");
        m.unregister("r1");
        assert!(!m.is_start_in_progress("r1"));
        assert!(!m.is_pending_delete("r1"));
        assert!(!m.has_sandbox("r1"));
    }

    #[test]
    fn partial_updates() {
        let m = RuntimeStateManager::new();
        m.register(info("r1"));
        m.update_sandbox_id("r1", "sb-new");
        m.update_checkpoint("r1", "ckpt-1");
        m.update_port_mappings("r1", "tcp:40001:8080");
        let s = m.find("r1").unwrap();
        assert_eq!(s.sandbox_id, "sb-new");
        assert_eq!(s.checkpoint_id, "ckpt-1");
        assert_eq!(s.port_mappings_json, "tcp:40001:8080");
        m.clear_checkpoint_id("r1");
        assert_eq!(m.get_checkpoint_id("r1"), "");
    }

    #[test]
    fn in_progress_and_pending_delete_flags() {
        let m = RuntimeStateManager::new();
        assert!(!m.is_start_in_progress("r1"));
        m.mark_start_in_progress("r1");
        assert!(m.is_start_in_progress("r1"));
        m.mark_pending_delete("r1");
        assert!(m.is_pending_delete("r1"));
        m.mark_start_done("r1");
        m.clear_pending_delete("r1");
        assert!(!m.is_start_in_progress("r1"));
        assert!(!m.is_pending_delete("r1"));
    }

    #[test]
    fn warm_up_is_orthogonal() {
        let m = RuntimeStateManager::new();
        m.register_warm_up("w1", FunctionRuntime::default());
        assert!(m.is_warm_up("w1"));
        assert!(!m.has_sandbox("w1"));
        assert!(m.get_warm_up("w1").is_some());
        m.unregister_warm_up("w1");
        assert!(!m.is_warm_up("w1"));
    }
}
