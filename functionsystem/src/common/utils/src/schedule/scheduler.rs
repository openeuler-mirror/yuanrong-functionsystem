//! Priority scheduler strategy (`schedule_strategy.h`, `priority_scheduler.{h,cpp}`).

use std::collections::HashSet;
use std::sync::{Arc, Mutex};

use parking_lot::Mutex as PMutex;

use crate::status::{Status, StatusCode};

use super::performer::{
    AggregatedSchedulePerformer, GroupSchedulePerformer, InstanceSchedulePerformer,
};
use super::queue::{AggregatedStrategy, DynQueueItem, QueueKind};
use super::recorder::ScheduleRecorder;
use super::types::{
    AggregatedItem, GroupItem, GroupScheduleResult, InstanceItem, PreAllocContext, PriorityPolicyType,
    QueueItem, QueueItemType, ResourceViewInfo,
};

/// Queue role in the two-queue scheduler (`QueueStatus` in C++).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum QueueStatus {
    Waiting,
    Running,
    Pending,
}

/// Default vs priority discipline (`ScheduleType` in C++ `scheduler_common.h`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum SchedulerDiscipline {
    Default,
    Priority,
}

trait PriorityPolicy: Send + Sync {
    fn can_schedule(&self, item: &dyn QueueItem) -> bool;
    fn prepare_for_scheduling(&self, item: &dyn QueueItem);
    fn store_pending_info(&self, item: &dyn QueueItem);
    fn clear_pending_infos(&self);
    fn release_after_schedule(&self, item: &dyn QueueItem);
}

struct FifoPolicy;

impl PriorityPolicy for FifoPolicy {
    fn can_schedule(&self, _item: &dyn QueueItem) -> bool {
        true
    }
    fn prepare_for_scheduling(&self, _item: &dyn QueueItem) {}
    fn store_pending_info(&self, _item: &dyn QueueItem) {}
    fn clear_pending_infos(&self) {}
    fn release_after_schedule(&self, _item: &dyn QueueItem) {}
}

struct FairnessPolicy {
    running_ids: PMutex<HashSet<String>>,
    pending_ids: PMutex<HashSet<String>>,
}

impl FairnessPolicy {
    fn new() -> Self {
        Self {
            running_ids: PMutex::new(HashSet::new()),
            pending_ids: PMutex::new(HashSet::new()),
        }
    }
}

impl PriorityPolicy for FairnessPolicy {
    fn can_schedule(&self, item: &dyn QueueItem) -> bool {
        let id = item.request_id();
        if self.pending_ids.lock().contains(&id) {
            return false;
        }
        let mut run = self.running_ids.lock();
        if run.contains(&id) {
            false
        } else {
            run.insert(id);
            true
        }
    }
    fn prepare_for_scheduling(&self, _item: &dyn QueueItem) {}
    fn store_pending_info(&self, item: &dyn QueueItem) {
        let id = item.request_id();
        self.running_ids.lock().remove(&id);
        self.pending_ids.lock().insert(id);
    }
    fn clear_pending_infos(&self) {
        self.pending_ids.lock().clear();
    }
    fn release_after_schedule(&self, item: &dyn QueueItem) {
        self.running_ids.lock().remove(&item.request_id());
    }
}

fn make_policy(kind: PriorityPolicyType) -> Arc<dyn PriorityPolicy> {
    match kind {
        PriorityPolicyType::Fifo => Arc::new(FifoPolicy),
        PriorityPolicyType::Fairness => Arc::new(FairnessPolicy::new()),
    }
}

/// Strategy surface (`ScheduleStrategy` in C++).
pub trait ScheduleStrategy: Send {
    fn enqueue(&mut self, item: DynQueueItem) -> Result<(), Status>;
    fn check_is_running_queue_empty(&self) -> bool;
    fn check_is_pending_queue_empty(&self) -> bool;
    fn schedule_discipline(&self) -> SchedulerDiscipline;
    fn consume_running_queue(&mut self) -> Result<(), Status>;
    fn handle_resource_info_update(&mut self, info: ResourceViewInfo);
    fn activate_pending_requests(&mut self);
    fn register_policy(&mut self, _policy_name: &str) -> Result<(), Status> {
        Ok(())
    }
}

/// Main façade tying strategy and preemption (`Scheduler` in C++ `scheduler.h`, split across actors).
#[derive(Debug, Clone)]
pub struct Scheduler {
    pub strategy: Arc<Mutex<PriorityScheduler>>,
    pub preemption: Arc<super::preemption::PreemptionController>,
}

impl Scheduler {
    pub fn new(
        max_priority: u16,
        priority_policy: PriorityPolicyType,
        aggregated: AggregatedStrategy,
        recorder: Option<Arc<ScheduleRecorder>>,
    ) -> Self {
        let preemption = Arc::new(super::preemption::PreemptionController::new());
        Self {
            strategy: Arc::new(Mutex::new(PriorityScheduler::new(
                recorder,
                max_priority,
                priority_policy,
                aggregated,
            ))),
            preemption,
        }
    }

    pub fn enqueue(&self, item: DynQueueItem) -> Result<(), Status> {
        self.strategy.lock().expect("poisoned").enqueue(item)
    }
}

/// Priority / fairness two-queue scheduler (`PriorityScheduler` in C++).
pub struct PriorityScheduler {
    priority_policy: Arc<dyn PriorityPolicy>,
    policy_type: PriorityPolicyType,
    running: QueueKind,
    pending: QueueKind,
    aggregated_strategy: AggregatedStrategy,
    resource_info: ResourceViewInfo,
    pre_ctx: PreAllocContext,
    recorder: Option<Arc<ScheduleRecorder>>,
    max_priority: u16,
    instance_performer: Option<Arc<dyn InstanceSchedulePerformer>>,
    group_performer: Option<Arc<dyn GroupSchedulePerformer>>,
    aggregated_performer: Option<Arc<dyn AggregatedSchedulePerformer>>,
}

impl std::fmt::Debug for PriorityScheduler {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("PriorityScheduler")
            .field("policy_type", &self.policy_type)
            .field("max_priority", &self.max_priority)
            .field("aggregated_strategy", &self.aggregated_strategy)
            .finish_non_exhaustive()
    }
}

impl PriorityScheduler {
    pub fn new(
        recorder: Option<Arc<ScheduleRecorder>>,
        max_priority: u16,
        priority_policy: PriorityPolicyType,
        aggregated_strategy: AggregatedStrategy,
    ) -> Self {
        let (running, pending) = QueueKind::new_pair(max_priority, aggregated_strategy);
        Self {
            priority_policy: make_policy(priority_policy),
            policy_type: priority_policy,
            running,
            pending,
            aggregated_strategy,
            resource_info: ResourceViewInfo::default(),
            pre_ctx: PreAllocContext::default(),
            recorder,
            max_priority,
            instance_performer: None,
            group_performer: None,
            aggregated_performer: None,
        }
    }

    pub fn set_priority_policy_type(&mut self, kind: PriorityPolicyType) {
        self.policy_type = kind;
        self.priority_policy = make_policy(kind);
    }

    pub fn register_performers(
        &mut self,
        instance: Option<Arc<dyn InstanceSchedulePerformer>>,
        group: Option<Arc<dyn GroupSchedulePerformer>>,
        aggregated: Option<Arc<dyn AggregatedSchedulePerformer>>,
    ) {
        self.instance_performer = instance;
        self.group_performer = group;
        self.aggregated_performer = aggregated;
    }

    fn new_empty_pending_queue(&self) -> QueueKind {
        QueueKind::new_empty_pending(self.max_priority, self.aggregated_strategy)
    }
}

impl ScheduleStrategy for PriorityScheduler {
    fn enqueue(&mut self, item: DynQueueItem) -> Result<(), Status> {
        if self.priority_policy.can_schedule(&*item) {
            self.running.enqueue(item)
        } else {
            self.pending.enqueue(item)
        }
    }

    fn check_is_running_queue_empty(&self) -> bool {
        self.running.is_empty()
    }

    fn check_is_pending_queue_empty(&self) -> bool {
        self.pending.is_empty()
    }

    fn schedule_discipline(&self) -> SchedulerDiscipline {
        SchedulerDiscipline::Priority
    }

    fn handle_resource_info_update(&mut self, info: ResourceViewInfo) {
        self.resource_info = info;
        self.pre_ctx = PreAllocContext::default();
    }

    fn activate_pending_requests(&mut self) {
        if self.pending.is_empty() {
            return;
        }
        let _ = self.pending.extend(&self.running);
        std::mem::swap(&mut self.running, &mut self.pending);
        self.pending = self.new_empty_pending_queue();
        self.priority_policy.clear_pending_infos();
    }

    fn consume_running_queue(&mut self) -> Result<(), Status> {
        while !self.running.is_empty() {
            self.do_consume_one()?;
        }
        Ok(())
    }
}

impl PriorityScheduler {
    /// Remove error recorder entry when a failed item leaves the hot path (C++ `EraseRecord`).
    pub fn erase_record_if_failed(&self, item: &dyn QueueItem) {
        if let Some(rec) = &self.recorder {
            if item.has_failed() {
                rec.erase_schedule_err(&item.request_id());
            }
        }
    }

    fn do_consume_one(&mut self) -> Result<(), Status> {
        let item: DynQueueItem = match self.running.front() {
            Some(i) => i,
            None => return Ok(()),
        };

        if let Some(reason) = item.cancel_reason() {
            item.associate_failure(StatusCode::ErrScheduleCanceled as i32, &reason);
            self.priority_policy.release_after_schedule(&*item);
            let _ = self.running.dequeue();
            return Ok(());
        }

        if !self.priority_policy.can_schedule(&*item) {
            self.pending.enqueue(Arc::clone(&item))?;
            let _ = self.running.dequeue();
            self.priority_policy.store_pending_info(&*item);
            return Ok(());
        }

        match item.item_type() {
            QueueItemType::Instance => {
                let instance = item
                    .as_any()
                    .downcast_ref::<InstanceItem>()
                    .ok_or_else(|| Status::new(StatusCode::Failed, "expected InstanceItem"))?
                    .clone();
                let perf = self
                    .instance_performer
                    .as_ref()
                    .ok_or_else(|| Status::new(StatusCode::Failed, "no instance performer"))?;
                self.priority_policy
                    .prepare_for_scheduling(&instance);
                let r = perf.do_schedule(&self.pre_ctx, &self.resource_info, &instance);
                instance.set_schedule_result(r);
                self.erase_record_if_failed(&instance);
                self.priority_policy.release_after_schedule(&instance);
                let _ = self.running.dequeue();
            }
            QueueItemType::Group => {
                let group = item
                    .as_any()
                    .downcast_ref::<GroupItem>()
                    .ok_or_else(|| Status::new(StatusCode::Failed, "expected GroupItem"))?
                    .clone();
                if group.instances().is_empty() {
                    group.set_group_result(GroupScheduleResult::new(0, "", vec![]));
                    self.priority_policy.release_after_schedule(&group);
                    let _ = self.running.dequeue();
                    return Ok(());
                }
                let perf = self
                    .group_performer
                    .as_ref()
                    .ok_or_else(|| Status::new(StatusCode::Failed, "no group performer"))?;
                self.priority_policy.prepare_for_scheduling(&group);
                let r = perf.do_schedule(&self.pre_ctx, &self.resource_info, &group);
                group.set_group_result(r);
                self.erase_record_if_failed(&group);
                self.priority_policy.release_after_schedule(&group);
                let _ = self.running.dequeue();
            }
            QueueItemType::Aggregated => {
                let aggregated = item
                    .as_any()
                    .downcast_ref::<AggregatedItem>()
                    .ok_or_else(|| Status::new(StatusCode::Failed, "expected AggregatedItem"))?
                    .clone();
                let perf = self
                    .aggregated_performer
                    .as_ref()
                    .ok_or_else(|| Status::new(StatusCode::Failed, "no aggregated performer"))?;
                while let Some(inst) = aggregated.front_instance() {
                    if inst.cancel_reason().is_some() {
                        self.priority_policy.release_after_schedule(&inst);
                        aggregated.pop_front_instance();
                        if aggregated.is_req_queue_empty() {
                            let _ = self.running.dequeue();
                            return Ok(());
                        }
                        continue;
                    }
                    break;
                }
                if aggregated.is_req_queue_empty() {
                    let _ = self.running.dequeue();
                    return Ok(());
                }
                let front = aggregated
                    .front_instance()
                    .ok_or_else(|| Status::new(StatusCode::Failed, "aggregated queue empty"))?;
                self.priority_policy.prepare_for_scheduling(&front);
                let results = perf.do_schedule(&self.pre_ctx, &self.resource_info, &aggregated);
                for r in results {
                    let inst = aggregated
                        .pop_front_instance()
                        .ok_or_else(|| Status::new(StatusCode::Failed, "aggregated size mismatch"))?;
                    inst.set_schedule_result(r);
                    self.priority_policy.release_after_schedule(&inst);
                }
                if aggregated.is_req_queue_empty() {
                    let _ = self.running.dequeue()?;
                }
            }
        }

        Ok(())
    }

    pub fn cancel_running(&self, request_id: &str) -> bool {
        self.running.cancel(request_id)
    }

    pub fn cancel_pending(&self, request_id: &str) -> bool {
        self.pending.cancel(request_id)
    }
}
