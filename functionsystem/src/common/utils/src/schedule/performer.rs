//! Schedule performers (`schedule_performer.h` and specialized performers in C++).

use std::sync::Arc;

use parking_lot::Mutex as PLMutex;

use crate::status::Status;

use super::preemption::{PreemptResult, PreemptionController};
use super::types::{
    AggregatedItem, AllocateType, GroupItem, GroupScheduleResult, InstanceItem, PreAllocContext,
    QueueItem, ResourceViewInfo, ScheduleResult,
};

/// Callback type for issuing preemption against the cluster (C++ `PreemptInstancesFunc`).
pub type PreemptInstancesFn =
    Arc<dyn Fn(Vec<PreemptResult>) -> Result<(), Status> + Send + Sync>;

/// Common hooks shared by concrete performers (`SchedulePerformer` in C++).
pub trait SchedulePerformer: Send + Sync {
    fn allocate_type(&self) -> AllocateType;
    fn preemption_controller(&self) -> &PreemptionController;
    fn register_preempt_callback(&self, cb: Option<PreemptInstancesFn>);
    fn enable_print_resource_view(&self) -> bool;
    fn set_enable_print_resource_view(&self, _enable: bool) {}
}

/// Schedules a single instance (`InstanceSchedulePerformer` in C++).
pub trait InstanceSchedulePerformer: SchedulePerformer {
    fn do_schedule(
        &self,
        ctx: &PreAllocContext,
        resource: &ResourceViewInfo,
        item: &InstanceItem,
    ) -> ScheduleResult;

    fn rollback(&self, _ctx: &PreAllocContext, _item: &InstanceItem, _result: &ScheduleResult) {}
}

/// Schedules gang / group requests (`GroupSchedulePerformer` in C++).
pub trait GroupSchedulePerformer: SchedulePerformer {
    fn do_schedule(
        &self,
        ctx: &PreAllocContext,
        resource: &ResourceViewInfo,
        item: &GroupItem,
    ) -> GroupScheduleResult;

    fn rollback(
        &self,
        _ctx: &PreAllocContext,
        _item: &GroupItem,
        _result: &GroupScheduleResult,
    ) {
    }
}

/// Drains an aggregated batch (`AggregatedSchedulePerformer` in C++).
pub trait AggregatedSchedulePerformer: SchedulePerformer {
    fn do_schedule(
        &self,
        ctx: &PreAllocContext,
        resource: &ResourceViewInfo,
        item: &AggregatedItem,
    ) -> Vec<ScheduleResult>;
}

/// Minimal stub used in tests; production binaries swap in real performers.
pub struct NoopSchedulePerformer {
    allocate: AllocateType,
    preempt: PreemptionController,
    preempt_cb: PLMutex<Option<PreemptInstancesFn>>,
    print_rv: std::sync::atomic::AtomicBool,
}

impl std::fmt::Debug for NoopSchedulePerformer {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("NoopSchedulePerformer").finish_non_exhaustive()
    }
}

impl NoopSchedulePerformer {
    pub fn new(allocate: AllocateType) -> Self {
        Self {
            allocate,
            preempt: PreemptionController::new(),
            preempt_cb: PLMutex::new(None),
            print_rv: std::sync::atomic::AtomicBool::new(false),
        }
    }
}

impl SchedulePerformer for NoopSchedulePerformer {
    fn allocate_type(&self) -> AllocateType {
        self.allocate
    }

    fn preemption_controller(&self) -> &PreemptionController {
        &self.preempt
    }

    fn register_preempt_callback(&self, cb: Option<PreemptInstancesFn>) {
        *self.preempt_cb.lock() = cb;
    }

    fn enable_print_resource_view(&self) -> bool {
        self.print_rv.load(std::sync::atomic::Ordering::Relaxed)
    }

    fn set_enable_print_resource_view(&self, enable: bool) {
        self.print_rv.store(enable, std::sync::atomic::Ordering::Relaxed);
    }
}

impl InstanceSchedulePerformer for NoopSchedulePerformer {
    fn do_schedule(
        &self,
        _ctx: &PreAllocContext,
        _resource: &ResourceViewInfo,
        item: &InstanceItem,
    ) -> ScheduleResult {
        ScheduleResult::new(item.request_id(), 0, "noop")
    }
}

impl GroupSchedulePerformer for NoopSchedulePerformer {
    fn do_schedule(
        &self,
        _ctx: &PreAllocContext,
        _resource: &ResourceViewInfo,
        _item: &GroupItem,
    ) -> GroupScheduleResult {
        GroupScheduleResult::new(0, "noop", vec![])
    }
}

impl AggregatedSchedulePerformer for NoopSchedulePerformer {
    fn do_schedule(
        &self,
        _ctx: &PreAllocContext,
        _resource: &ResourceViewInfo,
        item: &AggregatedItem,
    ) -> Vec<ScheduleResult> {
        item.instance_items_snapshot()
            .into_iter()
            .map(|i| ScheduleResult::new(i.request_id(), 0, "noop"))
            .collect()
    }
}
