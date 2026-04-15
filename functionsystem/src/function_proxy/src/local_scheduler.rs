use crate::resource_view::ResourceVector;
use crate::schedule_reporter;
use crate::state_machine::{InstanceMetadata, InstanceState};
use crate::AppContext;
use async_trait::async_trait;
use std::sync::Arc;
use std::time::Instant;
use tokio::sync::Mutex;
use tracing::warn;
use yr_proto::internal::domain_scheduler_service_client::DomainSchedulerServiceClient;
use yr_proto::internal::local_scheduler_service_server::LocalSchedulerService;
use yr_proto::internal::{
    EvictInstancesRequest, EvictInstancesResponse, GroupScheduleRequest, GroupScheduleResponse,
    KillGroupRequest, KillGroupResponse, PreemptInstancesRequest, PreemptInstancesResponse,
    ScheduleRequest, ScheduleResponse,
};

/// Optional token bucket for limiting instance creates per second.
#[derive(Debug)]
pub struct CreateRateLimiter {
    per_sec: u32,
    inner: Mutex<(f64, Instant)>,
}

impl CreateRateLimiter {
    pub fn new(per_sec: u32) -> Option<Arc<Self>> {
        if per_sec == 0 {
            return None;
        }
        Some(Arc::new(Self {
            per_sec,
            inner: Mutex::new((per_sec as f64, Instant::now())),
        }))
    }

    pub async fn acquire(&self) -> bool {
        let mut g = self.inner.lock().await;
        let now = Instant::now();
        let elapsed = now.duration_since(g.1).as_secs_f64();
        g.0 = (g.0 + elapsed * self.per_sec as f64).min(self.per_sec as f64);
        g.1 = now;
        if g.0 >= 1.0 {
            g.0 -= 1.0;
            true
        } else {
            false
        }
    }
}

pub struct LocalSchedulerGrpc {
    ctx: Arc<AppContext>,
    rate: Option<Arc<CreateRateLimiter>>,
}

impl LocalSchedulerGrpc {
    pub fn new(ctx: Arc<AppContext>) -> Self {
        let rate = CreateRateLimiter::new(ctx.config.create_rate_limit_per_sec);
        Self { ctx, rate }
    }

    async fn forward_domain(
        &self,
        mut req: ScheduleRequest,
    ) -> Result<tonic::Response<ScheduleResponse>, tonic::Status> {
        req.source_node_id = self.ctx.config.node_id.clone();
        let domain = self.ctx.domain_addr.read().clone();
        let domain = domain.trim().to_string();
        if domain.is_empty() {
            return Err(tonic::Status::failed_precondition(
                "no domain scheduler address",
            ));
        }
        let mut client = DomainSchedulerServiceClient::connect(domain)
            .await
            .map_err(|e| tonic::Status::internal(e.to_string()))?;
        client
            .forward_schedule(req)
            .await
    }

    async fn schedule_local(
        &self,
        req: ScheduleRequest,
    ) -> Result<tonic::Response<ScheduleResponse>, tonic::Status> {
        let func_ver = req
            .extension
            .get("function_version")
            .map(|s| s.as_str())
            .unwrap_or("$latest");
        self.ctx
            .instance_ctrl
            .schedule_get_func_meta(&req.tenant_id, &req.function_name, func_ver)
            .await?;
        self.ctx
            .instance_ctrl
            .schedule_do_authorize_create(&req.tenant_id, &req.function_name)
            .await?;
        if self.ctx.instance_ctrl.tenant_cooldown_active(&req.tenant_id) {
            return Ok(tonic::Response::new(ScheduleResponse {
                success: false,
                error_code: yr_proto::common::ErrorCode::ErrCreateRateLimited as i32,
                message: "tenant cooldown active".into(),
                instance_id: String::new(),
                node_id: String::new(),
                node_address: String::new(),
            }));
        }

        if let Some(lim) = &self.rate {
            if !lim.acquire().await {
                return Ok(tonic::Response::new(ScheduleResponse {
                    success: false,
                    error_code: yr_proto::common::ErrorCode::ErrCreateRateLimited as i32,
                    message: "local create rate limited".into(),
                    instance_id: String::new(),
                    node_id: String::new(),
                    node_address: String::new(),
                }));
            }
        }

        let required = req.required_resources.clone();
        let clamped = self.ctx.instance_ctrl.clamp_resources(&required);
        if !self.ctx.resource_view.reserve_pending(&clamped) {
            return Ok(tonic::Response::new(ScheduleResponse {
                success: false,
                error_code: yr_proto::common::ErrorCode::ErrResourceNotEnough as i32,
                message: "not enough local resources".into(),
                instance_id: String::new(),
                node_id: String::new(),
                node_address: String::new(),
            }));
        }

        let instance_id = if req.designated_instance_id.is_empty() {
            uuid::Uuid::new_v4().to_string()
        } else {
            req.designated_instance_id.clone()
        };

        let group_id = req
            .labels
            .get("group_id")
            .cloned()
            .or_else(|| req.extension.get("group_id").cloned());

        let now = InstanceMetadata::now_ms();
        let mut meta = InstanceMetadata {
            id: instance_id.clone(),
            function_name: req.function_name.clone(),
            tenant: req.tenant_id.clone(),
            node_id: self.ctx.config.node_id.clone(),
            runtime_id: String::new(),
            runtime_port: 0,
            state: InstanceState::Scheduling,
            created_at_ms: now,
            updated_at_ms: now,
            group_id,
            trace_id: req.trace_id.clone(),
            resources: clamped.clone(),
            etcd_kv_version: None,
            etcd_mod_revision: None,
        };

        if meta.transition(InstanceState::Creating).is_err() {
            self.ctx.resource_view.release_pending(&clamped);
            return Err(tonic::Status::internal("state machine"));
        }

        self.ctx.instance_ctrl.insert_metadata(meta.clone());
        self.ctx.instance_ctrl.persist_if_policy(&meta).await;

        let rt = req
            .extension
            .get("runtime_type")
            .cloned()
            .unwrap_or_else(|| "default".into());

        match self
            .ctx
            .instance_ctrl
            .start_instance(
                &instance_id,
                &req.function_name,
                &req.tenant_id,
                clamped.clone(),
                &rt,
            )
            .await
        {
            Ok((runtime_id, runtime_port)) => {
                self.ctx.resource_view.commit_pending_to_used(&clamped);
                if let Some(mut m) = self.ctx.instance_ctrl.instances().get_mut(&instance_id) {
                    m.runtime_id = runtime_id.clone();
                    m.runtime_port = runtime_port;
                    if let Err(e) = m.transition(InstanceState::Running) {
                        warn!(error = %e, %instance_id, "transition to Running failed");
                    }
                    let snapshot = m.clone();
                    drop(m);
                    self.ctx.instance_ctrl.persist_if_policy(&snapshot).await;
                }
                Ok(tonic::Response::new(ScheduleResponse {
                    success: true,
                    error_code: 0,
                    message: String::new(),
                    instance_id,
                    node_id: self.ctx.config.node_id.clone(),
                    node_address: self.ctx.config.advertise_grpc_endpoint(),
                }))
            }
            Err(e) => {
                self.ctx.resource_view.release_pending(&clamped);
                if let Some(mut m) = self.ctx.instance_ctrl.instances().get_mut(&instance_id) {
                    if let Err(e) = m.transition(InstanceState::Failed) {
                        warn!(error = %e, %instance_id, "transition to Failed failed");
                    }
                    let snapshot = m.clone();
                    drop(m);
                    self.ctx.instance_ctrl.persist_if_policy(&snapshot).await;
                }
                warn!(error = %e, %instance_id, "StartInstance failed");
                Ok(tonic::Response::new(ScheduleResponse {
                    success: false,
                    error_code: yr_proto::common::ErrorCode::ErrRuntimeManagerOperationError as i32,
                    message: e.to_string(),
                    instance_id,
                    node_id: String::new(),
                    node_address: String::new(),
                }))
            }
        }
    }

    fn should_place_locally(&self, req: &ScheduleRequest) -> bool {
        let required = ResourceVector::from_required(&req.required_resources);
        let used = self.ctx.resource_view.used_snapshot();
        let pending = self.ctx.resource_view.pending_snapshot();
        let cap = self.ctx.resource_view.capacity_snapshot();
        used.cpu + pending.cpu + required.cpu <= cap.cpu
            && used.memory + pending.memory + required.memory <= cap.memory
            && used.npu + pending.npu + required.npu <= cap.npu
    }
}

#[async_trait]
impl LocalSchedulerService for LocalSchedulerGrpc {
    async fn schedule(
        &self,
        request: tonic::Request<ScheduleRequest>,
    ) -> Result<tonic::Response<ScheduleResponse>, tonic::Status> {
        let req = request.into_inner();
        let try_local = req
            .extension
            .get("force_domain")
            .map(|v| v != "true")
            .unwrap_or(true);

        if try_local && self.should_place_locally(&req) {
            self.schedule_local(req).await
        } else {
            self.forward_domain(req).await
        }
    }

    async fn evict_instances(
        &self,
        request: tonic::Request<EvictInstancesRequest>,
    ) -> Result<tonic::Response<EvictInstancesResponse>, tonic::Status> {
        let r = request.into_inner();
        let mut evicted = Vec::new();
        for id in r.instance_ids {
            if let Some(mut m) = self.ctx.instance_ctrl.instances().get_mut(&id) {
                if let Err(e) = m.transition(InstanceState::Evicting) {
                    warn!(error = %e, %id, "transition to Evicting failed");
                }
            }
            let meta = self.ctx.instance_ctrl.get(&id);
            if let Some(ref meta) = meta {
                let _ = self
                    .ctx
                    .instance_ctrl
                    .stop_instance(&id, &meta.runtime_id, true)
                    .await;
            }
            if let Some(mut m) = self.ctx.instance_ctrl.instances().get_mut(&id) {
                if let Err(e) = m.transition(InstanceState::Evicted) {
                    warn!(error = %e, %id, "transition to Evicted failed");
                }
                self.ctx.resource_view.release_used(&m.resources);
                let snap = m.clone();
                drop(m);
                self.ctx.instance_ctrl.persist_if_policy(&snap).await;
            }
            evicted.push(id);
        }
        Ok(tonic::Response::new(EvictInstancesResponse {
            success: true,
            evicted_ids: evicted,
        }))
    }

    async fn preempt_instances(
        &self,
        request: tonic::Request<PreemptInstancesRequest>,
    ) -> Result<tonic::Response<PreemptInstancesResponse>, tonic::Status> {
        if !self.ctx.config.enable_preemption {
            return Ok(tonic::Response::new(PreemptInstancesResponse {
                success: false,
                preempted_ids: vec![],
            }));
        }
        let r = request.into_inner();
        let mut preempted = Vec::new();
        for id in r.instance_ids {
            if let Some(mut m) = self.ctx.instance_ctrl.instances().get_mut(&id) {
                if let Err(e) = m.transition(InstanceState::Evicting) {
                    warn!(error = %e, %id, "transition to Evicting failed");
                }
            }
            let meta = self.ctx.instance_ctrl.get(&id);
            if let Some(ref meta) = meta {
                let _ = self
                    .ctx
                    .instance_ctrl
                    .stop_instance(&id, &meta.runtime_id, true)
                    .await;
            }
            if let Some(mut m) = self.ctx.instance_ctrl.instances().get_mut(&id) {
                if let Err(e) = m.transition(InstanceState::Evicted) {
                    warn!(error = %e, %id, "transition to Evicted failed");
                }
                self.ctx.resource_view.release_used(&m.resources);
                let snap = m.clone();
                drop(m);
                self.ctx.instance_ctrl.persist_if_policy(&snap).await;
            }
            preempted.push(id);
        }
        Ok(tonic::Response::new(PreemptInstancesResponse {
            success: true,
            preempted_ids: preempted,
        }))
    }

    async fn group_schedule(
        &self,
        request: tonic::Request<GroupScheduleRequest>,
    ) -> Result<tonic::Response<GroupScheduleResponse>, tonic::Status> {
        let r = request.into_inner();
        let mut ids = Vec::new();
        for mut sub in r.requests {
            if sub.extension.get("group_id").is_none() && !r.group_id.is_empty() {
                sub.extension
                    .insert("group_id".into(), r.group_id.clone());
            }
            let resp = self
                .schedule(tonic::Request::new(sub))
                .await?
                .into_inner();
            if !resp.success {
                return Ok(tonic::Response::new(GroupScheduleResponse {
                    success: false,
                    error_code: resp.error_code,
                    message: resp.message,
                    instance_ids: ids,
                    group_id: r.group_id,
                }));
            }
            ids.push(resp.instance_id);
        }
        Ok(tonic::Response::new(GroupScheduleResponse {
            success: true,
            error_code: 0,
            message: String::new(),
            instance_ids: ids,
            group_id: r.group_id,
        }))
    }

    async fn kill_group(
        &self,
        request: tonic::Request<KillGroupRequest>,
    ) -> Result<tonic::Response<KillGroupResponse>, tonic::Status> {
        let gid = request.into_inner().group_id;
        let mut hit = Vec::new();
        for e in self.ctx.instance_ctrl.instances().iter() {
            if e.group_id.as_deref() == Some(&gid) {
                hit.push(e.id.clone());
            }
        }
        for id in hit {
            let _ = self
                .evict_instances(tonic::Request::new(EvictInstancesRequest {
                    instance_ids: vec![id],
                    reason: "kill_group".into(),
                }))
                .await;
        }
        Ok(tonic::Response::new(KillGroupResponse { success: true }))
    }
}
