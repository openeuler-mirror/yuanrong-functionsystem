use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use tokio::sync::Mutex;
use yr_metastore_client::MetaStoreClient;

use crate::config::{ElectionMode, IamConfig};
use crate::token_store::TokenStore;

pub struct AppState {
    pub config: IamConfig,
    /// Present when IAM persistence or etcd election requires etcd.
    pub metastore: Option<Arc<Mutex<MetaStoreClient>>>,
    /// When false, mutating handlers return 503 (etcd/k8s election follower).
    pub is_leader: Arc<AtomicBool>,
    pub token_store: Arc<TokenStore>,
}

impl AppState {
    pub fn new(config: IamConfig, metastore: Option<MetaStoreClient>) -> Self {
        let is_leader = Arc::new(AtomicBool::new(matches!(
            config.election_mode,
            ElectionMode::Standalone
        )));
        Self {
            config,
            metastore: metastore.map(|c| Arc::new(Mutex::new(c))),
            is_leader,
            token_store: Arc::new(TokenStore::new()),
        }
    }

    pub fn require_leader(&self) -> bool {
        self.is_leader.load(Ordering::SeqCst)
    }

    pub fn set_leader(&self, v: bool) {
        self.is_leader.store(v, Ordering::SeqCst);
    }
}
