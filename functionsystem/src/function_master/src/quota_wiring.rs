//! Quota step-3 wiring: drive `QuotaEnforcer` from the master's instance watch and
//! execute enforcement (evict + cooldown push) against the owning proxy.
//!
//! C++ analogue: `QuotaManagerActor` consumes instance RUNNING/EXITED events,
//! LIFO-evicts via ForwardKill, and sends `TenantQuotaExceeded{tenant, cooldownMs}`
//! to the proxy's InstanceCtrl (which blocks the tenant's schedules).

use std::sync::Arc;

use parking_lot::Mutex;
use serde_json::Value;
use tokio::sync::mpsc;
use tracing::{info, warn};
use yr_proto::internal::local_scheduler_service_client::LocalSchedulerServiceClient;
use yr_proto::internal::{EvictInstancesRequest, TenantCooldownRequest};

use crate::quota::QuotaEnforcer;

/// One enforcement action produced by the watch-side decision and executed by the
/// RPC task (keeps tonic clients out of the watch path).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct QuotaAction {
    pub tenant_id: String,
    pub evict_instance_ids: Vec<String>,
    pub cooldown_ms: i64,
    /// gRPC endpoint of the proxy owning the evicted instances (from the busproxy
    /// registration of the instance's node).
    pub proxy_grpc: String,
}

/// Fields the quota path needs from one instance JSON record.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InstanceQuotaView {
    pub instance_id: String,
    pub tenant: String,
    pub node_id: String,
    pub running: bool,
    pub cpu_millicores: i64,
    pub mem_mb: i64,
    pub created_at_ms: i64,
}

/// Parse the quota-relevant fields out of a persisted instance JSON
/// (`InstanceMetadata` serde shape). Returns None when the record has no id.
pub fn instance_quota_view(v: &Value) -> Option<InstanceQuotaView> {
    let o = v.as_object()?;
    let instance_id = o.get("id")?.as_str()?.to_string();
    if instance_id.trim().is_empty() {
        return None;
    }
    let tenant = o
        .get("tenant")
        .and_then(|t| t.as_str())
        .unwrap_or("")
        .to_string();
    let node_id = o
        .get("node_id")
        .and_then(|t| t.as_str())
        .unwrap_or("")
        .to_string();
    let state = o
        .get("state")
        .and_then(|s| s.as_str())
        .unwrap_or("")
        .to_ascii_lowercase();
    let running = state == "running";
    let (mut cpu, mut mem) = (0i64, 0i64);
    if let Some(res) = o.get("resources").and_then(|r| r.as_object()) {
        for (k, val) in res {
            let n = val.as_f64().unwrap_or(0.0) as i64;
            match k.to_ascii_uppercase().as_str() {
                "CPU" => cpu = n,
                "MEMORY" | "MEM" => mem = n,
                _ => {}
            }
        }
    }
    let created_at_ms = o
        .get("created_at_ms")
        .and_then(|c| c.as_i64())
        .unwrap_or(0);
    Some(InstanceQuotaView {
        instance_id,
        tenant,
        node_id,
        running,
        cpu_millicores: cpu,
        mem_mb: mem,
        created_at_ms,
    })
}

/// Shared quota state hooked into the instance watch.
pub struct QuotaState {
    enforcer: Mutex<QuotaEnforcer>,
    tx: mpsc::UnboundedSender<QuotaAction>,
}

impl QuotaState {
    /// Returns None when enforcement is disabled (no config file). The returned
    /// receiver must be passed to [`spawn_action_executor`].
    pub fn new(enforcer: QuotaEnforcer) -> Option<(Arc<Self>, mpsc::UnboundedReceiver<QuotaAction>)> {
        if !enforcer.is_enabled() {
            return None;
        }
        let (tx, rx) = mpsc::unbounded_channel();
        Some((
            Arc::new(Self {
                enforcer: Mutex::new(enforcer),
                tx,
            }),
            rx,
        ))
    }

    /// Drive the enforcer from one instance upsert. `resolve_proxy` maps a node_id
    /// to the proxy gRPC endpoint (busproxy registration); enforcement is dropped
    /// with a warning when the proxy is unknown.
    pub fn on_instance_upsert(
        &self,
        old: Option<&Value>,
        new: &Value,
        resolve_proxy: impl Fn(&str) -> Option<String>,
    ) {
        let Some(view) = instance_quota_view(new) else {
            return;
        };
        let was_running = old
            .and_then(instance_quota_view)
            .map(|v| v.running)
            .unwrap_or(false);

        if view.running && !was_running {
            let outcome = self.enforcer.lock().on_instance_running(
                &view.tenant,
                &view.instance_id,
                view.cpu_millicores,
                view.mem_mb,
                view.created_at_ms,
            );
            if outcome.evict.is_empty() {
                return;
            }
            let Some(proxy_grpc) = resolve_proxy(&view.node_id) else {
                warn!(
                    tenant = %view.tenant,
                    node_id = %view.node_id,
                    evict = ?outcome.evict,
                    "tenant over quota but owning proxy unknown; skipping eviction"
                );
                return;
            };
            let action = QuotaAction {
                tenant_id: view.tenant.clone(),
                evict_instance_ids: outcome.evict,
                cooldown_ms: outcome.cooldown_ms.unwrap_or(0),
                proxy_grpc,
            };
            info!(
                tenant = %action.tenant_id,
                evict = ?action.evict_instance_ids,
                cooldown_ms = action.cooldown_ms,
                "tenant over quota: dispatching eviction + cooldown"
            );
            let _ = self.tx.send(action);
        } else if !view.running && was_running {
            self.enforcer
                .lock()
                .on_instance_exited(&view.tenant, &view.instance_id);
        }
    }

    /// Drive the enforcer from one instance delete event (uses the cached old value).
    pub fn on_instance_removed(&self, old: Option<&Value>) {
        let Some(view) = old.and_then(instance_quota_view) else {
            return;
        };
        self.enforcer
            .lock()
            .on_instance_exited(&view.tenant, &view.instance_id);
    }
}

/// Execute enforcement actions: EvictInstances + NotifyTenantCooldown on the
/// owning proxy's LocalSchedulerService.
pub fn spawn_action_executor(mut rx: mpsc::UnboundedReceiver<QuotaAction>) {
    tokio::spawn(async move {
        while let Some(action) = rx.recv().await {
            let uri = if action.proxy_grpc.starts_with("http://")
                || action.proxy_grpc.starts_with("https://")
            {
                action.proxy_grpc.clone()
            } else {
                format!("http://{}", action.proxy_grpc)
            };
            let mut client = match LocalSchedulerServiceClient::connect(uri.clone()).await {
                Ok(c) => c,
                Err(e) => {
                    warn!(error = %e, %uri, "quota: connect proxy failed; dropping action");
                    continue;
                }
            };
            match client
                .evict_instances(EvictInstancesRequest {
                    instance_ids: action.evict_instance_ids.clone(),
                    reason: "tenant quota exceeded".into(),
                })
                .await
            {
                Ok(resp) => info!(
                    tenant = %action.tenant_id,
                    evicted = ?resp.into_inner().evicted_ids,
                    "quota eviction executed"
                ),
                Err(e) => warn!(error = %e, tenant = %action.tenant_id, "quota eviction RPC failed"),
            }
            if action.cooldown_ms > 0 {
                if let Err(e) = client
                    .notify_tenant_cooldown(TenantCooldownRequest {
                        tenant_id: action.tenant_id.clone(),
                        cooldown_ms: action.cooldown_ms,
                    })
                    .await
                {
                    warn!(error = %e, tenant = %action.tenant_id, "quota cooldown RPC failed");
                }
            }
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::quota::QuotaConfig;
    use serde_json::json;

    fn inst(id: &str, tenant: &str, state: &str, cpu: f64, mem: f64, at: i64) -> Value {
        json!({
            "id": id,
            "tenant": tenant,
            "node_id": "node-1",
            "state": state,
            "created_at_ms": at,
            "resources": {"CPU": cpu, "Memory": mem},
        })
    }

    fn quota_state(cpu: i64, mem: i64) -> (Arc<QuotaState>, mpsc::UnboundedReceiver<QuotaAction>) {
        static SEQ: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
        let seq = SEQ.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
        let path = std::env::temp_dir().join(format!(
            "yr_quota_wiring_{}_{seq}.json",
            std::process::id()
        ));
        std::fs::write(
            &path,
            format!(r#"{{"default":{{"cpuMillicores":{cpu},"memMb":{mem},"cooldownMs":5000}}}}"#),
        )
        .expect("write quota config");
        let cfg = QuotaConfig::load_from_file(path.to_str().unwrap()).expect("config json");
        let _ = std::fs::remove_file(&path);
        QuotaState::new(QuotaEnforcer::new(cfg)).expect("enabled")
    }

    #[test]
    fn parses_instance_quota_view() {
        let v = inst("i1", "ten", "Running", 1000.0, 2048.0, 42);
        let view = instance_quota_view(&v).expect("view");
        assert_eq!(view.instance_id, "i1");
        assert_eq!(view.tenant, "ten");
        assert!(view.running);
        assert_eq!(view.cpu_millicores, 1000);
        assert_eq!(view.mem_mb, 2048);
        assert_eq!(view.created_at_ms, 42);
    }

    #[test]
    fn over_quota_dispatches_lifo_eviction_action() {
        let (qs, mut rx) = quota_state(1500, 1_000_000);
        let resolve = |_: &str| Some("1.2.3.4:22772".to_string());
        qs.on_instance_upsert(None, &inst("i-old", "ten", "Running", 1000.0, 10.0, 1), resolve);
        assert!(rx.try_recv().is_err(), "first instance under quota");
        qs.on_instance_upsert(None, &inst("i-new", "ten", "Running", 1000.0, 10.0, 2), resolve);
        let action = rx.try_recv().expect("over quota dispatches action");
        // LIFO: the newest instance is evicted.
        assert_eq!(action.evict_instance_ids, vec!["i-new".to_string()]);
        assert_eq!(action.cooldown_ms, 5000);
        assert_eq!(action.proxy_grpc, "1.2.3.4:22772");
        assert_eq!(action.tenant_id, "ten");
    }

    #[test]
    fn exit_releases_usage_and_unknown_proxy_skips() {
        let (qs, mut rx) = quota_state(1500, 1_000_000);
        let resolve_none = |_: &str| None;
        let running1 = inst("i1", "ten", "Running", 1000.0, 10.0, 1);
        qs.on_instance_upsert(None, &running1, resolve_none);
        // i1 exits → usage released.
        let exited1 = inst("i1", "ten", "Evicted", 1000.0, 10.0, 1);
        qs.on_instance_upsert(Some(&running1), &exited1, resolve_none);
        // i2 now fits.
        qs.on_instance_upsert(None, &inst("i2", "ten", "Running", 1000.0, 10.0, 2), resolve_none);
        assert!(rx.try_recv().is_err(), "no eviction after release");
        // i3 over quota but proxy unknown → skipped (logged), no action.
        qs.on_instance_upsert(None, &inst("i3", "ten", "Running", 1000.0, 10.0, 3), resolve_none);
        assert!(rx.try_recv().is_err(), "unknown proxy drops the action");
    }

    #[test]
    fn duplicate_running_upserts_are_idempotent() {
        let (qs, mut rx) = quota_state(1500, 1_000_000);
        let resolve = |_: &str| Some("p:1".to_string());
        let running = inst("i1", "ten", "Running", 1000.0, 10.0, 1);
        qs.on_instance_upsert(None, &running, resolve);
        // Same RUNNING record again (watch replay) — no double count.
        qs.on_instance_upsert(Some(&running), &running, resolve);
        qs.on_instance_upsert(None, &inst("i2", "ten", "Running", 400.0, 10.0, 2), resolve);
        assert!(rx.try_recv().is_err(), "1400 <= 1500 after idempotent replay");
    }

    #[test]
    fn delete_event_releases_usage() {
        let (qs, mut rx) = quota_state(1500, 1_000_000);
        let resolve = |_: &str| Some("p:1".to_string());
        let running = inst("i1", "ten", "Running", 1000.0, 10.0, 1);
        qs.on_instance_upsert(None, &running, resolve);
        qs.on_instance_removed(Some(&running));
        qs.on_instance_upsert(None, &inst("i2", "ten", "Running", 1000.0, 10.0, 2), resolve);
        assert!(rx.try_recv().is_err(), "usage released by delete");
    }
}
