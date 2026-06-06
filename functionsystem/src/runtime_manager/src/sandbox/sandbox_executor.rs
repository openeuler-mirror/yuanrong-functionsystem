//! CONTAINER executor — port of C++ `SandboxExecutor`. Implements the three start
//! paths: `start_normal` (cold start, M3), `start_warmup` + `start_by_snapshot`
//! (M4). Lifecycle (delete/stats) follows in M6.

use std::sync::Arc;

use anyhow::{anyhow, Result};
use tracing::{info, warn};

use super::checkpoint_orchestrator::CheckpointOrchestrator;
use super::launcher_client::LauncherClient;
use super::request_builder::{build_start_request, encode_port_mapping, PortForward, SandboxStartParams};
use super::runtime_state_manager::{RuntimeStateManager, SandboxInfo};
use super::start_guard::SandboxStartGuard;
use crate::port_manager::SharedPortManager;
use yr_proto::runtime::v1::{DeleteRequest, FunctionRuntime, RegisterRequest, UnregisterRequest};

/// Owns the sandbox state + launcher client + host-port allocator (+ optional
/// checkpoint orchestrator for the Restore path).
pub struct SandboxExecutor {
    state: Arc<RuntimeStateManager>,
    launcher: LauncherClient,
    ports: Arc<SharedPortManager>,
    ckpt: Option<Arc<CheckpointOrchestrator>>,
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
            ckpt: None,
        }
    }

    pub fn with_checkpoint(mut self, ckpt: Arc<CheckpointOrchestrator>) -> Self {
        self.ckpt = Some(ckpt);
        self
    }

    pub fn state(&self) -> &Arc<RuntimeStateManager> {
        &self.state
    }

    fn release_ports(&self, keys: &[String]) {
        for k in keys {
            self.ports.release(k);
        }
    }

    /// Release every forward port keyed `runtime_id#*` (used on stop, where the
    /// per-forward count is not tracked separately).
    fn release_all_forward_ports(&self, runtime_id: &str) {
        let prefix = format!("{runtime_id}#");
        let keys: Vec<String> = self
            .ports
            .snapshot_allocations()
            .into_keys()
            .filter(|k| k.starts_with(&prefix))
            .collect();
        self.release_ports(&keys);
    }

    /// Allocate one host port per forward (key = `runtime_id#index`). Releases any
    /// already-allocated ports and errors if a later allocation fails.
    fn alloc_forward_ports(
        &self,
        runtime_id: &str,
        forwards: &[PortForward],
    ) -> Result<(Vec<String>, Vec<String>)> {
        let mut keys = Vec::new();
        let mut mappings = Vec::new();
        for (i, f) in forwards.iter().enumerate() {
            let key = format!("{runtime_id}#{i}");
            match self.ports.allocate(&key) {
                Ok(host) => {
                    keys.push(key);
                    mappings.push(encode_port_mapping(&f.protocol, host, f.container_port));
                }
                Err(e) => {
                    self.release_ports(&keys);
                    return Err(anyhow!("allocate host port: {e}"));
                }
            }
        }
        Ok((keys, mappings))
    }

    /// Build → launcher.start → register on success. On launcher rejection/transport
    /// error, releases `port_keys` and returns Err (the caller's `SandboxStartGuard`
    /// rolls back state; restore callers also release the checkpoint ref).
    async fn build_start_register(
        &self,
        params: &SandboxStartParams,
        port_keys: &[String],
        checkpoint_id: &str,
    ) -> Result<SandboxStarted> {
        let rid = &params.runtime_id;
        let req = build_start_request(params);
        match self.launcher.start(req).await {
            Ok(resp) if resp.code == 0 => {
                let port_json = serde_json::to_string(&params.port_mappings).unwrap_or_default();
                self.state.register(SandboxInfo {
                    runtime_id: rid.clone(),
                    sandbox_id: resp.id.clone(),
                    checkpoint_id: checkpoint_id.to_string(),
                    port_mappings_json: port_json.clone(),
                    ..Default::default()
                });
                info!(runtime_id = %rid, sandbox_id = %resp.id, "sandbox start ok");
                Ok(SandboxStarted {
                    sandbox_id: resp.id,
                    port_mappings_json: port_json,
                })
            }
            Ok(resp) => {
                self.release_ports(port_keys);
                warn!(runtime_id = %rid, code = resp.code, msg = %resp.message, "sandbox launcher rejected");
                Err(anyhow!("launcher start failed: code={} {}", resp.code, resp.message))
            }
            Err(e) => {
                self.release_ports(port_keys);
                Err(e)
            }
        }
    }

    /// Standard container cold start (C++ `StartNormal`).
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
        let (keys, mappings) = self.alloc_forward_ports(&rid, &forwards)?;
        params.port_mappings = mappings;
        let started = self.build_start_register(&params, &keys, "").await?;
        guard.commit();
        Ok(started)
    }

    /// WarmUp registration (C++ `StartWarmUp`): register the FunctionRuntime in the
    /// shim's warm pool — no container started, no ports allocated.
    pub async fn start_warmup(&self, func_runtime: FunctionRuntime) -> Result<()> {
        let rid = func_runtime.id.clone();
        if rid.is_empty() {
            return Err(anyhow!("start_warmup: empty runtime id"));
        }
        let resp = self
            .launcher
            .register(RegisterRequest {
                func_runtimes: vec![func_runtime.clone()],
            })
            .await?;
        if !resp.success {
            return Err(anyhow!("warmup register failed: {}", resp.message));
        }
        self.state.register_warm_up(&rid, func_runtime);
        info!(runtime_id = %rid, "sandbox warmup registered");
        Ok(())
    }

    /// Remove a warm-up registration (C++ `UnregisterWarmUp`).
    pub async fn stop_warmup(&self, runtime_id: &str) -> Result<()> {
        let resp = self
            .launcher
            .unregister(UnregisterRequest {
                ids: vec![runtime_id.to_string()],
            })
            .await?;
        self.state.unregister_warm_up(runtime_id);
        if !resp.success {
            return Err(anyhow!("warmup unregister failed: {}", resp.message));
        }
        Ok(())
    }

    /// Checkpoint-based start (C++ `StartBySnapshot`): download → add-ref → start
    /// with `ckpt_dir` → release-ref on any failure.
    pub async fn start_by_snapshot(
        &self,
        mut params: SandboxStartParams,
        checkpoint_id: &str,
        storage_url: &str,
        forwards: Vec<PortForward>,
    ) -> Result<SandboxStarted> {
        let rid = params.runtime_id.clone();
        if rid.is_empty() {
            return Err(anyhow!("start_by_snapshot: empty runtime_id"));
        }
        let ckpt = self
            .ckpt
            .as_ref()
            .ok_or_else(|| anyhow!("start_by_snapshot: no checkpoint orchestrator configured"))?;

        let guard = SandboxStartGuard::begin(self.state.clone(), &rid);

        let ckpt_dir = ckpt.download_for_restore(checkpoint_id, storage_url).await?;
        if let Err(e) = ckpt.add_ref(checkpoint_id, &rid).await {
            return Err(e); // guard drops -> state rollback; nothing referenced yet
        }

        // From here a failure must also release the checkpoint ref.
        let (keys, mappings) = match self.alloc_forward_ports(&rid, &forwards) {
            Ok(v) => v,
            Err(e) => {
                let _ = ckpt.release_ref(&rid).await;
                return Err(e);
            }
        };
        params.port_mappings = mappings;
        params.ckpt_dir = ckpt_dir;

        match self.build_start_register(&params, &keys, checkpoint_id).await {
            Ok(started) => {
                guard.commit();
                Ok(started)
            }
            Err(e) => {
                let _ = ckpt.release_ref(&rid).await;
                Err(e)
            }
        }
    }

    /// Stop & delete a sandbox (C++ `StopInstance` → `Delete`). WarmUp entries are
    /// unregistered from the pool; active sandboxes are deleted via the launcher,
    /// then host ports and any checkpoint ref are released and state is cleared.
    /// State teardown happens even if the launcher Delete errors (best-effort).
    pub async fn stop(&self, runtime_id: &str, timeout_secs: i64, _force: bool) -> Result<()> {
        if self.state.is_warm_up(runtime_id) {
            return self.stop_warmup(runtime_id).await;
        }
        let sandbox_id = self.state.get_sandbox_id(runtime_id);
        // Prefer the launcher's sandbox id; fall back to runtime_id if unset.
        let delete_id = if sandbox_id.is_empty() {
            runtime_id.to_string()
        } else {
            sandbox_id
        };
        let delete_res = self
            .launcher
            .delete(DeleteRequest {
                id: delete_id,
                timeout: timeout_secs,
            })
            .await;

        // Always release local resources + state, even if Delete failed.
        self.release_all_forward_ports(runtime_id);
        if let Some(ckpt) = self.ckpt.as_ref() {
            let _ = ckpt.release_ref(runtime_id).await;
        }
        self.state.unregister(runtime_id);

        match delete_res {
            Ok(_) => {
                info!(runtime_id, "sandbox stopped");
                Ok(())
            }
            Err(e) => {
                warn!(runtime_id, error = %e, "sandbox launcher delete failed; state cleaned anyway");
                Err(e)
            }
        }
    }
}
