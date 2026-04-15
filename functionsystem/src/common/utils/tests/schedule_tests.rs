//! Integration tests for `yr_common::schedule`.

use std::sync::Arc;

use yr_common::schedule::{
    wrap_item, AggregatedQueue, AggregatedStrategy, InstanceItem, InstanceSchedulePerformer,
    NoopSchedulePerformer, PreemptResult, PreemptionController, PriorityPolicyType, PriorityScheduler,
    ResourceUnitDescriptor, ScheduleQueue, ScheduleStrategy, TimeSortedQueue,
};
use yr_common::schedule::types::InstanceDescriptor;
use yr_common::schedule::{AllocateType, QueueItemType};
use yr_common::status::{Status, StatusCode};

fn instance(
    id: &str,
    priority: u16,
    ts: i64,
    cpu: f64,
    mem: f64,
    aggregate_eligible: bool,
) -> InstanceItem {
    InstanceItem::new(id, priority, ts, cpu, mem, aggregate_eligible)
}

#[test]
fn schedule_queue_fifo_within_priority() {
    let q = ScheduleQueue::new(1);
    q.enqueue(wrap_item(instance("first", 0, 1, 1.0, 1.0, true)))
        .unwrap();
    q.enqueue(wrap_item(instance("second", 0, 2, 1.0, 1.0, true)))
        .unwrap();
    assert_eq!(q.front().unwrap().request_id(), "first");
    q.dequeue().unwrap();
    assert_eq!(q.front().unwrap().request_id(), "second");
}

#[test]
fn time_sorted_queue_orders_by_timestamp_then_id() {
    let q = TimeSortedQueue::new(2);
    q.enqueue(wrap_item(instance("b", 0, 200, 1.0, 1.0, true)))
        .unwrap();
    q.enqueue(wrap_item(instance("a", 0, 100, 1.0, 1.0, true)))
        .unwrap();
    q.enqueue(wrap_item(instance("c", 0, 100, 1.0, 1.0, true)))
        .unwrap();
    assert_eq!(q.front().unwrap().request_id(), "a");
    q.dequeue().unwrap();
    assert_eq!(q.front().unwrap().request_id(), "c");
    q.dequeue().unwrap();
    assert_eq!(q.front().unwrap().request_id(), "b");
}

#[test]
fn time_sorted_respects_priority_bands() {
    let q = TimeSortedQueue::new(2);
    q.enqueue(wrap_item(instance("low", 0, 10, 1.0, 1.0, true)))
        .unwrap();
    q.enqueue(wrap_item(instance("high", 2, 999, 1.0, 1.0, true)))
        .unwrap();
    assert_eq!(q.front().unwrap().request_id(), "high");
}

#[test]
fn fairness_policy_blocks_duplicate_request_id_until_cleared() {
    let mut sched = PriorityScheduler::new(None, 2, PriorityPolicyType::Fairness, AggregatedStrategy::NoAggregate);
    let a = wrap_item(instance("dup", 0, 1, 1.0, 1.0, true));
    sched.enqueue(a).unwrap();
    let b = wrap_item(instance("dup", 0, 2, 1.0, 1.0, true));
    sched.enqueue(b).unwrap();
    assert!(!sched.check_is_pending_queue_empty());
    assert!(!sched.check_is_running_queue_empty());
}

#[test]
fn preemption_controller_hook_basic_flow() {
    let c = PreemptionController::new();
    c.set_hook(Arc::new(|_ins, _unit| PreemptResult {
        status: Status::ok(),
        unit_id: "u1".into(),
        owner_id: "o1".into(),
        preempted_instances: vec![InstanceDescriptor {
            request_id: "r1".into(),
            instance_id: "i1".into(),
            tenant_id: "t1".into(),
        }],
    }));
    let ins = InstanceDescriptor {
        request_id: "new".into(),
        instance_id: "ni".into(),
        tenant_id: "t1".into(),
    };
    let unit = ResourceUnitDescriptor {
        unit_id: "u1".into(),
        owner_id: "o1".into(),
    };
    let r = c.preempt_decision(&ins, &unit);
    assert!(r.status.is_ok());
    assert_eq!(r.unit_id, "u1");
    assert_eq!(c.take_last_result().unwrap().owner_id, "o1");
}

#[test]
fn aggregated_queue_strict_groups_same_resource_key_at_tail() {
    let q = AggregatedQueue::new(1, AggregatedStrategy::Strict);
    let i1 = instance("g-1", 0, 1, 2.0, 4.0, true);
    let i2 = instance("g-2", 0, 2, 2.0, 4.0, true);
    q.enqueue(wrap_item(i1)).unwrap();
    q.enqueue(wrap_item(i2)).unwrap();
    assert_eq!(q.size(), 1);
    let head = q.front().unwrap();
    assert_eq!(head.item_type(), QueueItemType::Aggregated);
}

#[test]
fn aggregated_queue_no_aggregate_keeps_separate_entries() {
    let q = AggregatedQueue::new(1, AggregatedStrategy::NoAggregate);
    q.enqueue(wrap_item(instance("x", 0, 1, 1.0, 1.0, true)))
        .unwrap();
    q.enqueue(wrap_item(instance("y", 0, 2, 1.0, 1.0, true)))
        .unwrap();
    assert_eq!(q.size(), 2);
}

#[test]
fn aggregated_queue_relax_merges_same_key_across_tail() {
    let q = AggregatedQueue::new(1, AggregatedStrategy::Relax);
    let i1 = instance("r1", 0, 1, 3.0, 6.0, true);
    let i2 = instance("r2", 0, 2, 1.0, 1.0, true);
    let i3 = instance("r3", 0, 3, 3.0, 6.0, true);
    q.enqueue(wrap_item(i1)).unwrap();
    q.enqueue(wrap_item(i2)).unwrap();
    q.enqueue(wrap_item(i3)).unwrap();
    // `r1` and `r3` share an aggregate key and merge into one `AggregatedItem`; `r2` stays separate.
    assert_eq!(q.size(), 2);
}

#[test]
fn priority_scheduler_consumes_instance_with_performer() {
    let mut sched = PriorityScheduler::new(None, 2, PriorityPolicyType::Fifo, AggregatedStrategy::NoAggregate);
    let perf: Arc<dyn InstanceSchedulePerformer> =
        Arc::new(NoopSchedulePerformer::new(AllocateType::PreAllocation));
    sched.register_performers(Some(perf), None, None);
    let item = instance("run-me", 1, 1, 1.0, 1.0, true);
    sched.enqueue(wrap_item(item.clone())).unwrap();
    sched.consume_running_queue().unwrap();
    let r = item.take_schedule_result().expect("result");
    assert_eq!(r.code, 0);
    assert_eq!(r.id, "run-me");
}

#[test]
fn instance_cancel_reason_skips_schedule_slot() {
    let mut sched = PriorityScheduler::new(None, 2, PriorityPolicyType::Fifo, AggregatedStrategy::NoAggregate);
    let perf: Arc<dyn InstanceSchedulePerformer> =
        Arc::new(NoopSchedulePerformer::new(AllocateType::PreAllocation));
    sched.register_performers(Some(perf), None, None);
    let item = instance("canceled", 0, 1, 1.0, 1.0, true);
    item.set_cancel_reason(Some("user abort".into()));
    sched.enqueue(wrap_item(item.clone())).unwrap();
    sched.consume_running_queue().unwrap();
    let r = item.take_schedule_result().expect("failure recorded");
    assert_eq!(r.code, StatusCode::ErrScheduleCanceled as i32);
}
