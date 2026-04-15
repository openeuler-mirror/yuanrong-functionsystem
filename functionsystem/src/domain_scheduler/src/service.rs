use std::sync::Arc;
use std::time::Duration;

use async_trait::async_trait;
use tonic::{Request, Response, Status};
use tracing::Instrument;
use yr_proto::internal::domain_scheduler_service_server::DomainSchedulerService;
use yr_proto::internal::{
    GroupScheduleRequest, GroupScheduleResponse, HeartbeatPing, HeartbeatPong, ScheduleRequest,
    ScheduleResponse, WorkerStatusNotification, WorkerStatusResponse,
};

use crate::group::{self, GroupLifecycle};
use crate::schedule_decision::TrackedInstance;
use crate::state::DomainSchedulerState;

pub struct DomainSchedulerGrpc {
    state: Arc<DomainSchedulerState>,
}

impl DomainSchedulerGrpc {
    pub fn new(state: Arc<DomainSchedulerState>) -> Self {
        Self { state }
    }

    fn clamp_req_priority(&self, req: &mut ScheduleRequest) {
        req.priority = req
            .priority
            .clamp(0, self.state.config.max_priority);
    }

    fn reserve_id(req: &ScheduleRequest) -> String {
        if req.request_id.is_empty() {
            format!("auto-{}", uuid::Uuid::new_v4())
        } else {
            req.request_id.clone()
        }
    }

    async fn try_preempt_anywhere(&self, req: &ScheduleRequest, exclude_source: bool) -> bool {
        if !self.state.config.enable_preemption {
            return false;
        }
        for n in self.state.nodes.healthy_node_infos() {
            if exclude_source {
                let s = req.source_node_id.as_str();
                if !s.is_empty() && s == n.node_id.as_str() {
                    continue;
                }
            }
            if self
                .state
                .scheduler
                .preemption
                .try_preempt_for_schedule(
                    &self.state.nodes,
                    &self.state.resource_view,
                    &n.node_id,
                    req.priority,
                    "domain scheduler preemption",
                )
                .await
            {
                return true;
            }
        }
        false
    }

    pub(crate) async fn place_one(
        &self,
        mut req: ScheduleRequest,
        exclude_source: bool,
    ) -> Result<ScheduleResponse, Status> {
        if !self.state.require_leader() {
            return Err(Status::failed_precondition(
                "not leader: domain scheduler passive mode",
            ));
        }
        self.clamp_req_priority(&mut req);
        let ttl = if req.schedule_timeout_ms > 0 {
            Duration::from_millis(req.schedule_timeout_ms as u64)
        } else {
            Duration::from_secs(120)
        };

        let mut node = self.state.scheduler.select_node(&req, exclude_source);
        if node.is_none() && self.try_preempt_anywhere(&req, exclude_source).await {
            node = self.state.scheduler.select_node(&req, exclude_source);
        }

        let Some(node) = node else {
            self.state.scheduler.enqueue_pending(req.clone());
            return Ok(ScheduleResponse {
                success: false,
                error_code: 409,
                message: "no suitable local node (queued)".into(),
                instance_id: String::new(),
                node_id: String::new(),
                node_address: String::new(),
            });
        };

        let rid = Self::reserve_id(&req);
        let mut reserved = self.state.resource_view.try_reserve(
            &node.node_id,
            &rid,
            &req.required_resources,
            ttl,
        );

        if !reserved
            && self.state.config.enable_preemption
            && self
                .state
                .scheduler
                .preemption
                .try_preempt_for_schedule(
                    &self.state.nodes,
                    &self.state.resource_view,
                    &node.node_id,
                    req.priority,
                    "domain scheduler reservation preemption",
                )
                .await
        {
            reserved = self.state.resource_view.try_reserve(
                &node.node_id,
                &rid,
                &req.required_resources,
                ttl,
            );
        }

        if !reserved {
            self.state.scheduler.enqueue_pending(req.clone());
            return Ok(ScheduleResponse {
                success: false,
                error_code: 409,
                message: "resource reservation failed (queued)".into(),
                instance_id: String::new(),
                node_id: String::new(),
                node_address: String::new(),
            });
        }

        let forward = self
            .state
            .nodes
            .forward_schedule(&node.node_id, req.clone())
            .await;

        match forward {
            Ok(resp) => {
                if resp.success {
                    self.state
                        .resource_view
                        .commit_reservation(&node.node_id, &rid);
                    self.state.scheduler.recorder.record(
                        &req.request_id,
                        &req.function_name,
                        Some(&node.node_id),
                        "success",
                        "forward ok",
                    );
                    if !resp.instance_id.is_empty() {
                        self.state.scheduler.preemption.record_placement(TrackedInstance {
                            instance_id: resp.instance_id.clone(),
                            node_id: node.node_id.clone(),
                            priority: req.priority,
                            resources: req.required_resources.clone(),
                        });
                    }
                    Ok(resp)
                } else {
                    self.state
                        .resource_view
                        .release_reservation(&node.node_id, &rid);
                    self.state.scheduler.recorder.record(
                        &req.request_id,
                        &req.function_name,
                        Some(&node.node_id),
                        "rejected",
                        &resp.message,
                    );
                    Ok(resp)
                }
            }
            Err(e) => {
                self.state
                    .resource_view
                    .release_reservation(&node.node_id, &rid);
                if e.code() == tonic::Code::ResourceExhausted {
                    self.state.scheduler.enqueue_pending(req);
                }
                Err(e)
            }
        }
    }
}

/// Background retry for queued single schedules.
pub async fn pending_reconcile_tick(state: &Arc<DomainSchedulerState>) {
    let Some(req) = state.scheduler.drain_one_ready() else {
        return;
    };
    let svc = DomainSchedulerGrpc::new(state.clone());
    match svc.place_one(req, false).await {
        Ok(resp) if !resp.success => {
            tracing::debug!(message = %resp.message, "pending reconcile still blocked");
        }
        Err(e) => {
            tracing::debug!(error = %e, "pending reconcile rpc error");
        }
        _ => {}
    }
}

#[async_trait]
impl DomainSchedulerService for DomainSchedulerGrpc {
    async fn schedule(
        &self,
        request: Request<ScheduleRequest>,
    ) -> Result<Response<ScheduleResponse>, Status> {
        let req = request.into_inner();
        let span = tracing::info_span!("schedule", request_id = %req.request_id);
        self.place_one(req, false).instrument(span).await.map(Response::new)
    }

    async fn forward_schedule(
        &self,
        request: Request<ScheduleRequest>,
    ) -> Result<Response<ScheduleResponse>, Status> {
        let req = request.into_inner();
        let span = tracing::info_span!("forward_schedule", request_id = %req.request_id);
        self.place_one(req, true).instrument(span).await.map(Response::new)
    }

    async fn group_schedule(
        &self,
        request: Request<GroupScheduleRequest>,
    ) -> Result<Response<GroupScheduleResponse>, Status> {
        if !self.state.require_leader() {
            return Err(Status::failed_precondition(
                "not leader: domain scheduler passive mode",
            ));
        }
        let req = request.into_inner();
        let span = tracing::info_span!("group_schedule", group_id = %req.group_id);
        async {
            let prefix = self.state.group_meta_prefix();
            if let Some(ms) = self.state.metastore.as_ref() {
                let mut guard = ms.lock().await;
                let life = GroupLifecycle {
                    client: &mut *guard,
                    key_prefix: prefix.as_str(),
                };
                group::execute_group_schedule(
                    &self.state.scheduler,
                    &self.state.resource_view,
                    &self.state.nodes,
                    req,
                    Some(life),
                )
                .await
            } else {
                group::execute_group_schedule(
                    &self.state.scheduler,
                    &self.state.resource_view,
                    &self.state.nodes,
                    req,
                    None,
                )
                .await
            }
        }
        .instrument(span)
        .await
        .map(Response::new)
    }

    async fn notify_worker_status(
        &self,
        request: Request<WorkerStatusNotification>,
    ) -> Result<Response<WorkerStatusResponse>, Status> {
        let r = request.into_inner();
        if r.node_id.is_empty() {
            return Err(Status::invalid_argument("node_id required"));
        }
        self.state
            .nodes
            .notify_worker_status(&r.node_id, &r.status, &r.reason);
        self.state.worker_propagator.on_worker_status(
            &r.node_id,
            &r.status,
            &r.reason,
            &self.state.nodes,
        );
        Ok(Response::new(WorkerStatusResponse { acknowledged: true }))
    }

    async fn heartbeat(
        &self,
        request: Request<HeartbeatPing>,
    ) -> Result<Response<HeartbeatPong>, Status> {
        let r = request.into_inner();
        if !r.node_id.is_empty() {
            self.state.nodes.on_heartbeat(&r.node_id);
        }
        let now = std::time::SystemTime::now()
            .duration_since(std::time::SystemTime::UNIX_EPOCH)
            .map(|d| d.as_millis() as i64)
            .unwrap_or(0);
        Ok(Response::new(HeartbeatPong {
            timestamp_ms: now,
        }))
    }
}
