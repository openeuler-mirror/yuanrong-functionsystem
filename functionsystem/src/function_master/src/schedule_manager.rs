//! Global master scheduling façade: `yr_common::schedule::Scheduler`, built-in plugins, and domain dispatch performers.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

use dashmap::DashMap;
use parking_lot::Mutex as PLMutex;
use tokio::runtime::Handle;
use yr_common::schedule::types::{
    AggregatedItem, AllocateType, GroupItem, GroupSchedulePolicy, GroupScheduleResult, InstanceItem,
    PreAllocContext, RangeOpt, ResourceViewInfo, ScheduleResult,
};
use yr_common::schedule::{
    wrap_group, wrap_item, AggregatedSchedulePerformer, AggregatedStrategy, DynQueueItem,
    GroupSchedulePerformer, InstanceSchedulePerformer, PreemptInstancesFn, PreemptionController,
    PriorityPolicyType, QueueItem, SchedulePerformer, Scheduler, ScheduleStrategy,
};
use yr_common::schedule_plugin::plugins::register_builtin_plugins;
use yr_common::schedule_plugin::resource::{CPU_RESOURCE_NAME, MEMORY_RESOURCE_NAME};
use yr_common::status::StatusCode;
use yr_proto::internal::{
    GroupScheduleRequest, GroupScheduleResponse, ScheduleRequest, ScheduleResponse,
};

use crate::config::MasterConfig;
use crate::domain_sched_mgr::DomainSchedMgr;
use crate::resource_agg::ResourceAggregator;
use crate::topology::TopologyManager;

/// Owns the shared [`Scheduler`], pending protobuf payloads keyed by request / group id, and domain-forwarding performers.
pub struct ScheduleManager {
    scheduler: Arc<Scheduler>,
    max_priority: u16,
    pending_instance: Arc<DashMap<String, ScheduleRequest>>,
    pending_group: Arc<DashMap<String, GroupScheduleRequest>>,
    /// Latest domain RPC outcome per `request_id` (filled by performers during `consume_running_queue`).
    instance_rpc_result: Arc<DashMap<String, ScheduleResponse>>,
    /// Latest domain RPC outcome per `group_id`.
    group_rpc_result: Arc<DashMap<String, GroupScheduleResponse>>,
    performers_wired: AtomicBool,
}

impl ScheduleManager {
    pub fn new(config: &MasterConfig) -> Arc<Self> {
        register_builtin_plugins();
        let aggregated = AggregatedStrategy::from_str(config.aggregated_schedule_strategy.as_str());
        let max_pri = config.sched_max_priority.max(1).min(1024);
        let scheduler = Arc::new(Scheduler::new(
            max_pri,
            PriorityPolicyType::Fifo,
            aggregated,
            None,
        ));
        Arc::new(Self {
            scheduler,
            max_priority: max_pri,
            pending_instance: Arc::new(DashMap::new()),
            pending_group: Arc::new(DashMap::new()),
            instance_rpc_result: Arc::new(DashMap::new()),
            group_rpc_result: Arc::new(DashMap::new()),
            performers_wired: AtomicBool::new(false),
        })
    }

    pub fn scheduler(&self) -> &Arc<Scheduler> {
        &self.scheduler
    }

    pub fn pending_instance_requests(&self) -> &Arc<DashMap<String, ScheduleRequest>> {
        &self.pending_instance
    }

    pub fn pending_group_requests(&self) -> &Arc<DashMap<String, GroupScheduleRequest>> {
        &self.pending_group
    }

    /// Register [`InstanceSchedulePerformer`], [`GroupSchedulePerformer`], and [`AggregatedSchedulePerformer`] that forward to the root domain scheduler (idempotent).
    pub fn wire_domain_performers(
        &self,
        handle: Handle,
        domain: Arc<DomainSchedMgr>,
        topology: Arc<TopologyManager>,
        leader: Arc<std::sync::atomic::AtomicBool>,
    ) {
        if self
            .performers_wired
            .compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst)
            .is_err()
        {
            return;
        }
        let performer: Arc<DomainDispatchPerformer> = Arc::new(DomainDispatchPerformer {
            handle,
            domain,
            topology,
            leader,
            pending_instance: self.pending_instance.clone(),
            pending_group: self.pending_group.clone(),
            instance_rpc_result: self.instance_rpc_result.clone(),
            group_rpc_result: self.group_rpc_result.clone(),
            preempt: PreemptionController::new(),
            preempt_cb: PLMutex::new(None),
            print_rv: std::sync::atomic::AtomicBool::new(false),
        });
        let mut s = self.scheduler.strategy.lock().expect("poisoned scheduler mutex");
        s.register_performers(
            Some(performer.clone()),
            Some(performer.clone()),
            Some(performer),
        );
    }

    fn refresh_resource_view(&self, topology: &TopologyManager) {
        let info = ResourceAggregator::resource_view_for_scheduler(topology);
        let mut s = self.scheduler.strategy.lock().expect("poisoned scheduler mutex");
        s.handle_resource_info_update(info);
    }

    /// Enqueue one instance request, drain the running queue through domain RPC, return the domain response.
    pub fn process_schedule_request(
        &self,
        req: ScheduleRequest,
        topology: &TopologyManager,
    ) -> ScheduleResponse {
        let rid = req.request_id.clone();
        if rid.is_empty() {
            return ScheduleResponse {
                success: false,
                error_code: StatusCode::ErrParamInvalid as i32,
                message: "request_id required".into(),
                ..Default::default()
            };
        }
        self.pending_instance.insert(rid.clone(), req.clone());
        let prio = clamp_priority(req.priority, self.max_priority);
        let ts = now_ms();
        let (cpu, mem) = extract_cpu_mem(&req);
        let item = InstanceItem::new(&rid, prio, ts, cpu, mem, true);
        let wrapped: DynQueueItem = wrap_item(item);
        if let Err(e) = self.scheduler.enqueue(wrapped) {
            self.pending_instance.remove(&rid);
            return ScheduleResponse {
                success: false,
                error_code: StatusCode::Failed as i32,
                message: e.to_string(),
                ..Default::default()
            };
        }
        self.refresh_resource_view(topology);
        {
            let mut s = self.scheduler.strategy.lock().expect("poisoned scheduler mutex");
            s.activate_pending_requests();
            let _ = s.consume_running_queue();
        }
        self.pending_instance.remove(&rid);
        if let Some((_, r)) = self.instance_rpc_result.remove(&rid) {
            return r;
        }
        ScheduleResponse {
            success: false,
            error_code: StatusCode::Failed as i32,
            message: "no schedule result (performers not wired?)".into(),
            ..Default::default()
        }
    }

    /// Enqueue a group schedule batch and drain once.
    pub fn process_group_schedule_request(
        &self,
        req: GroupScheduleRequest,
        topology: &TopologyManager,
    ) -> GroupScheduleResponse {
        let gid = req.group_id.clone();
        if gid.is_empty() {
            return GroupScheduleResponse {
                success: false,
                error_code: StatusCode::ErrParamInvalid as i32,
                message: "group_id required".into(),
                ..Default::default()
            };
        }
        if req.requests.is_empty() {
            return GroupScheduleResponse {
                success: false,
                error_code: StatusCode::ErrParamInvalid as i32,
                message: "empty group requests".into(),
                group_id: gid,
                ..Default::default()
            };
        }
        for sub in &req.requests {
            if !sub.request_id.is_empty() {
                self.pending_instance
                    .insert(sub.request_id.clone(), sub.clone());
            }
        }
        self.pending_group.insert(gid.clone(), req.clone());
        let ts = now_ms();
        let mut inst_items: Vec<InstanceItem> = Vec::new();
        for sub in &req.requests {
            let rid = sub.request_id.clone();
            if rid.is_empty() {
                continue;
            }
            let prio = clamp_priority(sub.priority, self.max_priority);
            let (cpu, mem) = extract_cpu_mem(sub);
            inst_items.push(InstanceItem::new(&rid, prio, ts, cpu, mem, true));
        }
        if inst_items.is_empty() {
            self.pending_group.remove(&gid);
            return GroupScheduleResponse {
                success: false,
                error_code: StatusCode::ErrParamInvalid as i32,
                message: "no valid child request_id".into(),
                group_id: gid,
                ..Default::default()
            };
        }
        let timeout_ms = req.timeout_sec.saturating_mul(1000).max(1);
        let gitem = GroupItem::new(
            gid.clone(),
            inst_items,
            RangeOpt::default(),
            timeout_ms,
            GroupSchedulePolicy::None,
        );
        if let Err(e) = self.scheduler.enqueue(wrap_group(gitem)) {
            self.pending_group.remove(&gid);
            for sub in &req.requests {
                self.pending_instance.remove(&sub.request_id);
            }
            return GroupScheduleResponse {
                success: false,
                error_code: StatusCode::Failed as i32,
                message: e.to_string(),
                group_id: gid,
                ..Default::default()
            };
        }
        self.refresh_resource_view(topology);
        {
            let mut s = self.scheduler.strategy.lock().expect("poisoned scheduler mutex");
            s.activate_pending_requests();
            let _ = s.consume_running_queue();
        }
        self.pending_group.remove(&gid);
        for sub in &req.requests {
            self.pending_instance.remove(&sub.request_id);
        }
        if let Some((_, r)) = self.group_rpc_result.remove(&gid) {
            return r;
        }
        GroupScheduleResponse {
            success: false,
            error_code: StatusCode::Failed as i32,
            message: "no group schedule result (performers not wired?)".into(),
            group_id: gid,
            ..Default::default()
        }
    }
}

fn clamp_priority(p: i32, max_pri: u16) -> u16 {
    let p = p.max(0) as u16;
    p.min(max_pri)
}

fn extract_cpu_mem(req: &ScheduleRequest) -> (f64, f64) {
    let cpu = req
        .required_resources
        .get(CPU_RESOURCE_NAME)
        .copied()
        .unwrap_or(0.0);
    let mem = req
        .required_resources
        .get(MEMORY_RESOURCE_NAME)
        .copied()
        .unwrap_or(0.0);
    (cpu, mem)
}

fn now_ms() -> i64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

struct DomainDispatchPerformer {
    handle: Handle,
    domain: Arc<DomainSchedMgr>,
    topology: Arc<TopologyManager>,
    leader: Arc<std::sync::atomic::AtomicBool>,
    pending_instance: Arc<DashMap<String, ScheduleRequest>>,
    pending_group: Arc<DashMap<String, GroupScheduleRequest>>,
    instance_rpc_result: Arc<DashMap<String, ScheduleResponse>>,
    group_rpc_result: Arc<DashMap<String, GroupScheduleResponse>>,
    preempt: PreemptionController,
    preempt_cb: PLMutex<Option<PreemptInstancesFn>>,
    print_rv: std::sync::atomic::AtomicBool,
}

impl std::fmt::Debug for DomainDispatchPerformer {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("DomainDispatchPerformer")
            .finish_non_exhaustive()
    }
}

impl DomainDispatchPerformer {
    fn forward_instance(&self, req: &ScheduleRequest) -> ScheduleResponse {
        if !self.leader.load(Ordering::SeqCst) {
            return ScheduleResponse {
                success: false,
                error_code: 9,
                message: "not leader: schedule rejected (slave)".into(),
                ..Default::default()
            };
        }
        let Some((root_name, root_addr)) = self.topology.root_domain() else {
            return ScheduleResponse {
                success: false,
                error_code: 5,
                message: "root domain not found".into(),
                ..Default::default()
            };
        };
        match self.handle.block_on(
            self.domain
                .forward_schedule(&root_name, &root_addr, req.clone()),
        ) {
            Ok(r) => r,
            Err(msg) => ScheduleResponse {
                success: false,
                error_code: 5,
                message: msg,
                ..Default::default()
            },
        }
    }

    fn forward_group(&self, req: &GroupScheduleRequest) -> GroupScheduleResponse {
        if !self.leader.load(Ordering::SeqCst) {
            return GroupScheduleResponse {
                success: false,
                error_code: 9,
                message: "not leader: group schedule rejected".into(),
                group_id: req.group_id.clone(),
                ..Default::default()
            };
        }
        let Some((root_name, root_addr)) = self.topology.root_domain() else {
            return GroupScheduleResponse {
                success: false,
                error_code: 5,
                message: "root domain not found".into(),
                group_id: req.group_id.clone(),
                ..Default::default()
            };
        };
        match self.handle.block_on(
            self.domain
                .forward_group_schedule(&root_name, &root_addr, req.clone()),
        ) {
            Ok(r) => r,
            Err(msg) => GroupScheduleResponse {
                success: false,
                error_code: 5,
                message: msg,
                group_id: req.group_id.clone(),
                ..Default::default()
            },
        }
    }
}

impl SchedulePerformer for DomainDispatchPerformer {
    fn allocate_type(&self) -> AllocateType {
        AllocateType::Allocation
    }

    fn preemption_controller(&self) -> &PreemptionController {
        &self.preempt
    }

    fn register_preempt_callback(
        &self,
        cb: Option<PreemptInstancesFn>,
    ) {
        *self.preempt_cb.lock() = cb;
    }

    fn enable_print_resource_view(&self) -> bool {
        self.print_rv
            .load(std::sync::atomic::Ordering::Relaxed)
    }

    fn set_enable_print_resource_view(&self, enable: bool) {
        self.print_rv
            .store(enable, std::sync::atomic::Ordering::Relaxed);
    }
}

impl InstanceSchedulePerformer for DomainDispatchPerformer {
    fn do_schedule(
        &self,
        _ctx: &PreAllocContext,
        _resource: &ResourceViewInfo,
        item: &InstanceItem,
    ) -> ScheduleResult {
        let rid = item.request_id();
        let Some(req) = self
            .pending_instance
            .get(&rid)
            .map(|e| e.value().clone())
        else {
            return ScheduleResult::new(rid, StatusCode::Failed as i32, "missing pending ScheduleRequest");
        };
        let resp = self.forward_instance(&req);
        self.instance_rpc_result.insert(rid.clone(), resp.clone());
        let code = if resp.success { 0 } else { resp.error_code };
        let mut sr = ScheduleResult::new(&rid, code, &resp.message);
        sr.unit_id = resp.instance_id.clone();
        sr
    }
}

impl GroupSchedulePerformer for DomainDispatchPerformer {
    fn do_schedule(
        &self,
        _ctx: &PreAllocContext,
        _resource: &ResourceViewInfo,
        item: &GroupItem,
    ) -> GroupScheduleResult {
        let gid = item.request_id();
        let Some(greq) = self
            .pending_group
            .get(&gid)
            .map(|e| e.value().clone())
        else {
            return GroupScheduleResult::new(
                StatusCode::Failed as i32,
                "missing pending GroupScheduleRequest",
                vec![],
            );
        };
        let resp = self.forward_group(&greq);
        self.group_rpc_result.insert(gid.clone(), resp.clone());
        let code = if resp.success { 0 } else { resp.error_code };
        GroupScheduleResult::new(code, &resp.message, vec![])
    }
}

impl AggregatedSchedulePerformer for DomainDispatchPerformer {
    fn do_schedule(
        &self,
        _ctx: &PreAllocContext,
        _resource: &ResourceViewInfo,
        item: &AggregatedItem,
    ) -> Vec<ScheduleResult> {
        let mut out = Vec::new();
        for inst in item.instance_items_snapshot() {
            let rid = inst.request_id();
            let Some(req) = self
                .pending_instance
                .get(&rid)
                .map(|e| e.value().clone())
            else {
                out.push(ScheduleResult::new(
                    rid,
                    StatusCode::Failed as i32,
                    "missing pending ScheduleRequest",
                ));
                continue;
            };
            let resp = self.forward_instance(&req);
            self.instance_rpc_result.insert(rid.clone(), resp.clone());
            let code = if resp.success { 0 } else { resp.error_code };
            let mut sr = ScheduleResult::new(&rid, code, &resp.message);
            sr.unit_id = resp.instance_id.clone();
            out.push(sr);
        }
        out
    }
}
