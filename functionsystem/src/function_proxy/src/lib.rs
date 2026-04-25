//! Local function proxy (worker-side scheduler and bus/runtime/exec surfaces).

pub const DATA_CLIENT_CRATE: &str = yr_data_client_lite::CRATE_ID;

pub mod agent_manager;
pub mod busproxy;
pub mod config;
pub mod function_meta;
pub mod global_scheduler_forward;
pub mod grpc_services;
pub mod http_api;
pub mod instance_ctrl;
pub mod instance_manager;
pub mod instance_recover;
pub mod local_scheduler;
pub mod observer;
pub mod posix_client;
pub mod registration;
pub mod resource_reporter;
pub mod resource_view;
pub mod schedule_reporter;
pub mod state_machine;

pub use config::Config;

use agent_manager::AgentManager;
use busproxy::BusProxyCoordinator;
use instance_ctrl::InstanceController;
use instance_manager::InstanceManager;
use parking_lot::RwLock;
use resource_view::ResourceView;
use std::sync::Arc;
use yr_metastore_client::MetaStoreClient;

/// Shared handles wired through gRPC services, scheduler, and HTTP.
pub struct AppContext {
    pub config: Arc<Config>,
    pub resource_view: Arc<ResourceView>,
    pub agent_manager: Arc<AgentManager>,
    pub instance_ctrl: Arc<InstanceController>,
    pub instance_manager: Arc<InstanceManager>,
    pub bus: Arc<BusProxyCoordinator>,
    pub etcd: Option<Arc<tokio::sync::Mutex<MetaStoreClient>>>,
    /// Effective domain scheduler address (CLI or last register response).
    pub domain_addr: Arc<RwLock<String>>,
    /// Parsed global scheduler topology JSON (leader / members), when provided.
    pub topology: Arc<RwLock<Option<serde_json::Value>>>,
    /// Flipped to `true` after initial etcd reconciliation (routes + peers + function meta)
    /// completes. gRPC handlers must wait on this before processing SDK/driver traffic —
    /// mirrors C++ LiteBus behavior where the control plane fully initializes before serving.
    pub ready: Arc<tokio::sync::Notify>,
    pub ready_flag: Arc<std::sync::atomic::AtomicBool>,
}

impl AppContext {
    /// Block until the initial etcd sync is done. Returns immediately if already ready.
    pub async fn wait_ready(&self) {
        if self.ready_flag.load(std::sync::atomic::Ordering::Acquire) {
            return;
        }
        self.ready.notified().await;
    }

    pub fn is_ready(&self) -> bool {
        self.ready_flag.load(std::sync::atomic::Ordering::Acquire)
    }

    pub fn mark_ready(&self) {
        self.ready_flag
            .store(true, std::sync::atomic::Ordering::Release);
        self.ready.notify_waiters();
    }
}
