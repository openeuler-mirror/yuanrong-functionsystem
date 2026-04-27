use std::sync::Arc;

use async_trait::async_trait;
use tokio::sync::Mutex;
use yr_metastore_client::MetaStoreClient;

const STATE_PREFIX: &str = "/yr/state";

#[async_trait]
pub trait StateStore: Send + Sync {
    async fn set_state(&self, checkpoint_id: &str, state: &[u8]) -> Result<(), String>;
    async fn get_state(&self, checkpoint_id: &str) -> Result<Option<Vec<u8>>, String>;
    async fn delete_state(&self, checkpoint_id: &str) -> Result<(), String>;
}

pub struct MetaStoreStateStore {
    client: Arc<Mutex<MetaStoreClient>>,
}

impl MetaStoreStateStore {
    pub fn new(client: Arc<Mutex<MetaStoreClient>>) -> Self {
        Self { client }
    }

    fn key(checkpoint_id: &str) -> String {
        format!("{}/{}", STATE_PREFIX, checkpoint_id)
    }
}

#[async_trait]
impl StateStore for MetaStoreStateStore {
    async fn set_state(&self, checkpoint_id: &str, state: &[u8]) -> Result<(), String> {
        let key = Self::key(checkpoint_id);
        let mut client = self.client.lock().await;
        client
            .put(&key, state)
            .await
            .map(|_| ())
            .map_err(|e| e.to_string())
    }

    async fn get_state(&self, checkpoint_id: &str) -> Result<Option<Vec<u8>>, String> {
        let key = Self::key(checkpoint_id);
        let mut client = self.client.lock().await;
        let rsp = client.get(&key).await.map_err(|e| e.to_string())?;
        Ok(rsp.kvs.into_iter().next().map(|kv| kv.value))
    }

    async fn delete_state(&self, checkpoint_id: &str) -> Result<(), String> {
        let key = Self::key(checkpoint_id);
        let mut client = self.client.lock().await;
        client
            .delete(&key)
            .await
            .map(|_| ())
            .map_err(|e| e.to_string())
    }
}
