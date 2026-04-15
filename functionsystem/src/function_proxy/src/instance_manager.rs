//! Full instance lifecycle view over [`InstanceController`]: queries, stats, destroy, eviction.
//!
//! State graph follows `yr_common::types::InstanceState` (New → Scheduling → Creating → Running → …).

use std::collections::HashMap;
use std::sync::Arc;

use tracing::{info, warn};
use yr_common::types::InstanceState;

use crate::config::Config;
use crate::instance_ctrl::InstanceController;
use crate::state_machine::InstanceMetadata;

/// Aggregated counts for scheduling / observability (C++ instance manager style).
#[derive(Debug, Clone, Default)]
pub struct InstanceStatistics {
    pub total: usize,
    pub by_state: HashMap<InstanceState, usize>,
    pub by_function: HashMap<String, usize>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FunctionMetaChangeKind {
    Upsert,
    Delete,
}

/// High-level lifecycle + query API. Creation from schedule remains in [`crate::local_scheduler`];
/// this type centralizes teardown, eviction, and introspection.
pub struct InstanceManager {
    ctrl: Arc<InstanceController>,
    #[allow(dead_code)]
    config: Arc<Config>,
}

impl InstanceManager {
    pub fn new(ctrl: Arc<InstanceController>, config: Arc<Config>) -> Arc<Self> {
        Arc::new(Self { ctrl, config })
    }

    pub fn controller(&self) -> &Arc<InstanceController> {
        &self.ctrl
    }

    pub fn list_all(&self) -> Vec<InstanceMetadata> {
        self.ctrl
            .instances()
            .iter()
            .map(|e| e.value().clone())
            .collect()
    }

    pub fn list_by_state(&self, state: InstanceState) -> Vec<InstanceMetadata> {
        self.ctrl
            .instances()
            .iter()
            .filter(|e| e.value().state == state)
            .map(|e| e.value().clone())
            .collect()
    }

    pub fn list_by_function(&self, function_name: &str) -> Vec<InstanceMetadata> {
        self.ctrl
            .instances()
            .iter()
            .filter(|e| e.value().function_name == function_name)
            .map(|e| e.value().clone())
            .collect()
    }

    pub fn list_by_node(&self, node_id: &str) -> Vec<InstanceMetadata> {
        self.ctrl
            .instances()
            .iter()
            .filter(|e| e.value().node_id == node_id)
            .map(|e| e.value().clone())
            .collect()
    }

    pub fn statistics(&self) -> InstanceStatistics {
        let mut s = InstanceStatistics::default();
        for e in self.ctrl.instances().iter() {
            let m = e.value();
            s.total += 1;
            *s.by_state.entry(m.state).or_insert(0) += 1;
            *s.by_function
                .entry(m.function_name.clone())
                .or_insert(0) += 1;
        }
        s
    }

    fn had_committed_usage(state: InstanceState) -> bool {
        matches!(
            state,
            InstanceState::Running | InstanceState::SubHealth | InstanceState::Suspend
        )
    }

    /// Graceful stop: move to `Exiting`, call runtime stop, then `Exited` and release capacity when it was committed.
    pub async fn destroy_instance_graceful(
        &self,
        instance_id: &str,
    ) -> Result<InstanceMetadata, tonic::Status> {
        let meta = self
            .ctrl
            .get(instance_id)
            .ok_or_else(|| tonic::Status::not_found("instance not found"))?;

        if matches!(
            meta.state,
            InstanceState::Exited | InstanceState::Evicted | InstanceState::Fatal
        ) {
            return Ok(meta);
        }

        let before = meta.state;
        let release = Self::had_committed_usage(before) || before == InstanceState::Evicting;

        if matches!(
            before,
            InstanceState::Running | InstanceState::SubHealth | InstanceState::Suspend
        ) {
            self.ctrl
                .transition_with_version(instance_id, InstanceState::Exiting, None)
                .await?;
        } else if before == InstanceState::Creating {
            if meta.runtime_id.is_empty() {
                self.ctrl
                    .transition_with_version(instance_id, InstanceState::Failed, None)
                    .await?;
                return self
                    .ctrl
                    .get(instance_id)
                    .ok_or_else(|| tonic::Status::internal("instance vanished"));
            }
            self.ctrl
                .transition_with_version(instance_id, InstanceState::Exiting, None)
                .await?;
        } else if matches!(
            before,
            InstanceState::Scheduling | InstanceState::New | InstanceState::ScheduleFailed
        ) {
            self.ctrl
                .transition_with_version(instance_id, InstanceState::Failed, None)
                .await?;
            return self
                .ctrl
                .get(instance_id)
                .ok_or_else(|| tonic::Status::internal("instance vanished"));
        } else if before == InstanceState::Exiting {
            // already tearing down; still try stop + finalize
        } else if before == InstanceState::Failed {
            return Ok(meta);
        }

        let meta = self
            .ctrl
            .get(instance_id)
            .ok_or_else(|| tonic::Status::not_found("instance not found"))?;
        if !meta.runtime_id.is_empty() {
            if let Err(e) = self
                .ctrl
                .stop_instance(instance_id, &meta.runtime_id, false)
                .await
            {
                warn!(%instance_id, error = %e, "graceful stop_instance failed");
            }
        }

        let out = self
            .ctrl
            .transition_terminal_with_release(instance_id, InstanceState::Exited, release)
            .await;
        out.ok_or_else(|| tonic::Status::failed_precondition("transition to EXITED failed"))
    }

    /// Force kill runtime then mark `Exited` (or `Failed` if transition disallows).
    pub async fn destroy_instance_force(
        &self,
        instance_id: &str,
    ) -> Result<InstanceMetadata, tonic::Status> {
        let meta = self
            .ctrl
            .get(instance_id)
            .ok_or_else(|| tonic::Status::not_found("instance not found"))?;

        if matches!(
            meta.state,
            InstanceState::Exited | InstanceState::Evicted | InstanceState::Fatal
        ) {
            return Ok(meta);
        }

        let before = meta.state;
        let release = Self::had_committed_usage(before) || before == InstanceState::Evicting;

        if matches!(before, InstanceState::Running | InstanceState::SubHealth | InstanceState::Suspend)
        {
            let _ = self
                .ctrl
                .transition_with_version(instance_id, InstanceState::Exiting, None)
                .await;
        }

        let meta = self
            .ctrl
            .get(instance_id)
            .ok_or_else(|| tonic::Status::not_found("instance not found"))?;
        if !meta.runtime_id.is_empty() {
            let _ = self
                .ctrl
                .stop_instance(instance_id, &meta.runtime_id, true)
                .await;
        }

        if let Some(m) = self
            .ctrl
            .transition_terminal_with_release(instance_id, InstanceState::Exited, release)
            .await
        {
            return Ok(m);
        }
        let m = self
            .ctrl
            .transition_terminal_with_release(instance_id, InstanceState::Failed, false)
            .await;
        m.ok_or_else(|| tonic::Status::internal("force destroy: could not finalize instance"))
    }

    /// Eviction path: `Running`/`SubHealth` → `Evicting` → stop → `Evicted`.
    pub async fn evict_instance(
        &self,
        instance_id: &str,
        force_kill: bool,
    ) -> Result<InstanceMetadata, tonic::Status> {
        let meta = self
            .ctrl
            .get(instance_id)
            .ok_or_else(|| tonic::Status::not_found("instance not found"))?;

        if matches!(
            meta.state,
            InstanceState::Exited | InstanceState::Evicted | InstanceState::Fatal
        ) {
            return Ok(meta);
        }

        let before = meta.state;
        if matches!(before, InstanceState::Running | InstanceState::SubHealth) {
            self.ctrl
                .transition_with_version(instance_id, InstanceState::Evicting, None)
                .await?;
        } else {
            return Err(tonic::Status::failed_precondition(
                "instance not in a state that supports eviction",
            ));
        }

        let meta = self
            .ctrl
            .get(instance_id)
            .ok_or_else(|| tonic::Status::not_found("instance not found"))?;
        if !meta.runtime_id.is_empty() {
            let _ = self
                .ctrl
                .stop_instance(instance_id, &meta.runtime_id, force_kill)
                .await;
        }

        let release = Self::had_committed_usage(InstanceState::Running);
        let out = self
            .ctrl
            .transition_terminal_with_release(instance_id, InstanceState::Evicted, release)
            .await;
        out.ok_or_else(|| tonic::Status::failed_precondition("transition to EVICTED failed"))
    }

    /// Hook when function catalog keys change under etcd (watch path). Reserved for future preemption / refresh.
    pub fn on_function_meta_change(&self, func_key: &str, kind: FunctionMetaChangeKind) {
        let total = self.statistics().total;
        match kind {
            FunctionMetaChangeKind::Upsert => {
                info!(
                    target: "yr_proxy::function_meta",
                    %func_key,
                    instance_records_on_node = total,
                    "function metadata upserted"
                );
            }
            FunctionMetaChangeKind::Delete => {
                info!(
                    target: "yr_proxy::function_meta",
                    %func_key,
                    instance_records_on_node = total,
                    "function metadata deleted (instances may be stale until rescheduled)"
                );
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::resource_view::{ResourceVector, ResourceView};
    use clap::Parser;
    use std::collections::HashMap;

    fn test_manager() -> Arc<InstanceManager> {
        let cfg = Arc::new(
            Config::try_parse_from(["yr-proxy", "--node-id", "n1", "--grpc-listen-port", "1"]).unwrap(),
        );
        let rv = ResourceView::new(ResourceVector {
            cpu: 8.0,
            memory: 64.0,
            npu: 0.0,
        });
        let ctrl = InstanceController::new(cfg.clone(), rv, None, None);
        InstanceManager::new(ctrl, cfg)
    }

    fn meta(id: &str, state: InstanceState, func: &str) -> InstanceMetadata {
        let now = InstanceMetadata::now_ms();
        InstanceMetadata {
            id: id.into(),
            function_name: func.into(),
            tenant: "t".into(),
            node_id: "n1".into(),
            runtime_id: if state == InstanceState::Running {
                "rt1".into()
            } else {
                String::new()
            },
            runtime_port: if state == InstanceState::Running { 1 } else { 0 },
            state,
            created_at_ms: now,
            updated_at_ms: now,
            group_id: None,
            trace_id: String::new(),
            resources: HashMap::from([("cpu".into(), 1.0), ("memory".into(), 512.0)]),
            etcd_kv_version: None,
            etcd_mod_revision: None,
        }
    }

    #[test]
    fn statistics_counts_by_state_and_function() {
        let m = test_manager();
        m.controller().insert_metadata(meta("a", InstanceState::Running, "f1"));
        m.controller().insert_metadata(meta("b", InstanceState::Running, "f1"));
        m.controller().insert_metadata(meta("c", InstanceState::Creating, "f2"));
        let s = m.statistics();
        assert_eq!(s.total, 3);
        assert_eq!(*s.by_state.get(&InstanceState::Running).unwrap(), 2);
        assert_eq!(*s.by_state.get(&InstanceState::Creating).unwrap(), 1);
        assert_eq!(*s.by_function.get("f1").unwrap(), 2);
        assert_eq!(*s.by_function.get("f2").unwrap(), 1);
    }

    #[test]
    fn list_by_node_and_function_filters() {
        let m = test_manager();
        m.controller().insert_metadata(meta("x", InstanceState::Scheduling, "fn"));
        assert_eq!(m.list_by_node("n1").len(), 1);
        assert_eq!(m.list_by_function("fn").len(), 1);
        assert_eq!(m.list_by_state(InstanceState::Scheduling).len(), 1);
    }
}
