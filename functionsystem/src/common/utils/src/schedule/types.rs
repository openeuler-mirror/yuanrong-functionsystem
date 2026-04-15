//! Core scheduling types aligned with C++ `schedule_decision/scheduler_common.h` and `queue_item.h`.

use std::any::Any;
use std::sync::{Arc, Mutex};

/// Result of scheduling a single instance (`ScheduleResult` in C++).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ScheduleResult {
    pub id: String,
    pub code: i32,
    pub reason: String,
    pub real_ids: Vec<i32>,
    pub hetero_product_name: String,
    pub unit_id: String,
}

impl ScheduleResult {
    pub fn new(id: impl Into<String>, code: i32, reason: impl Into<String>) -> Self {
        Self {
            id: id.into(),
            code,
            reason: reason.into(),
            real_ids: Vec::new(),
            hetero_product_name: String::new(),
            unit_id: String::new(),
        }
    }
}

/// Result of gang / group scheduling (`GroupScheduleResult` in C++).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct GroupScheduleResult {
    pub code: i32,
    pub reason: String,
    pub results: Vec<ScheduleResult>,
}

impl GroupScheduleResult {
    pub fn new(code: i32, reason: impl Into<String>, results: Vec<ScheduleResult>) -> Self {
        Self {
            code,
            reason: reason.into(),
            results,
        }
    }
}

/// Workload category for a scheduling operation (instance, group, or aggregated batch).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ScheduleType {
    Instance,
    Group,
    Aggregated,
}

/// How priority bands interact with pending/running queues (`PriorityPolicyType` in C++).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum PriorityPolicyType {
    Fifo,
    Fairness,
}

/// Group placement policy (`GroupSchedulePolicy` in C++).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum GroupSchedulePolicy {
    None,
    Spread,
    StrictSpread,
    Pack,
    StrictPack,
}

/// Range scheduling options on a group (`GroupSpec::RangeOpt` in C++).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct RangeOpt {
    pub is_range: bool,
    pub min: i32,
    pub max: i32,
    pub step: i32,
}

impl Default for RangeOpt {
    fn default() -> Self {
        Self {
            is_range: false,
            min: 0,
            max: 0,
            step: 1,
        }
    }
}

/// Group scheduling specification (`GroupSpec` in C++), without protobuf payloads.
#[derive(Debug, Clone)]
pub struct GroupSpec {
    pub request_ids: Vec<String>,
    pub group_req_id: String,
    pub range_opt: RangeOpt,
    pub priority: bool,
    pub timeout_ms: i64,
    pub group_schedule_policy: GroupSchedulePolicy,
    /// When set, the request is treated as canceled (mirrors `cancelTag` resolved with a reason).
    pub cancel_reason: Option<String>,
}

impl GroupSpec {
    pub fn new(group_req_id: impl Into<String>, request_ids: Vec<String>) -> Self {
        Self {
            request_ids,
            group_req_id: group_req_id.into(),
            range_opt: RangeOpt::default(),
            priority: false,
            timeout_ms: 1,
            group_schedule_policy: GroupSchedulePolicy::None,
            cancel_reason: None,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum QueueItemType {
    Instance,
    Group,
    Aggregated,
}

/// Queue entry abstraction (`QueueItem` in C++).
pub trait QueueItem: std::fmt::Debug + Send + Sync + Any {
    fn as_any(&self) -> &dyn Any;
    fn item_type(&self) -> QueueItemType;
    fn request_id(&self) -> String;
    fn priority(&self) -> u16;
    fn created_timestamp(&self) -> i64;
    fn associate_failure(&self, code: i32, reason: &str);
    fn tag_failure(&self);
    fn has_failed(&self) -> bool;
    /// `Some(reason)` if scheduling was canceled (litebus cancel tag completed).
    fn cancel_reason(&self) -> Option<String>;
}

#[derive(Debug)]
struct InstanceShared {
    request_id: String,
    priority: u16,
    created_timestamp: i64,
    cpu_scalar: f64,
    memory_scalar: f64,
    aggregate_eligible: bool,
    state: Mutex<InstanceState>,
}

#[derive(Debug, Default)]
struct InstanceState {
    has_failed: bool,
    result: Option<ScheduleResult>,
}

/// Single-instance queue item (`InstanceItem` in C++).
#[derive(Debug, Clone)]
pub struct InstanceItem {
    shared: Arc<InstanceShared>,
}

impl InstanceItem {
    pub fn new(
        request_id: impl Into<String>,
        priority: u16,
        created_timestamp: i64,
        cpu_scalar: f64,
        memory_scalar: f64,
        aggregate_eligible: bool,
    ) -> Self {
        Self {
            shared: Arc::new(InstanceShared {
                request_id: request_id.into(),
                priority,
                created_timestamp,
                cpu_scalar,
                memory_scalar,
                aggregate_eligible,
                state: Mutex::new(InstanceState::default()),
            }),
        }
    }

    pub fn set_cancel_reason(&self, reason: Option<String>) {
        let mut s = self.shared.state.lock().expect("poisoned");
        if let Some(r) = reason {
            s.result = Some(ScheduleResult::new(
                self.shared.request_id.clone(),
                crate::status::StatusCode::ErrScheduleCanceled as i32,
                r,
            ));
        }
    }

    pub fn take_schedule_result(&self) -> Option<ScheduleResult> {
        self.shared.state.lock().expect("poisoned").result.take()
    }

    pub fn set_schedule_result(&self, result: ScheduleResult) {
        self.shared.state.lock().expect("poisoned").result = Some(result);
    }

    pub fn cpu_scalar(&self) -> f64 {
        self.shared.cpu_scalar
    }

    pub fn memory_scalar(&self) -> f64 {
        self.shared.memory_scalar
    }

    pub fn aggregate_eligible(&self) -> bool {
        self.shared.aggregate_eligible
    }

    pub fn generate_aggregate_key(&self) -> String {
        format!(
            "priority:{}_CPU:{}_Memory:{}",
            self.shared.priority, self.shared.cpu_scalar, self.shared.memory_scalar
        )
    }
}

impl QueueItem for InstanceItem {
    fn as_any(&self) -> &dyn Any {
        self
    }

    fn item_type(&self) -> QueueItemType {
        QueueItemType::Instance
    }

    fn request_id(&self) -> String {
        self.shared.request_id.clone()
    }

    fn priority(&self) -> u16 {
        self.shared.priority
    }

    fn created_timestamp(&self) -> i64 {
        self.shared.created_timestamp
    }

    fn associate_failure(&self, code: i32, reason: &str) {
        let mut s = self.shared.state.lock().expect("poisoned");
        if s.result.is_none() {
            s.result = Some(ScheduleResult::new(
                self.shared.request_id.clone(),
                code,
                reason.to_string(),
            ));
        }
    }

    fn tag_failure(&self) {
        self.shared.state.lock().expect("poisoned").has_failed = true;
    }

    fn has_failed(&self) -> bool {
        self.shared.state.lock().expect("poisoned").has_failed
    }

    fn cancel_reason(&self) -> Option<String> {
        let s = self.shared.state.lock().expect("poisoned");
        s.result
            .as_ref()
            .filter(|r| {
                r.code == crate::status::StatusCode::ErrScheduleCanceled as i32
            })
            .map(|r| r.reason.clone())
    }
}

#[derive(Debug)]
struct GroupShared {
    group_req_id: String,
    instances: Vec<InstanceItem>,
    range_opt: RangeOpt,
    timeout_ms: i64,
    _group_schedule_policy: GroupSchedulePolicy,
    cancel_reason: Mutex<Option<String>>,
    state: Mutex<GroupState>,
}

#[derive(Debug, Default)]
struct GroupState {
    has_failed: bool,
    result: Option<GroupScheduleResult>,
}

/// Group / gang queue item (`GroupItem` in C++).
#[derive(Debug, Clone)]
pub struct GroupItem {
    shared: Arc<GroupShared>,
}

impl GroupItem {
    pub fn new(
        group_req_id: impl Into<String>,
        instances: Vec<InstanceItem>,
        range_opt: RangeOpt,
        timeout_ms: i64,
        group_schedule_policy: GroupSchedulePolicy,
    ) -> Self {
        Self {
            shared: Arc::new(GroupShared {
                group_req_id: group_req_id.into(),
                instances,
                range_opt,
                timeout_ms,
                _group_schedule_policy: group_schedule_policy,
                cancel_reason: Mutex::new(None),
                state: Mutex::new(GroupState::default()),
            }),
        }
    }

    pub fn instances(&self) -> &[InstanceItem] {
        &self.shared.instances
    }

    pub fn range_opt(&self) -> RangeOpt {
        self.shared.range_opt
    }

    pub fn timeout_ms(&self) -> i64 {
        self.shared.timeout_ms
    }

    pub fn set_cancel_reason(&self, reason: Option<String>) {
        *self.shared.cancel_reason.lock().expect("poisoned") = reason;
    }

    pub fn take_group_result(&self) -> Option<GroupScheduleResult> {
        self.shared.state.lock().expect("poisoned").result.take()
    }

    pub fn set_group_result(&self, result: GroupScheduleResult) {
        self.shared.state.lock().expect("poisoned").result = Some(result);
    }
}

impl QueueItem for GroupItem {
    fn as_any(&self) -> &dyn Any {
        self
    }

    fn item_type(&self) -> QueueItemType {
        QueueItemType::Group
    }

    fn request_id(&self) -> String {
        self.shared.group_req_id.clone()
    }

    fn priority(&self) -> u16 {
        self.shared
            .instances
            .first()
            .map(|i| i.priority())
            .unwrap_or(0)
    }

    fn created_timestamp(&self) -> i64 {
        self.shared
            .instances
            .first()
            .map(|i| i.created_timestamp())
            .unwrap_or(0)
    }

    fn associate_failure(&self, code: i32, reason: &str) {
        let mut s = self.shared.state.lock().expect("poisoned");
        if s.result.is_none() {
            s.result = Some(GroupScheduleResult::new(
                code,
                reason.to_string(),
                vec![],
            ));
        }
    }

    fn tag_failure(&self) {
        self.shared.state.lock().expect("poisoned").has_failed = true;
    }

    fn has_failed(&self) -> bool {
        self.shared.state.lock().expect("poisoned").has_failed
    }

    fn cancel_reason(&self) -> Option<String> {
        self.shared.cancel_reason.lock().expect("poisoned").clone()
    }
}

#[derive(Debug)]
struct AggregatedShared {
    aggregated_key: String,
    queue: Mutex<Vec<InstanceItem>>,
}

/// Aggregated batch of instance items sharing a resource fingerprint (`AggregatedItem` in C++).
#[derive(Debug, Clone)]
pub struct AggregatedItem {
    shared: Arc<AggregatedShared>,
}

impl AggregatedItem {
    pub fn new(aggregated_key: impl Into<String>, first: InstanceItem) -> Self {
        let key = aggregated_key.into();
        Self {
            shared: Arc::new(AggregatedShared {
                aggregated_key: key,
                queue: Mutex::new(vec![first]),
            }),
        }
    }

    pub fn aggregated_key(&self) -> String {
        self.shared.aggregated_key.clone()
    }

    pub fn push_instance(&self, item: InstanceItem) {
        self.shared.queue.lock().expect("poisoned").push(item);
    }

    pub fn pop_front_instance(&self) -> Option<InstanceItem> {
        let mut q = self.shared.queue.lock().expect("poisoned");
        if q.is_empty() {
            None
        } else {
            Some(q.remove(0))
        }
    }

    pub fn is_req_queue_empty(&self) -> bool {
        self.shared.queue.lock().expect("poisoned").is_empty()
    }

    pub fn front_instance(&self) -> Option<InstanceItem> {
        self.shared
            .queue
            .lock()
            .expect("poisoned")
            .first()
            .cloned()
    }

    pub fn len(&self) -> usize {
        self.shared.queue.lock().expect("poisoned").len()
    }

    /// Snapshot of embedded instance items (for queue merge / cancel).
    pub fn instance_items_snapshot(&self) -> Vec<InstanceItem> {
        self.shared.queue.lock().expect("poisoned").clone()
    }

    /// Remove instance entries matching `predicate`; returns whether any were removed.
    pub fn retain_instances(&self, mut predicate: impl FnMut(&InstanceItem) -> bool) -> bool {
        let mut q = self.shared.queue.lock().expect("poisoned");
        let old = q.len();
        q.retain(|x| predicate(x));
        old != q.len()
    }
}

impl QueueItem for AggregatedItem {
    fn as_any(&self) -> &dyn Any {
        self
    }

    fn item_type(&self) -> QueueItemType {
        QueueItemType::Aggregated
    }

    fn request_id(&self) -> String {
        self.front_instance()
            .map(|i| i.request_id())
            .unwrap_or_default()
    }

    fn priority(&self) -> u16 {
        self.front_instance().map(|i| i.priority()).unwrap_or(0)
    }

    fn created_timestamp(&self) -> i64 {
        self.front_instance()
            .map(|i| i.created_timestamp())
            .unwrap_or(0)
    }

    fn associate_failure(&self, _code: i32, _reason: &str) {
        // C++ AggregatedItem::AssociateFailure is intentionally empty.
    }

    fn tag_failure(&self) {}

    fn has_failed(&self) -> bool {
        false
    }

    fn cancel_reason(&self) -> Option<String> {
        None
    }
}

/// Lightweight placeholders for performer hooks (filled in by yr-master / domain-scheduler).
#[derive(Debug, Clone, Default)]
pub struct ResourceViewInfo {
    pub label: String,
}

#[derive(Debug, Clone, Default)]
pub struct PreAllocContext {
    pub scheduler_level: i32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum AllocateType {
    PreAllocation,
    Allocation,
}

/// Minimal instance description for preemption decisions (`resource_view::InstanceInfo` stand-in).
#[derive(Debug, Clone, Default)]
pub struct InstanceDescriptor {
    pub request_id: String,
    pub instance_id: String,
    pub tenant_id: String,
}

/// Minimal resource unit view for preemption (`resource_view::ResourceUnit` stand-in).
#[derive(Debug, Clone, Default)]
pub struct ResourceUnitDescriptor {
    pub unit_id: String,
    pub owner_id: String,
}
