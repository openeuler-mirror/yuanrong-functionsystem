//! Cold start / proxy restart: reload instance rows from MetaStore and normalize in-flight state.
//!
//! Running instances are loaded for local bookkeeping; runtimes reconnect over the data plane when they
//! open streams again (see bus proxy). Stale `Scheduling` / `Creating` rows without a runtime id are
//! marked `Failed` so the state machine can make progress.

use tracing::info;
use yr_common::types::InstanceState;

use crate::instance_ctrl::InstanceController;

/// Outcome counters for logs and tests.
#[derive(Debug, Clone, Default, Eq, PartialEq)]
pub struct RecoverSummary {
    /// Rows merged into memory from etcd (same as log `loaded` in rehydrate).
    pub rehydrated: u32,
    /// In-flight instances without a runtime that were marked failed after restart.
    pub stale_in_flight_marked_failed: u32,
}

/// Load persisted instances for this node, then fail stale pre-runtime schedules (crash window).
pub async fn recover_after_proxy_start(
    ctrl: &InstanceController,
    store: &mut yr_metastore_client::MetaStoreClient,
) -> RecoverSummary {
    let rehydrated = ctrl.rehydrate_local_instances(store).await;
    let stale = resume_stale_in_flight(ctrl).await;
    RecoverSummary {
        rehydrated,
        stale_in_flight_marked_failed: stale,
    }
}

/// Mark `Scheduling` / `Creating` instances that never got a `runtime_id` as `Failed` after a proxy restart.
async fn resume_stale_in_flight(ctrl: &InstanceController) -> u32 {
    let ids: Vec<String> = ctrl
        .instances()
        .iter()
        .filter(|e| {
            matches!(
                e.value().state,
                InstanceState::Scheduling | InstanceState::Creating
            ) && e.value().runtime_id.is_empty()
        })
        .map(|e| e.key().clone())
        .collect();

    let mut n = 0u32;
    for id in ids {
        match ctrl
            .transition_with_version(&id, InstanceState::Failed, None)
            .await
        {
            Ok(_) => {
                n += 1;
                info!(instance_id = %id, "recovery: marked stale in-flight instance as FAILED");
            }
            Err(_) => {}
        }
    }
    n
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::Config;
    use crate::resource_view::{ResourceVector, ResourceView};
    use clap::Parser;
    use std::collections::HashMap;
    use std::sync::Arc;

    #[tokio::test]
    async fn stale_scheduling_marked_failed_without_etcd() {
        let cfg = Arc::new(
            Config::try_parse_from(["yr-proxy", "--node-id", "n1", "--grpc-listen-port", "1"])
                .unwrap(),
        );
        let rv = ResourceView::new(ResourceVector {
            cpu: 8.0,
            memory: 64.0,
            npu: 0.0,
        });
        let ctrl = InstanceController::new(cfg, rv, None, None);
        let now = crate::state_machine::InstanceMetadata::now_ms();
        ctrl.insert_metadata(crate::state_machine::InstanceMetadata {
            id: "stale-1".into(),
            function_name: "f".into(),
            tenant: "t".into(),
            node_id: "n1".into(),
            runtime_id: String::new(),
            runtime_port: 0,
            state: InstanceState::Scheduling,
            created_at_ms: now,
            updated_at_ms: now,
            group_id: None,
            trace_id: String::new(),
            resources: HashMap::new(),
            etcd_kv_version: None,
            etcd_mod_revision: None,
        });
        let n = resume_stale_in_flight(&ctrl).await;
        assert_eq!(n, 1);
        assert_eq!(ctrl.get("stale-1").unwrap().state, InstanceState::Failed);
    }
}
