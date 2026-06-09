use crate::agent_manager::AgentManager;
use crate::config::Config;
use crate::function_meta::FunctionMetaCache;
use crate::resource_view::ResourceView;
use crate::state_machine::{InstanceMetadata, InstanceState};
use dashmap::DashMap;
use prost::Message;
use std::collections::HashMap;
use std::path::Path;
use std::sync::Arc;
use tracing::{info, warn};
use yr_common::etcd_keys::{gen_func_meta_key, INSTANCE_PATH_PREFIX};
use yr_common::service_json::{validate_service_infos, ServiceInfo};
use yr_common::types::{dr_mode_skips_persistence, need_persistence_state};
use yr_proto::internal::function_agent_service_client::FunctionAgentServiceClient;
use yr_proto::internal::{StartInstanceRequest, StopInstanceRequest};
use yr_runtime_manager::runtime_ops::{start_instance_op, stop_instance_op};
use yr_runtime_manager::sandbox::{parse_forward_ports, SandboxConfig};
use yr_runtime_manager::state::RuntimeManagerState;

#[derive(Debug, Clone, Default)]
struct ServiceFunctionMeta {
    runtime_type: String,
    code_path: String,
    env: HashMap<String, String>,
    /// CONTAINER backend marker: when the function's services.yaml entry carries a
    /// `rootfs` block, the C++ runtime_manager runs it as a runc container. We carry
    /// the whole spec so `start_instance` can emit the internal sandbox config_json.
    rootfs: yr_common::metadata::RootfsSpecMeta,
    bootstrap: yr_common::metadata::BootstrapMetaData,
}

fn function_name_candidates(function_name: &str) -> Vec<String> {
    let without_version = function_name
        .split('/')
        .rev()
        .find(|part| !part.starts_with('$'))
        .unwrap_or(function_name);
    let mut out = vec![function_name.to_string(), without_version.to_string()];
    if let Some((_, suffix)) = without_version.rsplit_once('-') {
        out.push(suffix.to_string());
    }
    out.sort();
    out.dedup();
    out
}

fn job_id_from_trace(trace_id: &str) -> Option<String> {
    trace_id
        .split("-trace")
        .next()
        .filter(|s| s.starts_with("job-") && s.len() > "job-".len())
        .map(str::to_string)
}

fn custom_envs_from_create_args(args: &[yr_proto::common::Arg]) -> HashMap<String, String> {
    let Some(first) = args.first() else {
        return HashMap::new();
    };
    let Ok(meta) = yr_proto::resources::MetaData::decode(first.value.as_slice()) else {
        return HashMap::new();
    };
    meta.config.map(|cfg| cfg.custom_envs).unwrap_or_default()
}

fn delegate_envs_from_create_options(
    create_options: &HashMap<String, String>,
) -> HashMap<String, String> {
    let Some(raw) = create_options.get("DELEGATE_ENV_VAR") else {
        return HashMap::new();
    };
    let Ok(v) = serde_json::from_str::<serde_json::Value>(raw) else {
        return HashMap::new();
    };
    let Some(map) = v.as_object() else {
        return HashMap::new();
    };
    map.iter()
        .filter_map(|(k, v)| Some((k.clone(), v.as_str()?.to_string())))
        .collect()
}

/// When the function's services.yaml entry carries a `rootfs` block, the C++
/// runtime_manager runs it as a runc container (CONTAINER backend, selected
/// per-function — see [[yr-rust-rewrite-parity-state]] ground-truth). Serialize the
/// internal sandbox `config_json` the runtime_manager decodes. Returns None for
/// process-mode functions (no rootfs), leaving the unchanged RUNTIME path.
fn sandbox_config_json(
    meta: &ServiceFunctionMeta,
    create_options: &HashMap<String, String>,
) -> Option<String> {
    use yr_common::metadata::RootfsSrcType;
    let rootfs = &meta.rootfs;
    if rootfs.image_url.trim().is_empty() && rootfs.path.trim().is_empty() {
        return None; // process-mode function
    }
    let (image, rootfs_type) = match rootfs.r#type {
        RootfsSrcType::Local => (rootfs.path.clone(), "local".to_string()),
        RootfsSrcType::S3 => (rootfs.image_url.clone(), "s3".to_string()),
        _ => (rootfs.image_url.clone(), "image".to_string()),
    };
    // Per-request port forwards from CreateOpt["network"] (portForwardings JSON),
    // re-encoded as "proto:port" for the runtime_manager's parse_port_spec.
    let ports = create_options
        .get("network")
        .map(|j| parse_forward_ports(j))
        .unwrap_or_default()
        .into_iter()
        .map(|f| format!("{}:{}", f.protocol, f.container_port))
        .collect::<Vec<_>>();
    // Command = services.yaml bootstrap.entrypoint, whitespace-split (the launcher
    // contract). runtime_manager appends --rt_server_address/--deploy_dir/
    // --runtime_id/--job_id/--log_level (C++ python_strategy BuildArgs parity).
    let command = meta
        .bootstrap
        .entrypoint
        .split_whitespace()
        .map(str::to_string)
        .collect::<Vec<_>>();
    let cfg = SandboxConfig {
        sandbox: true,
        image,
        rootfs_type,
        sandbox_runtime: rootfs.runtime.clone(),
        readonly: rootfs.readonly,
        mountpoint: rootfs.mountpoint.clone(),
        ports,
        command,
        ..Default::default()
    };
    serde_json::to_string(&cfg).ok()
}

/// Owns in-memory instance metadata, optional etcd persistence, and instance start/stop dispatch.
pub struct InstanceController {
    config: Arc<Config>,
    resource_view: Arc<ResourceView>,
    instances: Arc<DashMap<String, InstanceMetadata>>,
    etcd: Option<Arc<tokio::sync::Mutex<yr_metastore_client::MetaStoreClient>>>,
    /// Simple per-tenant throttle for schedule/create storms (`until_ms` wall clock).
    tenant_cooldown_until_ms: DashMap<String, i64>,
    /// In-process runtime manager state when `enable_merge_process` (C++ `function_proxy` parity).
    embedded_rm: Option<Arc<RuntimeManagerState>>,
    /// Local function_agent registry populated from C++-compatible Register RPCs.
    agent_manager: Option<Arc<AgentManager>>,
    /// Keys present under function-meta etcd prefix (`tenant/func/version`).
    function_meta: Arc<FunctionMetaCache>,
}

impl InstanceController {
    pub fn new(
        config: Arc<Config>,
        resource_view: Arc<ResourceView>,
        etcd: Option<Arc<tokio::sync::Mutex<yr_metastore_client::MetaStoreClient>>>,
        embedded_rm: Option<Arc<RuntimeManagerState>>,
    ) -> Arc<Self> {
        Arc::new(Self {
            config,
            resource_view,
            instances: Arc::new(DashMap::new()),
            etcd,
            tenant_cooldown_until_ms: DashMap::new(),
            embedded_rm,
            agent_manager: None,
            function_meta: Arc::new(FunctionMetaCache::new()),
        })
    }

    pub fn new_with_agent_manager(
        config: Arc<Config>,
        resource_view: Arc<ResourceView>,
        etcd: Option<Arc<tokio::sync::Mutex<yr_metastore_client::MetaStoreClient>>>,
        embedded_rm: Option<Arc<RuntimeManagerState>>,
        agent_manager: Arc<AgentManager>,
    ) -> Arc<Self> {
        Arc::new(Self {
            config,
            resource_view,
            instances: Arc::new(DashMap::new()),
            etcd,
            tenant_cooldown_until_ms: DashMap::new(),
            embedded_rm,
            agent_manager: Some(agent_manager),
            function_meta: Arc::new(FunctionMetaCache::new()),
        })
    }

    pub fn metastore(
        &self,
    ) -> Option<Arc<tokio::sync::Mutex<yr_metastore_client::MetaStoreClient>>> {
        self.etcd.clone()
    }

    pub fn function_meta_cache(&self) -> &Arc<FunctionMetaCache> {
        &self.function_meta
    }

    pub fn service_code_path_for(&self, function_name: &str) -> Option<String> {
        self.service_function_meta(function_name)
            .and_then(|m| (!m.code_path.trim().is_empty()).then_some(m.code_path))
    }

    fn service_function_meta(&self, function_name: &str) -> Option<ServiceFunctionMeta> {
        let path = self.config.cpp_ignored.services_path.trim();
        if path.is_empty() || !Path::new(path).is_file() {
            return None;
        }
        let text = std::fs::read_to_string(path).ok()?;
        let services: Vec<ServiceInfo> = match serde_yaml::from_str(&text) {
            Ok(v) => v,
            Err(e) => {
                warn!(error = %e, %path, "failed to parse services metadata");
                return None;
            }
        };
        if let Err(e) = validate_service_infos(&services) {
            warn!(error = %e, %path, "invalid services metadata");
            return None;
        }
        let candidates = function_name_candidates(function_name);
        for service in services {
            for (name, spec) in service.functions {
                if !candidates.iter().any(|c| c == &name) {
                    continue;
                }
                return Some(ServiceFunctionMeta {
                    runtime_type: spec.runtime,
                    code_path: spec.code_path,
                    env: spec.environment,
                    rootfs: spec.rootfs,
                    bootstrap: spec.bootstrap,
                });
            }
        }
        None
    }

    /// Resolve function metadata before local placement (etcd catalog).
    pub async fn schedule_get_func_meta(
        &self,
        tenant: &str,
        function_name: &str,
        version: &str,
    ) -> Result<(), tonic::Status> {
        if !self.config.require_function_meta {
            return Ok(());
        }
        let func_key = format!("{}/{}/{}", tenant, function_name, version);
        if self.function_meta.contains(&func_key) {
            return Ok(());
        }
        let Some(store) = &self.etcd else {
            return Err(tonic::Status::failed_precondition(
                "require_function_meta set but etcd is disabled",
            ));
        };
        let Some(etcd_key) = gen_func_meta_key(&func_key) else {
            return Err(tonic::Status::invalid_argument(
                "function name / tenant cannot be turned into a metadata key",
            ));
        };
        let mut c = store.lock().await;
        let res = c
            .get(&etcd_key)
            .await
            .map_err(|e| tonic::Status::internal(e.to_string()))?;
        if res.kvs.is_empty() {
            return Err(tonic::Status::not_found(format!(
                "function metadata not found: {func_key}"
            )));
        }
        drop(c);
        self.function_meta.insert_key(func_key);
        Ok(())
    }

    /// Hook: authorization before create (IAM / policy). Pass-through for E2E.
    pub async fn schedule_do_authorize_create(
        &self,
        _tenant: &str,
        _function_name: &str,
    ) -> Result<(), tonic::Status> {
        Ok(())
    }

    pub fn tenant_cooldown_active(&self, tenant: &str) -> bool {
        let now = InstanceMetadata::now_ms();
        self.tenant_cooldown_until_ms
            .get(tenant)
            .map(|t| *t > now)
            .unwrap_or(false)
    }

    pub fn set_tenant_cooldown_ms(&self, tenant: &str, cooldown_ms: i64) {
        let until = InstanceMetadata::now_ms() + cooldown_ms;
        self.tenant_cooldown_until_ms
            .insert(tenant.to_string(), until);
    }

    /// Apply a state change with optional etcd KV `version` check (optimistic concurrency).
    pub async fn transition_with_version(
        &self,
        instance_id: &str,
        next: InstanceState,
        expected_kv_version: Option<i64>,
    ) -> Result<InstanceMetadata, tonic::Status> {
        let updated = if let Some(mut m) = self.instances.get_mut(instance_id) {
            if let Some(exp) = expected_kv_version {
                if m.etcd_kv_version != Some(exp) {
                    return Err(tonic::Status::aborted("instance etcd_kv_version mismatch"));
                }
            }
            m.transition(next)
                .map_err(|_| tonic::Status::failed_precondition("invalid state transition"))?;
            Some(m.clone())
        } else {
            None
        };
        let meta = updated.ok_or_else(|| tonic::Status::not_found("instance not found"))?;
        self.persist_if_policy(&meta).await;
        Ok(meta)
    }

    /// Persist when the new state is one that must be durable per `yr_common::types` (plus running/creating for local ops).
    pub async fn persist_if_policy(&self, meta: &InstanceMetadata) {
        if self.etcd.is_none() {
            return;
        }
        let s = meta.state;
        // DR mode (gap2): single-write persistence at RUNNING. In direct-routing
        // mode SCHEDULING/CREATING are not yet route-bearing, so skip their etcd
        // writes; the single durable write happens at RUNNING. All other states
        // keep the existing policy. Mirrors C++ GetPersistenceType DR fast-path.
        if self.config.enable_direct_routing && dr_mode_skips_persistence(s) {
            return;
        }
        if need_persistence_state(s)
            || matches!(
                s,
                InstanceState::Running
                    | InstanceState::Creating
                    | InstanceState::Scheduling
                    | InstanceState::Failed
                    | InstanceState::Exiting
                    | InstanceState::Evicting
                    | InstanceState::Evicted
            )
        {
            self.persist(meta).await;
        }
    }

    pub fn instances(&self) -> Arc<DashMap<String, InstanceMetadata>> {
        self.instances.clone()
    }

    fn etcd_key(&self, instance_id: &str) -> String {
        format!("{}/{}", INSTANCE_PATH_PREFIX, instance_id)
    }

    pub async fn persist(&self, meta: &InstanceMetadata) {
        let Some(store) = &self.etcd else {
            return;
        };
        let key = self.etcd_key(&meta.id);
        let payload = match serde_json::to_vec(meta) {
            Ok(p) => p,
            Err(e) => {
                warn!(error = %e, "serialize instance metadata");
                return;
            }
        };
        let mut c = store.lock().await;
        if let Err(e) = c.put(&key, &payload).await {
            warn!(error = %e, %key, "etcd put instance");
        }
    }

    pub fn clamp_resources(&self, required: &HashMap<String, f64>) -> HashMap<String, f64> {
        let mut m = required.clone();
        let cpu = m
            .get("cpu")
            .copied()
            .unwrap_or(self.config.min_instance_cpu)
            .clamp(self.config.min_instance_cpu, self.config.max_instance_cpu);
        let mem = m
            .get("memory")
            .copied()
            .unwrap_or(self.config.min_instance_memory)
            .clamp(
                self.config.min_instance_memory,
                self.config.max_instance_memory,
            );
        m.insert("cpu".into(), cpu);
        m.insert("memory".into(), mem);
        if let Some(npu) = m.get("npu").or_else(|| m.get("ascend")).copied() {
            m.insert("npu".into(), npu.max(0.0));
        }
        m
    }

    /// StartInstance: either in-process runtime manager (`enable_merge_process`) or FunctionAgent RPC.
    pub async fn start_instance(
        &self,
        instance_id: &str,
        function_name: &str,
        tenant_id: &str,
        resources: HashMap<String, f64>,
        runtime_type: &str,
        create_options: &HashMap<String, String>,
        trace_id: &str,
        create_args: &[yr_proto::common::Arg],
    ) -> Result<(String, i32), tonic::Status> {
        let service_meta = self.service_function_meta(function_name);
        let mut env_vars = service_meta
            .as_ref()
            .map(|m| m.env.clone())
            .unwrap_or_default();
        env_vars.extend(custom_envs_from_create_args(create_args));
        env_vars.extend(delegate_envs_from_create_options(create_options));
        env_vars.insert(
            "YR_SERVER_ADDRESS".into(),
            format!("{}:{}", self.config.host, self.config.posix_port),
        );
        env_vars.insert(
            "POSIX_LISTEN_ADDR".into(),
            format!("{}:{}", self.config.host, self.config.posix_port),
        );
        env_vars.insert(
            "PROXY_GRPC_SERVER_PORT".into(),
            self.config.posix_port.to_string(),
        );
        env_vars.insert("ENABLE_DS_CLIENT".into(), "true".into());
        env_vars.insert("POD_IP".into(), self.config.host.clone());
        env_vars.insert("HOST_IP".into(), self.config.host.clone());
        env_vars.insert(
            "DRIVER_SERVER_PORT".into(),
            self.config.posix_port.to_string(),
        );
        if self.config.data_system_port > 0 {
            let ds_addr = format!(
                "{}:{}",
                self.config.data_system_host, self.config.data_system_port
            );
            env_vars.insert("DATASYSTEM_ADDR".into(), ds_addr.clone());
            env_vars.insert("YR_DS_ADDRESS".into(), ds_addr);
        }
        if let Some(parent) = create_options
            .get("DELEGATE_DIRECTORY_INFO")
            .filter(|v| !v.trim().is_empty())
        {
            let parent = if std::path::Path::new(parent).is_dir() {
                parent.trim().to_string()
            } else {
                "/tmp".to_string()
            };
            let work_dir = format!("{}/{}", parent.trim_end_matches('/'), instance_id);
            env_vars.insert("INSTANCE_WORK_DIR".into(), work_dir.clone());
            env_vars.insert("DELEGATE_DIRECTORY_INFO".into(), parent);
            env_vars.insert("YR_INSTANCE_WORK_DIR".into(), work_dir);
        }
        for (k, v) in create_options {
            env_vars.entry(k.clone()).or_insert_with(|| v.clone());
        }
        if let Some(job_id) = job_id_from_trace(trace_id) {
            env_vars.insert("YR_JOB_ID".into(), job_id);
        }

        let resolved_runtime_type = service_meta
            .as_ref()
            .and_then(|m| (!m.runtime_type.trim().is_empty()).then(|| m.runtime_type.clone()))
            .unwrap_or_else(|| runtime_type.to_string());
        if resolved_runtime_type
            .to_ascii_lowercase()
            .contains("python")
        {
            env_vars
                .entry("INIT_HANDLER".into())
                .or_insert_with(|| "yrlib_handler.init".into());
            env_vars
                .entry("CALL_HANDLER".into())
                .or_insert_with(|| "yrlib_handler.call".into());
            env_vars
                .entry("CHECKPOINT_HANDLER".into())
                .or_insert_with(|| "yrlib_handler.checkpoint".into());
            env_vars
                .entry("RECOVER_HANDLER".into())
                .or_insert_with(|| "yrlib_handler.recover".into());
            env_vars
                .entry("SHUTDOWN_HANDLER".into())
                .or_insert_with(|| "yrlib_handler.shutdown".into());
            env_vars
                .entry("SIGNAL_HANDLER".into())
                .or_insert_with(|| "yrlib_handler.signal".into());
        }
        let resolved_code_path = service_meta
            .as_ref()
            .and_then(|m| (!m.code_path.trim().is_empty()).then(|| m.code_path.clone()))
            .unwrap_or_default();
        if !resolved_code_path.trim().is_empty() {
            env_vars.insert("FUNCTION_LIB_PATH".into(), resolved_code_path.clone());
            env_vars.insert("YR_FUNCTION_LIB_PATH".into(), resolved_code_path.clone());
        }

        if let Some(ld) = env_vars.get("LD_LIBRARY_PATH") {
            if ld.contains("depend") || ld.contains("${") {
                info!(
                    %instance_id,
                    %function_name,
                    ld_library_path = %ld,
                    yr_function_lib_path = ?env_vars.get("YR_FUNCTION_LIB_PATH"),
                    "start_instance prepared custom LD_LIBRARY_PATH"
                );
            }
        }

        // CONTAINER backend: functions whose services.yaml entry has a `rootfs` block
        // carry a sandbox config_json so the runtime_manager routes them to the
        // SandboxExecutor. Process-mode functions get an empty config_json (unchanged).
        let config_json = service_meta
            .as_ref()
            .and_then(|m| sandbox_config_json(m, create_options))
            .unwrap_or_default();
        if !config_json.is_empty() {
            info!(%instance_id, %function_name, "StartInstance → CONTAINER backend (services.yaml rootfs)");
        }

        let req = StartInstanceRequest {
            instance_id: instance_id.to_string(),
            function_name: function_name.to_string(),
            tenant_id: tenant_id.to_string(),
            runtime_type: resolved_runtime_type,
            env_vars,
            resources,
            code_path: resolved_code_path,
            config_json,
        };

        if let Some(st) = self.embedded_rm.as_ref() {
            let paths = st.config.runtime_path_list();
            info!(%instance_id, %function_name, "StartInstance → embedded runtime manager");
            let resp = start_instance_op(st, &paths, req).await?;
            if !resp.success {
                return Err(tonic::Status::internal(resp.message));
            }
            info!(%instance_id, runtime_id = %resp.runtime_id, runtime_port = resp.runtime_port, "StartInstance success");
            return Ok((resp.runtime_id, resp.runtime_port));
        }

        let configured_addr = self.config.runtime_manager_address.trim().to_string();
        let addr = if configured_addr.is_empty() {
            self.agent_manager
                .as_ref()
                .and_then(|m| m.first_grpc_endpoint())
                .unwrap_or_default()
        } else {
            configured_addr
        };
        if addr.is_empty() {
            return Err(tonic::Status::failed_precondition(
                "runtime_manager_address is not configured and no local function_agent is registered",
            ));
        }
        let uri = if addr.starts_with("http://") || addr.starts_with("https://") {
            addr
        } else {
            format!("http://{addr}")
        };
        info!(%instance_id, %function_name, agent_addr = %uri, "StartInstance → FunctionAgent");
        let mut client = FunctionAgentServiceClient::connect(uri)
            .await
            .map_err(|e| tonic::Status::internal(format!("connect agent: {e}")))?;

        let resp = client.start_instance(req).await?.into_inner();
        if !resp.success {
            return Err(tonic::Status::internal(resp.message));
        }
        info!(%instance_id, runtime_id = %resp.runtime_id, runtime_port = resp.runtime_port, "StartInstance success");
        Ok((resp.runtime_id, resp.runtime_port))
    }

    pub async fn stop_instance(
        &self,
        instance_id: &str,
        runtime_id: &str,
        force: bool,
    ) -> Result<(), tonic::Status> {
        if let Some(st) = self.embedded_rm.as_ref() {
            let resp = stop_instance_op(
                st,
                StopInstanceRequest {
                    instance_id: instance_id.to_string(),
                    runtime_id: runtime_id.to_string(),
                    force,
                },
            )?;
            if !resp.success {
                return Err(tonic::Status::internal(resp.message));
            }
            return Ok(());
        }

        let configured_addr = self.config.runtime_manager_address.trim().to_string();
        let addr = if configured_addr.is_empty() {
            self.agent_manager
                .as_ref()
                .and_then(|m| m.first_grpc_endpoint())
                .unwrap_or_default()
        } else {
            configured_addr
        };
        if addr.is_empty() {
            return Ok(());
        }
        let uri = if addr.starts_with("http://") || addr.starts_with("https://") {
            addr
        } else {
            format!("http://{addr}")
        };
        let mut client = FunctionAgentServiceClient::connect(uri)
            .await
            .map_err(|e| tonic::Status::internal(format!("connect agent: {e}")))?;
        let resp = client
            .stop_instance(StopInstanceRequest {
                instance_id: instance_id.to_string(),
                runtime_id: runtime_id.to_string(),
                force,
            })
            .await?
            .into_inner();
        if !resp.success {
            return Err(tonic::Status::internal(resp.message));
        }
        Ok(())
    }

    pub async fn apply_exit_event(
        &self,
        instance_id: &str,
        exit_ok: bool,
        message: &str,
    ) -> Option<InstanceMetadata> {
        let mut updated = None;
        if let Some(mut ent) = self.instances.get_mut(instance_id) {
            let next = if exit_ok {
                InstanceState::Exited
            } else {
                InstanceState::Failed
            };
            if ent.transition(next).is_ok() {
                info!(%instance_id, state = %ent.state, %message, "instance exit event");
                updated = Some(ent.clone());
            }
        }
        if let Some(ref meta) = updated {
            self.resource_view.release_used(&meta.resources);
            self.persist_if_policy(meta).await;
        }
        updated
    }

    /// Move to a terminal (or stopping) state and optionally return committed capacity to [`ResourceView`].
    pub async fn transition_terminal_with_release(
        &self,
        instance_id: &str,
        next: InstanceState,
        release_used_resources: bool,
    ) -> Option<InstanceMetadata> {
        let mut updated = None;
        if let Some(mut ent) = self.instances.get_mut(instance_id) {
            if ent.transition(next).is_ok() {
                updated = Some(ent.clone());
            }
        }
        if let Some(ref meta) = updated {
            if release_used_resources {
                self.resource_view.release_used(&meta.resources);
            }
            self.persist_if_policy(meta).await;
        }
        updated
    }

    /// Register metadata after local placement (caller already reserved resources).
    pub fn insert_metadata(&self, meta: InstanceMetadata) {
        self.instances.insert(meta.id.clone(), meta);
    }

    pub fn get(&self, id: &str) -> Option<InstanceMetadata> {
        self.instances.get(id).map(|e| e.clone())
    }

    pub fn remove(&self, id: &str) -> Option<InstanceMetadata> {
        self.instances.remove(id).map(|(_, v)| v)
    }

    /// Reload instance rows from MetaStore for this node (C++ `instance_recover` / cold start).
    ///
    /// Only reapplies JSON that deserializes as [`InstanceMetadata`] and matches `config.node_id`.
    /// `Running` instances refresh [`ResourceView`] usage; other states are loaded without touching
    /// capacity so we do not double-count in-flight schedules after a crash.
    pub async fn rehydrate_local_instances(
        &self,
        store: &mut yr_metastore_client::MetaStoreClient,
    ) -> u32 {
        let prefix = format!("{}/", INSTANCE_PATH_PREFIX.trim_end_matches('/'));
        let res = match store.get_prefix(&prefix).await {
            Ok(r) => r,
            Err(e) => {
                warn!(error = %e, "instance rehydrate get_prefix");
                return 0;
            }
        };
        let mut loaded = 0u32;
        for kv in res.kvs {
            let meta: InstanceMetadata = match serde_json::from_slice(&kv.value) {
                Ok(m) => m,
                Err(_) => continue,
            };
            if meta.node_id != self.config.node_id {
                continue;
            }
            if self.instances.contains_key(&meta.id) {
                continue;
            }
            if matches!(meta.state, InstanceState::Running) {
                self.resource_view.adopt_used(&meta.resources);
            }
            self.instances.insert(meta.id.clone(), meta);
            loaded += 1;
        }
        if loaded > 0 {
            info!(%loaded, node_id = %self.config.node_id, "rehydrated instances from MetaStore");
        }
        loaded
    }
}

#[cfg(test)]
mod sandbox_config_tests {
    use super::*;
    use yr_common::metadata::{BootstrapMetaData, RootfsSpecMeta, RootfsSrcType};
    use yr_runtime_manager::sandbox::parse_sandbox_config;

    fn container_meta() -> ServiceFunctionMeta {
        ServiceFunctionMeta {
            runtime_type: "python3.10".into(),
            rootfs: RootfsSpecMeta {
                runtime: "runc".into(),
                r#type: RootfsSrcType::Image,
                readonly: true,
                image_url: "aio-yr-runtime:latest".into(),
                mountpoint: "/mnt".into(),
                ..Default::default()
            },
            bootstrap: BootstrapMetaData {
                entrypoint: "set -e; python ${YR_RUNTIME_MAIN}".into(),
                ..Default::default()
            },
            ..Default::default()
        }
    }

    #[test]
    fn process_mode_function_emits_no_sandbox_config() {
        let meta = ServiceFunctionMeta {
            runtime_type: "python3.10".into(),
            ..Default::default()
        };
        assert!(sandbox_config_json(&meta, &HashMap::new()).is_none());
    }

    #[test]
    fn rootfs_function_emits_container_config_with_port_forwards() {
        let mut opts = HashMap::new();
        // CreateOpt["network"] is the frontend's portForwardings JSON.
        opts.insert(
            "network".to_string(),
            r#"{"portForwardings":[{"port":8080,"protocol":"tcp"}]}"#.to_string(),
        );
        let json = sandbox_config_json(&container_meta(), &opts).expect("container config");
        // Round-trips through the runtime_manager's decoder (the "sandbox":true gate).
        let cfg = parse_sandbox_config(&json).expect("decodes as sandbox config");
        assert_eq!(cfg.image, "aio-yr-runtime:latest");
        assert_eq!(cfg.rootfs_type, "image");
        assert_eq!(cfg.sandbox_runtime, "runc");
        assert!(cfg.readonly);
        assert_eq!(cfg.mountpoint, "/mnt");
        assert_eq!(cfg.ports, vec!["tcp:8080".to_string()]);
        // entrypoint is whitespace-split; runtime args are appended later by the RM.
        assert_eq!(cfg.command.first().map(String::as_str), Some("set"));
        assert!(cfg.command.iter().any(|t| t == "${YR_RUNTIME_MAIN}"));
    }

    #[test]
    fn rootfs_function_without_ports_still_routes_to_container() {
        let json = sandbox_config_json(&container_meta(), &HashMap::new()).expect("container");
        let cfg = parse_sandbox_config(&json).expect("decodes");
        assert!(cfg.sandbox);
        assert!(cfg.ports.is_empty());
    }
}
