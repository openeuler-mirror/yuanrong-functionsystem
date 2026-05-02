use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use tokio::sync::Mutex;
use tonic::Request;
use tracing::{info, warn};
use yr_proto::internal::global_scheduler_service_client::GlobalSchedulerServiceClient;
use yr_proto::internal::local_scheduler_service_client::LocalSchedulerServiceClient;
use yr_proto::internal::{
    EvictInstancesRequest, RegisterRequest, UpdateInstanceStatusRequest, UpdateResourcesRequest,
};

use crate::config::Config;
use crate::health_monitor;
use crate::node_manager::NodeManager;
use crate::rm_client::RuntimeManagerClient;

/// Evict payload used for the cheap local-scheduler heartbeat (`heartbeat_ping`).
pub fn heartbeat_evict_request() -> EvictInstancesRequest {
    EvictInstancesRequest {
        instance_ids: vec![],
        reason: "yr-agent-heartbeat".into(),
    }
}

/// `RegisterRequest` aligned with `try_register_global` (function agent role metadata).
pub fn build_global_register_request(
    node_id: impl Into<String>,
    agent_grpc_endpoint: impl Into<String>,
) -> RegisterRequest {
    let node_id = node_id.into();
    let agent_endpoint = agent_grpc_endpoint.into();
    let agent_info_json = serde_json::json!({
        "role": "function_agent",
        "node_id": &node_id,
        "grpc": &agent_endpoint,
    })
    .to_string();
    RegisterRequest {
        node_id: node_id.clone(),
        address: agent_endpoint,
        resource_json: "{}".into(),
        agent_info_json,
        resource_unit: None,
    }
}

/// Periodic resource snapshot RPC body (same shape as the background reporter).
pub fn build_resource_update_request(
    node_id: impl Into<String>,
    resource_json: String,
) -> UpdateResourcesRequest {
    UpdateResourcesRequest {
        node_id: node_id.into(),
        resource_json,
        resource_unit: None,
    }
}

/// Tracks connectivity to the local scheduler and optionally registers like the C++ RegisterHelper.
pub struct SchedulerLink {
    local_uri: String,
    node_id: String,
    agent_endpoint: String,
    local_client: Mutex<Option<LocalSchedulerServiceClient<tonic::transport::Channel>>>,
    pub scheduler_reachable: AtomicBool,
}

impl SchedulerLink {
    pub fn new_arc(local_uri: String, node_id: String, agent_endpoint: String) -> Arc<Self> {
        Arc::new(Self {
            local_uri,
            node_id,
            agent_endpoint,
            local_client: Mutex::new(None),
            scheduler_reachable: AtomicBool::new(false),
        })
    }

    async fn connect_local(
        &self,
    ) -> Result<LocalSchedulerServiceClient<tonic::transport::Channel>, tonic::Status> {
        let uri = Config::normalize_grpc_uri(&self.local_uri);
        let channel = tonic::transport::Endpoint::from_shared(uri)
            .map_err(|e| tonic::Status::internal(e.to_string()))?
            .connect_timeout(Duration::from_secs(5))
            .connect()
            .await
            .map_err(|e| tonic::Status::unavailable(e.to_string()))?;
        Ok(LocalSchedulerServiceClient::new(channel))
    }

    async fn cached_local(
        &self,
    ) -> Result<LocalSchedulerServiceClient<tonic::transport::Channel>, tonic::Status> {
        let mut guard = self.local_client.lock().await;
        if guard.is_none() {
            let c = self.connect_local().await?;
            *guard = Some(c.clone());
            return Ok(c);
        }
        Ok(guard.as_ref().unwrap().clone())
    }

    /// Cheap RPC to verify the local scheduler (function_proxy) is reachable.
    pub async fn heartbeat_ping(&self) -> Result<(), tonic::Status> {
        let mut c = self.cached_local().await?;
        let _ = c
            .evict_instances(Request::new(heartbeat_evict_request()))
            .await?;
        self.scheduler_reachable.store(true, Ordering::SeqCst);
        Ok(())
    }

    /// Best-effort registration with a global scheduler listening on the same gRPC URI (or dedicated master).
    pub async fn try_register_global(&self) -> Result<(), tonic::Status> {
        let uri = Config::normalize_grpc_uri(&self.local_uri);
        let mut client = GlobalSchedulerServiceClient::connect(uri.clone())
            .await
            .map_err(|e| tonic::Status::unavailable(format!("global client: {e}")))?;
        let resp = client
            .register(Request::new(build_global_register_request(
                self.node_id.clone(),
                self.agent_endpoint.clone(),
            )))
            .await?
            .into_inner();
        if !resp.success {
            return Err(tonic::Status::internal(format!(
                "register refused: {}",
                resp.message
            )));
        }
        info!(%uri, "registered function agent with global scheduler");
        Ok(())
    }

    pub async fn forward_update_resources(
        &self,
        req: UpdateResourcesRequest,
    ) -> Result<bool, tonic::Status> {
        let uri = Config::normalize_grpc_uri(&self.local_uri);
        let mut client = GlobalSchedulerServiceClient::connect(uri)
            .await
            .map_err(|e| tonic::Status::unavailable(format!("global connect: {e}")))?;
        let ok = client
            .update_resources(Request::new(req))
            .await?
            .into_inner()
            .success;
        Ok(ok)
    }

    /// LocalSchedulerService has no instance-status RPC yet; we log and treat as acknowledged.
    pub fn forward_instance_status(&self, req: &UpdateInstanceStatusRequest) {
        info!(
            instance_id = %req.instance_id,
            runtime_id = %req.runtime_id,
            status = %req.status,
            exit_code = req.exit_code,
            "instance status (forward to local scheduler not available in proto; logged)"
        );
    }

    pub fn node_id(&self) -> &str {
        &self.node_id
    }

    pub async fn invalidate_local_cache(&self) {
        let mut g = self.local_client.lock().await;
        *g = None;
    }
}

/// Background registration: global register (best effort), periodic local heartbeat, resource scrape.
pub fn spawn_registration_tasks(
    link: Arc<SchedulerLink>,
    rm: Arc<RuntimeManagerClient>,
    runtimes: Arc<dashmap::DashMap<String, String>>,
    node: Arc<NodeManager>,
) {
    let l = link.clone();
    tokio::spawn(async move {
        let mut delay = Duration::from_millis(400);
        for attempt in 0u32..32 {
            match l.try_register_global().await {
                Ok(()) => {
                    info!(
                        attempt,
                        "registered with global scheduler (function_proxy / master)"
                    );
                    break;
                }
                Err(e) => {
                    warn!(attempt, error = %e, "global register retry");
                    tokio::time::sleep(delay).await;
                    delay = (delay * 2).min(Duration::from_secs(20));
                }
            }
        }
    });

    let l = link.clone();
    tokio::spawn(async move {
        let mut reg_interval = tokio::time::interval(Duration::from_secs(30));
        loop {
            reg_interval.tick().await;
            if let Err(e) = l.try_register_global().await {
                warn!(error = %e, "global register (best effort)");
            }
        }
    });

    tokio::spawn(async move {
        let mut beat = tokio::time::interval(Duration::from_secs(10));
        loop {
            beat.tick().await;
            if let Err(e) = link.heartbeat_ping().await {
                warn!(error = %e, "local scheduler heartbeat");
                link.scheduler_reachable.store(false, Ordering::SeqCst);
                link.invalidate_local_cache().await;
            }
            let mut map = serde_json::Map::new();
            for e in runtimes.iter() {
                let rid = e.value().clone();
                if let Ok(st) = rm.get_runtime_status(rid.clone()).await {
                    map.insert(
                        e.key().clone(),
                        serde_json::json!({
                            "runtime_id": rid,
                            "status": st.status,
                            "exit_code": st.exit_code,
                        }),
                    );
                }
            }
            map.insert(
                "node".into(),
                serde_json::json!({
                    "labels": node.labels_json(),
                    "ready": node.ready(),
                }),
            );
            map.insert(
                "host_health".into(),
                health_monitor::host_resource_snapshot(),
            );
            let resource_json = serde_json::Value::Object(map).to_string();
            let req = build_resource_update_request(link.node_id(), resource_json);
            match link.forward_update_resources(req).await {
                Ok(true) => {}
                Ok(false) => warn!("global scheduler update_resources returned success=false"),
                Err(e) => warn!(error = %e, "forward resource snapshot (best effort)"),
            }
        }
    });
}
