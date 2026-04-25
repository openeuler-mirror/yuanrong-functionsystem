use std::collections::VecDeque;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use parking_lot::Mutex;

use async_trait::async_trait;
use etcd_client::Client;
use tonic::{Request, Response, Status};
use tracing::warn;
use yr_proto::internal::global_scheduler_service_server::GlobalSchedulerService;
use yr_proto::internal::{
    EvictAgentRequest, EvictAgentResponse, GroupScheduleRequest, GroupScheduleResponse,
    QueryAgentCountRequest, QueryAgentCountResponse, QueryAgentsRequest, QueryAgentsResponse,
    RegisterRequest, RegisterResponse, ScheduleRequest, ScheduleResponse, UpdateResourcesRequest,
    UpdateResourcesResponse,
};

use crate::config::MasterConfig;
use crate::domain_activator::DomainActivator;
use crate::domain_sched_mgr::DomainSchedMgr;
use crate::instances::InstanceManager;
use crate::local_sched_mgr::LocalSchedMgr;
use crate::node_manager::NodeManager;
use crate::schedule_decision::ScheduleDecisionManager;
use crate::schedule_manager::ScheduleManager;
use crate::snapshot::SnapshotManager;
use crate::system_func_loader::SystemFunctionLoader;
use crate::topology::TopologyManager;

/// Shared master state: leader flag, topology, instance caches, and scheduler sub-managers.
pub struct MasterState {
    pub config: Arc<MasterConfig>,
    pub is_leader: Arc<AtomicBool>,
    pub topology: Arc<TopologyManager>,
    pub instances: Arc<InstanceManager>,
    pub domain_sched_mgr: Arc<DomainSchedMgr>,
    pub local_sched_mgr: Arc<LocalSchedMgr>,
    pub domain_activator: Arc<DomainActivator>,
    pub system_loader: Arc<SystemFunctionLoader>,
    /// Recent schedule request ids (diagnostics / HTTP `scheduling_queue`).
    pub scheduling_queue: Arc<Mutex<VecDeque<String>>>,
    pub snapshots: Arc<SnapshotManager>,
    pub schedule_mgr: Arc<ScheduleManager>,
    pub schedule_decision: Arc<ScheduleDecisionManager>,
    pub node_manager: Arc<NodeManager>,
}

impl MasterState {
    #[allow(clippy::too_many_arguments)]
    pub fn new(
        config: Arc<MasterConfig>,
        is_leader: Arc<AtomicBool>,
        topology: Arc<TopologyManager>,
        instances: Arc<InstanceManager>,
        domain_sched_mgr: Arc<DomainSchedMgr>,
        local_sched_mgr: Arc<LocalSchedMgr>,
        domain_activator: Arc<DomainActivator>,
        system_loader: Arc<SystemFunctionLoader>,
        scheduling_queue: Arc<Mutex<VecDeque<String>>>,
        snapshots: Arc<SnapshotManager>,
        schedule_mgr: Arc<ScheduleManager>,
        schedule_decision: Arc<ScheduleDecisionManager>,
        node_manager: Arc<NodeManager>,
    ) -> Arc<Self> {
        Arc::new(Self {
            config,
            is_leader,
            topology,
            instances,
            domain_sched_mgr,
            local_sched_mgr,
            domain_activator,
            system_loader,
            scheduling_queue,
            snapshots,
            schedule_mgr,
            schedule_decision,
            node_manager,
        })
    }

    pub fn require_leader(&self) -> bool {
        self.is_leader.load(Ordering::SeqCst)
    }

    pub fn set_leader(&self, v: bool) {
        self.is_leader.store(v, Ordering::SeqCst);
        if !v {
            self.domain_sched_mgr.disconnect_all();
        } else {
            self.rebuild_domain_routes();
            self.schedule_decision
                .apply_topology_resources(&self.topology);
        }
    }

    pub fn is_leader(&self) -> bool {
        self.require_leader()
    }

    /// Register every domain node (level 1) with [`DomainSchedMgr`] — C++ `Connect` after recovery.
    pub fn rebuild_domain_routes(&self) {
        self.domain_sched_mgr.disconnect_all();
        for (_, n) in self.topology.sched_tree().find_nodes(1) {
            self.domain_sched_mgr.register(n.name(), n.address());
        }
        self.schedule_decision
            .apply_topology_resources(&self.topology);
    }

    /// C++ `GlobalSchedActor::DoSchedule` (master only; slave has no root domain).
    pub async fn do_schedule(self: &Arc<Self>, req: ScheduleRequest) -> ScheduleResponse {
        if !self.require_leader() {
            return ScheduleResponse {
                success: false,
                error_code: 9,
                message: "not leader: schedule rejected (slave)".into(),
                instance_id: String::new(),
                node_id: String::new(),
                node_address: String::new(),
            };
        }
        {
            let mut q = self.scheduling_queue.lock();
            q.push_back(req.request_id.clone());
            if q.len() > 256 {
                q.pop_front();
            }
        }
        let this = Arc::clone(self);
        let req = req;
        match tokio::task::spawn_blocking(move || {
            this.schedule_mgr
                .process_schedule_request(req, &this.topology)
        })
        .await
        {
            Ok(r) => r,
            Err(_) => ScheduleResponse {
                success: false,
                error_code: 8,
                message: "schedule worker join failed".into(),
                instance_id: String::new(),
                node_id: String::new(),
                node_address: String::new(),
            },
        }
    }

    /// C++ `DoGroupSchedule` / `GroupSchedule` with retry interval from config.
    pub async fn do_group_schedule(
        self: &Arc<Self>,
        req: GroupScheduleRequest,
    ) -> GroupScheduleResponse {
        if !self.require_leader() {
            return GroupScheduleResponse {
                success: false,
                error_code: 9,
                message: "not leader: group schedule rejected".into(),
                instance_ids: vec![],
                group_id: req.group_id.clone(),
            };
        }
        if req.group_id.is_empty() {
            return GroupScheduleResponse {
                success: false,
                error_code: 3,
                message: "group_id is required".into(),
                instance_ids: vec![],
                group_id: String::new(),
            };
        }
        if req.requests.is_empty() {
            return GroupScheduleResponse {
                success: false,
                error_code: 3,
                message: "requests list is empty".into(),
                instance_ids: vec![],
                group_id: req.group_id.clone(),
            };
        }
        loop {
            if self.topology.root_domain().is_none() {
                tokio::time::sleep(Duration::from_secs(self.config.schedule_retry_sec.max(1)))
                    .await;
                continue;
            }
            let this = Arc::clone(self);
            let req = req.clone();
            let group_id_for_err = req.group_id.clone();
            return match tokio::task::spawn_blocking(move || {
                this.schedule_mgr
                    .process_group_schedule_request(req, &this.topology)
            })
            .await
            {
                Ok(r) => r,
                Err(_) => GroupScheduleResponse {
                    success: false,
                    error_code: 8,
                    message: "group schedule worker join failed".into(),
                    instance_ids: vec![],
                    group_id: group_id_for_err,
                },
            };
        }
    }
}

pub struct GlobalSchedulerImpl {
    state: Arc<MasterState>,
}

impl GlobalSchedulerImpl {
    pub fn new(state: Arc<MasterState>) -> Self {
        Self { state }
    }

    async fn publish_ready_agent_count(&self) {
        let endpoints: Vec<&str> = self
            .state
            .config
            .etcd_endpoints
            .iter()
            .map(String::as_str)
            .collect();
        if endpoints.is_empty() {
            return;
        }
        let key = self.state.config.ready_agent_count_key();
        let value = self.state.topology.agent_count().to_string();
        match Client::connect(endpoints, None).await {
            Ok(mut c) => {
                if let Err(e) = c.put(key.clone(), value.clone(), None).await {
                    warn!(error = %e, %key, %value, "publish ready-agent count failed");
                }
            }
            Err(e) => {
                warn!(error = %e, %key, %value, "connect etcd to publish ready-agent count failed")
            }
        }
    }
}

#[async_trait]
impl GlobalSchedulerService for GlobalSchedulerImpl {
    async fn register(
        &self,
        request: Request<RegisterRequest>,
    ) -> Result<Response<RegisterResponse>, Status> {
        if !self.state.require_leader() {
            return Err(Status::failed_precondition(
                "not leader: registration disabled in passive mode",
            ));
        }
        let r = request.into_inner();
        if r.node_id.is_empty() {
            return Err(Status::invalid_argument("node_id required"));
        }
        let (domain_address, rec) = self
            .state
            .topology
            .register_local(r.node_id, r.address, r.resource_json, r.agent_info_json)
            .await;
        self.state
            .local_sched_mgr
            .register(&rec.node_id, &rec.address);
        self.state
            .node_manager
            .on_proxy_register(&rec.node_id, &rec.address, &rec.domain_id);
        self.state.rebuild_domain_routes();
        self.publish_ready_agent_count().await;
        let topology = self.state.topology.topology_json();
        Ok(Response::new(RegisterResponse {
            success: true,
            message: "registered".into(),
            domain_address,
            topology,
        }))
    }

    async fn update_resources(
        &self,
        request: Request<UpdateResourcesRequest>,
    ) -> Result<Response<UpdateResourcesResponse>, Status> {
        if !self.state.require_leader() {
            return Err(Status::failed_precondition(
                "not leader: resource updates rejected in passive mode",
            ));
        }
        let r = request.into_inner();
        if r.node_id.is_empty() {
            return Err(Status::invalid_argument("node_id required"));
        }
        let ok = self
            .state
            .topology
            .update_resources(&r.node_id, r.resource_json)
            .await;
        if !ok {
            return Err(Status::not_found("unknown node_id"));
        }
        self.state.node_manager.touch(&r.node_id);
        self.state
            .schedule_decision
            .apply_topology_resources(&self.state.topology);
        Ok(Response::new(UpdateResourcesResponse { success: true }))
    }

    async fn query_agents(
        &self,
        request: Request<QueryAgentsRequest>,
    ) -> Result<Response<QueryAgentsResponse>, Status> {
        let r = request.into_inner();
        if !self.state.require_leader() {
            // C++ slave proxies to master via LiteBus; HTTP/gRPC returns local empty until leader RPC exists.
            return Ok(Response::new(QueryAgentsResponse {
                agents_json: "[]".into(),
            }));
        }
        let agents_json = self.state.topology.list_agents_json(&r.filter);
        Ok(Response::new(QueryAgentsResponse { agents_json }))
    }

    async fn query_agent_count(
        &self,
        _request: Request<QueryAgentCountRequest>,
    ) -> Result<Response<QueryAgentCountResponse>, Status> {
        if !self.state.require_leader() {
            return Ok(Response::new(QueryAgentCountResponse { count: 0 }));
        }
        let count = self.state.topology.agent_count();
        Ok(Response::new(QueryAgentCountResponse { count }))
    }

    async fn evict_agent(
        &self,
        request: Request<EvictAgentRequest>,
    ) -> Result<Response<EvictAgentResponse>, Status> {
        if !self.state.require_leader() {
            return Err(Status::failed_precondition(
                "not leader: eviction disabled in passive mode",
            ));
        }
        let r = request.into_inner();
        if r.node_id.is_empty() {
            return Err(Status::invalid_argument("node_id required"));
        }
        let grace = Duration::from_secs(self.state.config.grace_period_seconds.max(1) as u64);
        let _ = self
            .state
            .local_sched_mgr
            .evict_agent_with_ack(&r.node_id, &r.reason, grace)
            .await;
        let ok = self.state.topology.evict(&r.node_id).await;
        if ok {
            self.state.node_manager.remove(&r.node_id);
            self.state
                .schedule_decision
                .apply_topology_resources(&self.state.topology);
        }
        Ok(Response::new(EvictAgentResponse { success: ok }))
    }
}
