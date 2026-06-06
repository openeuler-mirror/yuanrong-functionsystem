//! Fail-safe start RAII — port of C++ `SandboxStartGuard`.
//!
//! Construction registers the in-progress start; if the guard is dropped without
//! `commit()` (any early-return / `?` on a start path), `Drop` rolls back all
//! state for the runtime_id (unregister + clear in-progress). On `commit()` the
//! rollback is suppressed and the sandbox stays registered.

use std::sync::Arc;

use super::runtime_state_manager::RuntimeStateManager;

pub struct SandboxStartGuard {
    state: Arc<RuntimeStateManager>,
    runtime_id: String,
    committed: bool,
}

impl SandboxStartGuard {
    /// Begin a start: marks the runtime_id in-progress.
    pub fn begin(state: Arc<RuntimeStateManager>, runtime_id: impl Into<String>) -> Self {
        let runtime_id = runtime_id.into();
        state.mark_start_in_progress(&runtime_id);
        Self {
            state,
            runtime_id,
            committed: false,
        }
    }

    /// Mark the start successful; suppresses rollback on drop.
    pub fn commit(mut self) {
        self.committed = true;
        // mark_start_done is the normal end-of-start bookkeeping (no rollback).
        self.state.mark_start_done(&self.runtime_id);
    }

    pub fn runtime_id(&self) -> &str {
        &self.runtime_id
    }
}

impl Drop for SandboxStartGuard {
    fn drop(&mut self) {
        if !self.committed {
            // Any mid-start failure: roll back all state for this runtime_id.
            self.state.unregister(&self.runtime_id);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::super::runtime_state_manager::{RuntimeStateManager, SandboxInfo};
    use super::*;

    fn reg(state: &RuntimeStateManager, id: &str) {
        state.register(SandboxInfo {
            runtime_id: id.to_string(),
            sandbox_id: format!("sb-{id}"),
            ..Default::default()
        });
    }

    #[test]
    fn dropped_without_commit_rolls_back() {
        let state = Arc::new(RuntimeStateManager::new());
        {
            let _g = SandboxStartGuard::begin(state.clone(), "r1");
            reg(&state, "r1");
            assert!(state.is_start_in_progress("r1"));
            assert!(state.has_sandbox("r1"));
            // guard dropped here without commit → rollback
        }
        assert!(!state.is_start_in_progress("r1"));
        assert!(!state.has_sandbox("r1"));
    }

    #[test]
    fn commit_keeps_sandbox_registered() {
        let state = Arc::new(RuntimeStateManager::new());
        {
            let g = SandboxStartGuard::begin(state.clone(), "r1");
            reg(&state, "r1");
            g.commit();
        }
        assert!(state.has_sandbox("r1"));
        assert!(!state.is_start_in_progress("r1")); // commit cleared the in-progress flag
    }

    #[test]
    fn early_return_via_question_mark_rolls_back() {
        let state = Arc::new(RuntimeStateManager::new());
        fn start(state: Arc<RuntimeStateManager>, fail: bool) -> Result<(), &'static str> {
            let g = SandboxStartGuard::begin(state.clone(), "r1");
            state.register(SandboxInfo {
                runtime_id: "r1".into(),
                ..Default::default()
            });
            if fail {
                return Err("boom"); // g drops → rollback
            }
            g.commit();
            Ok(())
        }
        assert!(start(state.clone(), true).is_err());
        assert!(!state.has_sandbox("r1"));
        assert!(start(state.clone(), false).is_ok());
        assert!(state.has_sandbox("r1"));
    }
}
