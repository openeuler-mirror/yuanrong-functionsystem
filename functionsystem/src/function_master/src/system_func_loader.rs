//! System function bootstrap / keepalive (`system_function_loader/*.cpp` analogue).

use std::sync::Arc;

use tracing::warn;

use crate::config::MasterConfig;

/// Loads system function definitions and keeps instances present (stub hooks for file/MetaStore sources).
pub struct SystemFunctionLoader {
    #[allow(dead_code)]
    config: Arc<MasterConfig>,
}

impl SystemFunctionLoader {
    pub fn new(config: Arc<MasterConfig>) -> Self {
        Self { config }
    }

    /// Periodic check: instance exists; if missing, build `ScheduleRequest` and reschedule (wired later).
    pub async fn keepalive_tick(&self) {
        warn!("system_func_loader: keepalive tick NOT IMPLEMENTED");
    }

    pub async fn load_from_files(&self) {
        warn!("system_func_loader: load_from_files NOT IMPLEMENTED");
    }
}
