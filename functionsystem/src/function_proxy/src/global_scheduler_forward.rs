use std::sync::Arc;
use tonic::{Request, Response, Status};
use yr_proto::internal::global_scheduler_service_client::GlobalSchedulerServiceClient;
use yr_proto::internal::global_scheduler_service_server::GlobalSchedulerService;
use yr_proto::internal::{
    EvictAgentRequest, EvictAgentResponse, QueryAgentCountRequest, QueryAgentCountResponse,
    QueryAgentsRequest, QueryAgentsResponse, RegisterRequest, RegisterResponse,
    UpdateResourcesRequest, UpdateResourcesResponse,
};

use crate::config::Config;

pub struct GlobalSchedulerForward {
    master_uri: String,
}

impl GlobalSchedulerForward {
    pub fn new(config: &Config) -> Self {
        let addr = config.global_scheduler_address.trim();
        let uri = if addr.starts_with("http://") || addr.starts_with("https://") {
            addr.to_string()
        } else {
            format!("http://{addr}")
        };
        Self { master_uri: uri }
    }

    async fn client(&self) -> Result<GlobalSchedulerServiceClient<tonic::transport::Channel>, Status> {
        GlobalSchedulerServiceClient::connect(self.master_uri.clone())
            .await
            .map_err(|e| Status::unavailable(format!("cannot reach global scheduler: {e}")))
    }
}

#[tonic::async_trait]
impl GlobalSchedulerService for GlobalSchedulerForward {
    async fn register(&self, request: Request<RegisterRequest>) -> Result<Response<RegisterResponse>, Status> {
        self.client().await?.register(request).await
    }

    async fn update_resources(&self, request: Request<UpdateResourcesRequest>) -> Result<Response<UpdateResourcesResponse>, Status> {
        self.client().await?.update_resources(request).await
    }

    async fn query_agents(&self, request: Request<QueryAgentsRequest>) -> Result<Response<QueryAgentsResponse>, Status> {
        self.client().await?.query_agents(request).await
    }

    async fn query_agent_count(&self, request: Request<QueryAgentCountRequest>) -> Result<Response<QueryAgentCountResponse>, Status> {
        self.client().await?.query_agent_count(request).await
    }

    async fn evict_agent(&self, request: Request<EvictAgentRequest>) -> Result<Response<EvictAgentResponse>, Status> {
        self.client().await?.evict_agent(request).await
    }
}
