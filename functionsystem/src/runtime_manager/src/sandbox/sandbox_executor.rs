//! CONTAINER executor — port of C++ `SandboxExecutor`. M3 implements the
//! `start_normal` (cold-start) path; WarmUp/Restore (M4) and lifecycle (M6) follow.

use std::sync::Arc;

use anyhow::{anyhow, Result};
use tracing::{info, warn};

use super::launcher_client::LauncherClient;
use super::request_builder::{
    build_start_request, encode_port_mapping, PortForward, SandboxStartParams,
};
use super::runtime_state_manager::{RuntimeStateManager, SandboxInfo};
use super::start_guard::SandboxStartGuard;
use crate::port_manager::SharedPortManager;

/// Owns the sandbox state + launcher client + host-port allocator.
pub struct SandboxExecutor {
    state: Arc<RuntimeStateManager>,
    launcher: LauncherClient,
    ports: Arc<SharedPortManager>,
}

/// Result of a successful sandbox start.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SandboxStarted {
    pub sandbox_id: String,
    pub port_mappings_json: String,
}

impl SandboxExecutor {
    pub fn new(
        state: Arc<RuntimeStateManager>,
        launcher: LauncherClient,
        ports: Arc<SharedPortManager>,
    ) -> Self {
        Self {
            state,
            launcher,
            ports,
        }
    }

    pub fn state(&self) -> &Arc<RuntimeStateManager> {
        &self.state
    }

    fn release_ports(&self, keys: &[String]) {
        for k in keys {
            self.ports.release(k);
        }
    }

    /// Standard container cold start (C++ `StartNormal`): allocate host ports for
    /// each requested forward, build the `StartRequest`, call the launcher, and
    /// register the sandbox on success. On any failure the [`SandboxStartGuard`]
    /// rolls back state and allocated ports are released.
    pub async fn start_normal(
        &self,
        mut params: SandboxStartParams,
        forwards: Vec<PortForward>,
    ) -> Result<SandboxStarted> {
        let rid = params.runtime_id.clone();
        if rid.is_empty() {
            return Err(anyhow!("start_normal: empty runtime_id"));
        }
        let guard = SandboxStartGuard::begin(self.state.clone(), &rid);

        // Allocate one host port per forward (key = runtime_id#index for multi-port).
        let mut keys: Vec<String> = Vec::new();
        let mut mappings: Vec<String> = Vec::new();
        for (i, f) in forwards.iter().enumerate() {
            let key = format!("{rid}#{i}");
            match self.ports.allocate(&key) {
                Ok(host) => {
                    keys.push(key);
                    mappings.push(encode_port_mapping(&f.protocol, host, f.container_port));
                }
                Err(e) => {
                    self.release_ports(&keys);
                    return Err(anyhow!("allocate host port: {e}")); // guard drops -> state rollback
                }
            }
        }
        params.port_mappings = mappings.clone();

        let req = build_start_request(&params);
        match self.launcher.start(req).await {
            Ok(resp) if resp.code == 0 => {
                let port_json = serde_json::to_string(&mappings).unwrap_or_default();
                self.state.register(SandboxInfo {
                    runtime_id: rid.clone(),
                    sandbox_id: resp.id.clone(),
                    port_mappings_json: port_json.clone(),
                    ..Default::default()
                });
                guard.commit();
                info!(runtime_id = %rid, sandbox_id = %resp.id, ports = mappings.len(), "sandbox start_normal ok");
                Ok(SandboxStarted {
                    sandbox_id: resp.id,
                    port_mappings_json: port_json,
                })
            }
            Ok(resp) => {
                self.release_ports(&keys);
                warn!(runtime_id = %rid, code = resp.code, msg = %resp.message, "sandbox launcher start rejected");
                Err(anyhow!(
                    "launcher start failed: code={} {}",
                    resp.code,
                    resp.message
                ))
            }
            Err(e) => {
                self.release_ports(&keys);
                Err(e)
            }
        }
    }
}
