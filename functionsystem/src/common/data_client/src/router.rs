use crate::error::{DsError, DsResult};
use async_trait::async_trait;
use tracing::debug;

#[async_trait]
pub trait RouterClient: Send + Sync {
    async fn get_worker_addr_by_worker_id(&self, worker_id: &str) -> DsResult<String>;
}

pub struct HttpRouterClient {
    client: reqwest::Client,
    base_url: String,
}

impl HttpRouterClient {
    pub fn new(host: &str, port: u16) -> Self {
        let base_url = format!("http://{}:{}/router/v1", host, port);
        debug!(base_url = %base_url, "creating DS Router client");
        Self {
            client: reqwest::Client::new(),
            base_url,
        }
    }
}

#[async_trait]
impl RouterClient for HttpRouterClient {
    async fn get_worker_addr_by_worker_id(&self, worker_id: &str) -> DsResult<String> {
        let url = format!("{}/worker/{}", self.base_url, worker_id);
        let resp = self
            .client
            .get(&url)
            .send()
            .await?
            .error_for_status()
            .map_err(|e| DsError::Operation(e.to_string()))?;
        let body: serde_json::Value = resp
            .json()
            .await
            .map_err(|e| DsError::Serialization(e.to_string()))?;
        body.get("address")
            .and_then(|v| v.as_str())
            .map(|s| s.to_string())
            .ok_or_else(|| DsError::NotFound(format!("worker {} not found", worker_id)))
    }
}
