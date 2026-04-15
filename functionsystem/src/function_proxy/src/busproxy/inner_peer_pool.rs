//! Pooled [`InnerServiceClient`] connections to peer proxies (C++ `runtime_pool` / connector style fan-in).

use std::collections::HashMap;
use tokio::sync::Mutex;
use yr_proto::inner_service::inner_service_client::InnerServiceClient;

pub struct InnerServicePeerPool {
    clients: Mutex<HashMap<String, InnerServiceClient<tonic::transport::Channel>>>,
}

impl InnerServicePeerPool {
    pub fn new() -> Self {
        Self {
            clients: Mutex::new(HashMap::new()),
        }
    }

    pub async fn get_or_connect(
        &self,
        endpoint: &str,
    ) -> Result<InnerServiceClient<tonic::transport::Channel>, tonic::Status> {
        let mut g = self.clients.lock().await;
        if let Some(c) = g.get(endpoint) {
            return Ok(c.clone());
        }
        let c = InnerServiceClient::connect(endpoint.to_string())
            .await
            .map_err(|e| tonic::Status::unavailable(e.to_string()))?;
        g.insert(endpoint.to_string(), c.clone());
        Ok(c)
    }
}
