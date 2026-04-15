use crate::error::{DsError, DsResult};
use async_trait::async_trait;
use tracing::debug;

/// Minimal KV client matching the datasystem::KVClient interface
/// used by function_proxy's DSCacheClientImpl.
#[async_trait]
pub trait KvClient: Send + Sync {
    async fn set(&self, key: &str, value: &[u8]) -> DsResult<()>;
    async fn get(&self, key: &str) -> DsResult<Option<Vec<u8>>>;
    async fn get_batch(&self, keys: &[String]) -> DsResult<Vec<Option<Vec<u8>>>>;
    async fn del(&self, key: &str) -> DsResult<bool>;
    async fn del_batch(&self, keys: &[String]) -> DsResult<Vec<bool>>;
}

#[derive(Clone)]
pub struct ConnectOptions {
    pub host: String,
    pub port: u16,
    pub access_key: Option<String>,
    pub secret_key: Option<String>,
}

pub struct HttpKvClient {
    client: reqwest::Client,
    base_url: String,
}

impl HttpKvClient {
    pub fn new(opts: &ConnectOptions) -> Self {
        let base_url = format!("http://{}:{}/kv/v1", opts.host, opts.port);
        debug!(base_url = %base_url, "creating DS KV client");
        Self {
            client: reqwest::Client::new(),
            base_url,
        }
    }
}

#[async_trait]
impl KvClient for HttpKvClient {
    async fn set(&self, key: &str, value: &[u8]) -> DsResult<()> {
        let url = format!("{}/set", self.base_url);
        let body = serde_json::json!({
            "key": key,
            "value": base64_encode(value),
        });
        self.client
            .post(&url)
            .json(&body)
            .send()
            .await?
            .error_for_status()
            .map_err(|e| DsError::Operation(e.to_string()))?;
        Ok(())
    }

    async fn get(&self, key: &str) -> DsResult<Option<Vec<u8>>> {
        let url = format!("{}/get/{}", self.base_url, urlencoded(key));
        let resp = self.client.get(&url).send().await?;
        if resp.status() == reqwest::StatusCode::NOT_FOUND {
            return Ok(None);
        }
        let resp = resp
            .error_for_status()
            .map_err(|e| DsError::Operation(e.to_string()))?;
        let body: serde_json::Value = resp.json().await?;
        if let Some(val) = body.get("value").and_then(|v| v.as_str()) {
            Ok(Some(base64_decode(val)?))
        } else {
            Ok(None)
        }
    }

    async fn get_batch(&self, keys: &[String]) -> DsResult<Vec<Option<Vec<u8>>>> {
        let mut results = Vec::with_capacity(keys.len());
        for key in keys {
            results.push(self.get(key).await?);
        }
        Ok(results)
    }

    async fn del(&self, key: &str) -> DsResult<bool> {
        let url = format!("{}/del/{}", self.base_url, urlencoded(key));
        let resp = self.client.delete(&url).send().await?;
        Ok(resp.status().is_success())
    }

    async fn del_batch(&self, keys: &[String]) -> DsResult<Vec<bool>> {
        let mut results = Vec::with_capacity(keys.len());
        for key in keys {
            results.push(self.del(key).await?);
        }
        Ok(results)
    }
}

fn base64_encode(data: &[u8]) -> String {
    use base64::Engine;
    base64::engine::general_purpose::STANDARD.encode(data)
}

fn base64_decode(s: &str) -> DsResult<Vec<u8>> {
    use base64::Engine;
    base64::engine::general_purpose::STANDARD
        .decode(s)
        .map_err(|e| DsError::Serialization(e.to_string()))
}

fn urlencoded(s: &str) -> String {
    s.replace('/', "%2F")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn base64_round_trip() {
        let raw: Vec<u8> = (0u8..=255).collect();
        let enc = base64_encode(&raw);
        let dec = base64_decode(&enc).expect("decode");
        assert_eq!(dec, raw);
    }

    #[test]
    fn urlencoded_slash() {
        assert_eq!(urlencoded("a/b/c"), "a%2Fb%2Fc");
        assert_eq!(urlencoded("plain"), "plain");
    }
}
