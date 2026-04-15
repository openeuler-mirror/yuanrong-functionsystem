use crate::error::{DsError, DsResult};
use async_trait::async_trait;
use serde::{Deserialize, Serialize};
use tracing::debug;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ObjMetaInfo {
    pub obj_id: String,
    pub size: u64,
    pub locations: Vec<String>,
}

#[async_trait]
pub trait ObjectClient: Send + Sync {
    async fn get_obj_meta_info(
        &self,
        obj_id: &str,
        tenant_id: Option<&str>,
    ) -> DsResult<ObjMetaInfo>;
    async fn health_check(&self) -> DsResult<bool>;
}

pub struct HttpObjectClient {
    client: reqwest::Client,
    base_url: String,
}

impl HttpObjectClient {
    pub fn new(host: &str, port: u16) -> Self {
        let base_url = format!("http://{}:{}/object/v1", host, port);
        debug!(base_url = %base_url, "creating DS Object client");
        Self {
            client: reqwest::Client::new(),
            base_url,
        }
    }
}

#[async_trait]
impl ObjectClient for HttpObjectClient {
    async fn get_obj_meta_info(
        &self,
        obj_id: &str,
        tenant_id: Option<&str>,
    ) -> DsResult<ObjMetaInfo> {
        let mut url = format!("{}/meta/{}", self.base_url, obj_id);
        if let Some(tid) = tenant_id {
            url.push_str(&format!("?tenant_id={}", tid));
        }
        let resp = self
            .client
            .get(&url)
            .send()
            .await?
            .error_for_status()
            .map_err(|e| DsError::Operation(e.to_string()))?;
        let info: ObjMetaInfo = resp
            .json()
            .await
            .map_err(|e| DsError::Serialization(e.to_string()))?;
        Ok(info)
    }

    async fn health_check(&self) -> DsResult<bool> {
        let url = format!("{}/health", self.base_url);
        match self.client.get(&url).send().await {
            Ok(resp) => Ok(resp.status().is_success()),
            Err(_) => Ok(false),
        }
    }
}
