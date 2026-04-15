use crate::config::Config;
use std::sync::Arc;
use std::time::Duration;
use tracing::{debug, warn};
use yr_proto::internal::function_agent_service_client::FunctionAgentServiceClient;
use yr_proto::internal::{
    UpdateInstanceStatusRequest, UpdateInstanceStatusResponse, UpdateResourcesRequest,
    UpdateResourcesResponse,
};

/// gRPC client to the function agent with simple retry for status acknowledgements.
#[derive(Clone)]
pub struct AgentClient {
    endpoint: String,
    node_id: String,
}

impl AgentClient {
    pub fn new(cfg: &Arc<Config>) -> anyhow::Result<Self> {
        Ok(Self {
            endpoint: cfg.agent_uri()?,
            node_id: cfg.node_id.clone(),
        })
    }

    pub async fn update_instance_status(
        &self,
        req: UpdateInstanceStatusRequest,
    ) -> anyhow::Result<UpdateInstanceStatusResponse> {
        let mut client = FunctionAgentServiceClient::connect(self.endpoint.clone()).await?;
        let resp = client.update_instance_status(req).await?.into_inner();
        Ok(resp)
    }

    pub async fn update_instance_status_retry(&self, req: UpdateInstanceStatusRequest) {
        const MAX_ATTEMPTS: u32 = 12;
        let mut delay = Duration::from_millis(200);
        for attempt in 0..MAX_ATTEMPTS {
            match self.update_instance_status(req.clone()).await {
                Ok(r) if r.acknowledged => return,
                Ok(r) => {
                    debug!(attempt, acknowledged = r.acknowledged, "agent status not acked");
                }
                Err(e) => {
                    warn!(attempt, error = %e, "UpdateInstanceStatus RPC failed");
                }
            }
            tokio::time::sleep(delay).await;
            delay = (delay * 2).min(Duration::from_secs(5));
        }
        warn!(
            instance_id = %req.instance_id,
            runtime_id = %req.runtime_id,
            "UpdateInstanceStatus retries exhausted"
        );
    }

    pub async fn update_resources(&self, resource_json: String) -> anyhow::Result<UpdateResourcesResponse> {
        let mut client = FunctionAgentServiceClient::connect(self.endpoint.clone()).await?;
        let resp = client
            .update_resources(UpdateResourcesRequest {
                node_id: self.node_id.clone(),
                resource_json,
            })
            .await?
            .into_inner();
        Ok(resp)
    }

    pub async fn update_resources_retry(&self, resource_json: String) {
        const MAX_ATTEMPTS: u32 = 5;
        let mut delay = Duration::from_millis(300);
        for attempt in 0..MAX_ATTEMPTS {
            match self.update_resources(resource_json.clone()).await {
                Ok(r) if r.success => return,
                Ok(r) => {
                    warn!(attempt, success = r.success, "UpdateResources not successful");
                }
                Err(e) => {
                    warn!(attempt, error = %e, "UpdateResources RPC failed");
                }
            }
            tokio::time::sleep(delay).await;
            delay = (delay * 2).min(Duration::from_secs(8));
        }
    }
}
