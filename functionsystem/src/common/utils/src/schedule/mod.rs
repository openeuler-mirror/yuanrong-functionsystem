//! Scheduling decision framework (Rust port of `functionsystem/src/common/schedule_decision/`).
//!
//! Used by global and domain schedulers: queues, performers, preemption, and recording.

mod performer;
mod preemption;
pub mod queue;
mod recorder;
pub mod scheduler;
pub mod types;

pub use performer::{
    AggregatedSchedulePerformer, GroupSchedulePerformer, InstanceSchedulePerformer, NoopSchedulePerformer,
    PreemptInstancesFn, SchedulePerformer,
};
pub use types::AllocateType;
pub use preemption::{PreemptResult, PreemptableUnit, PreemptionController};
pub use queue::{
    wrap_aggregated, wrap_group, wrap_item, AggregatedQueue, AggregatedStrategy, DynQueueItem,
    QueueKind, ScheduleQueue, TimeSortedQueue,
};
pub use recorder::ScheduleRecorder;
pub use scheduler::{
    PriorityScheduler, ScheduleStrategy, Scheduler, SchedulerDiscipline, QueueStatus,
};
pub use types::{
    AggregatedItem, GroupItem, GroupSchedulePolicy, GroupScheduleResult, GroupSpec, InstanceDescriptor,
    InstanceItem, PreAllocContext, PriorityPolicyType, QueueItem, QueueItemType, RangeOpt,
    ResourceUnitDescriptor, ResourceViewInfo, ScheduleResult, ScheduleType,
};
