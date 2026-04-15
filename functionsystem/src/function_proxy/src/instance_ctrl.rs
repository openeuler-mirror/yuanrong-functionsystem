use crate::config::Config;
use crate::function_meta::FunctionMetaCache;
use crate::resource_view::ResourceView;
use crate::state_machine::{InstanceMetadata, InstanceState};
use dashmap::DashMap;
use std::collections::HashMap;
use std::sync::Arc;
use tracing::{info, warn};
use yr_common::etcd_keys::{gen_func_meta_key, INSTANCE_PATH_PREFIX};
use yr_common::types::need_persistence_state;
use yr_proto::internal::function_agent_service_client::FunctionAgentServiceClient;
use yr_proto::internal::{StartInstanceRequest, StopInstanceRequest};
use yr_runtime_manager::runtime_ops::{start_instance_op, stop_instance_op};
use yr_runtime_manager::state::RuntimeManagerState;

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
            function_meta: Arc::new(FunctionMetaCache::new()),
        })
    }

    pub fn function_meta_cache(&self) -> &Arc<FunctionMetaCache> {
        &self.function_meta
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
        self.tenant_cooldown_until_ms.insert(tenant.to_string(), until);
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
    ) -> Result<(String, i32), tonic::Status> {
        let mut env_vars = HashMap::new();
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
        if self.config.data_system_port > 0 {
            let ds_addr = format!(
                "{}:{}",
                self.config.data_system_host, self.config.data_system_port
            );
            env_vars.insert("DATASYSTEM_ADDR".into(), ds_addr.clone());
            env_vars.insert("YR_DS_ADDRESS".into(), ds_addr);
        }

        let req = StartInstanceRequest {
            instance_id: instance_id.to_string(),
            function_name: function_name.to_string(),
            tenant_id: tenant_id.to_string(),
            runtime_type: runtime_type.to_string(),
            env_vars,
            resources,
            code_path: String::new(),
            config_json: String::new(),
        };

        if let Some(st) = self.embedded_rm.as_ref() {
            let paths = st.config.runtime_path_list();
            info!(%instance_id, %function_name, "StartInstance → embedded runtime manager");
            let resp = start_instance_op(st, &paths, req)?;
            if !resp.success {
                return Err(tonic::Status::internal(resp.message));
            }
            info!(%instance_id, runtime_id = %resp.runtime_id, runtime_port = resp.runtime_port, "StartInstance success");
            return Ok((resp.runtime_id, resp.runtime_port));
        }

        let addr = self.config.runtime_manager_address.trim();
        if addr.is_empty() {
            return Err(tonic::Status::failed_precondition(
                "runtime_manager_address is not configured",
            ));
        }
        let uri = if addr.starts_with("http://") || addr.starts_with("https://") {
            addr.to_string()
        } else {
            format!("http://{addr}")
        };
        info!(%instance_id, %function_name, agent_addr = %uri, "StartInstance → FunctionAgent");
        let mut client = FunctionAgentServiceClient::connect(uri)
            .await
            .map_err(|e| tonic::Status::internal(format!("connect agent: {e}")))?;

        let resp = client
            .start_instance(req)
            .await?
            .into_inner();
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

        let addr = self.config.runtime_manager_address.trim();
        if addr.is_empty() {
            return Ok(());
        }
        let uri = if addr.starts_with("http://") || addr.starts_with("https://") {
            addr.to_string()
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
