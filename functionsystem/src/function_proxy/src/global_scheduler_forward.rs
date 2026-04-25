use std::sync::Arc;
use std::time::{SystemTime, UNIX_EPOCH};
use tonic::{Request, Response, Status};
use yr_proto::internal::global_scheduler_service_client::GlobalSchedulerServiceClient;
use yr_proto::internal::global_scheduler_service_server::GlobalSchedulerService;
use yr_proto::internal::{
    EvictAgentRequest, EvictAgentResponse, QueryAgentCountRequest, QueryAgentCountResponse,
    QueryAgentsRequest, QueryAgentsResponse, RegisterRequest, RegisterResponse,
    UpdateResourcesRequest, UpdateResourcesResponse,
};

use crate::agent_manager::{AgentManager, AgentRecord};
use crate::config::Config;

pub struct GlobalSchedulerForward {
    master_uri: String,
    local_agents: Option<Arc<AgentManager>>,
}

impl GlobalSchedulerForward {
    pub fn new(config: &Config, local_agents: Option<Arc<AgentManager>>) -> Self {
        let addr = config.global_scheduler_address.trim();
        let uri = if addr.starts_with("http://") || addr.starts_with("https://") {
            addr.to_string()
        } else {
            format!("http://{addr}")
        };
        Self {
            master_uri: uri,
            local_agents,
        }
    }

    fn maybe_remember_function_agent(&self, req: &RegisterRequest) {
        let Some(local_agents) = &self.local_agents else {
            return;
        };
        let Ok(v) = serde_json::from_str::<serde_json::Value>(&req.agent_info_json) else {
            return;
        };
        if v.get("role").and_then(|r| r.as_str()) != Some("function_agent") {
            return;
        }
        let endpoint = req.address.trim();
        if endpoint.is_empty() {
            return;
        }
        let now_ms = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_millis() as i64)
            .unwrap_or_default();
        local_agents.upsert(AgentRecord {
            agent_id: req.node_id.clone(),
            pid: None,
            last_heartbeat_ms: now_ms,
            status: "registered".into(),
            grpc_endpoint: Some(endpoint.to_string()),
        });
    }

    async fn client(
        &self,
    ) -> Result<GlobalSchedulerServiceClient<tonic::transport::Channel>, Status> {
        GlobalSchedulerServiceClient::connect(self.master_uri.clone())
            .await
            .map_err(|e| Status::unavailable(format!("cannot reach global scheduler: {e}")))
    }
}

#[tonic::async_trait]
impl GlobalSchedulerService for GlobalSchedulerForward {
    async fn register(
        &self,
        request: Request<RegisterRequest>,
    ) -> Result<Response<RegisterResponse>, Status> {
        self.maybe_remember_function_agent(request.get_ref());
        self.client().await?.register(request).await
    }

    async fn update_resources(
        &self,
        request: Request<UpdateResourcesRequest>,
    ) -> Result<Response<UpdateResourcesResponse>, Status> {
        self.client().await?.update_resources(request).await
    }

    async fn query_agents(
        &self,
        request: Request<QueryAgentsRequest>,
    ) -> Result<Response<QueryAgentsResponse>, Status> {
        self.client().await?.query_agents(request).await
    }

    async fn query_agent_count(
        &self,
        request: Request<QueryAgentCountRequest>,
    ) -> Result<Response<QueryAgentCountResponse>, Status> {
        self.client().await?.query_agent_count(request).await
    }

    async fn evict_agent(
        &self,
        request: Request<EvictAgentRequest>,
    ) -> Result<Response<EvictAgentResponse>, Status> {
        self.client().await?.evict_agent(request).await
    }
}
