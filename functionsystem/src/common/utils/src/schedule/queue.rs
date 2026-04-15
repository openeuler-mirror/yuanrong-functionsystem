//! Priority queues: base FIFO per band, time-sorted bands, and aggregated batches.
//!
//! Mirrors C++ `schedule_queue.h`, `time_sorted_queue.h`, and `aggregated_queue.{h,cpp}`.

use std::collections::{BTreeMap, HashMap, HashSet, VecDeque};
use std::sync::{Arc, Mutex};

use crate::status::{Status, StatusCode};

use super::types::{
    AggregatedItem, GroupItem, InstanceItem, QueueItem, QueueItemType,
};

/// Type-erased queue entry (mirrors `std::shared_ptr<QueueItem>` in C++).
pub type DynQueueItem = Arc<dyn QueueItem>;

type DynItem = DynQueueItem;

/// Base priority queue: FIFO within each priority (`ScheduleQueue` in C++).
#[derive(Debug, Clone)]
pub struct ScheduleQueue {
    inner: Arc<Mutex<ScheduleQueueInner>>,
}

#[derive(Debug)]
struct ScheduleQueueInner {
    max_priority: u16,
    queue_map: HashMap<u16, VecDeque<DynItem>>,
    req_index: HashSet<String>,
}

impl ScheduleQueue {
    pub fn new(max_priority: u16) -> Self {
        Self {
            inner: Arc::new(Mutex::new(ScheduleQueueInner {
                max_priority,
                queue_map: HashMap::new(),
                req_index: HashSet::new(),
            })),
        }
    }

    pub fn enqueue(&self, item: DynItem) -> Result<(), Status> {
        let mut g = self.inner.lock().expect("poisoned");
        check_item(&g, &*item)?;
        let id = item.request_id();
        g.req_index.insert(id);
        g.queue_map
            .entry(item.priority())
            .or_default()
            .push_back(item);
        Ok(())
    }

    pub fn front(&self) -> Option<DynItem> {
        let g = self.inner.lock().expect("poisoned");
        for p in (0..=g.max_priority).rev() {
            if let Some(dq) = g.queue_map.get(&p) {
                if let Some(it) = dq.front() {
                    return Some(Arc::clone(it));
                }
            }
        }
        None
    }

    pub fn dequeue(&self) -> Result<(), Status> {
        let mut g = self.inner.lock().expect("poisoned");
        for p in (0..=g.max_priority).rev() {
            let popped = {
                let Some(dq) = g.queue_map.get_mut(&p) else {
                    continue;
                };
                dq.pop_front()
            };
            if let Some(item) = popped {
                let rid = item.request_id();
                g.req_index.remove(&rid);
                if g
                    .queue_map
                    .get(&p)
                    .map(|dq| dq.is_empty())
                    .unwrap_or(true)
                {
                    g.queue_map.remove(&p);
                }
                return Ok(());
            }
        }
        Ok(())
    }

    pub fn is_empty(&self) -> bool {
        self.inner.lock().expect("poisoned").req_index.is_empty()
    }

    pub fn size(&self) -> usize {
        self.inner.lock().expect("poisoned").req_index.len()
    }

    /// Removes a request by id from the queue if present.
    pub fn cancel(&self, request_id: &str) -> bool {
        let mut g = self.inner.lock().expect("poisoned");
        if !g.req_index.remove(request_id) {
            return false;
        }
        for dq in g.queue_map.values_mut() {
            if let Some(pos) = dq.iter().position(|x| x.request_id() == request_id) {
                dq.remove(pos);
                return true;
            }
        }
        true
    }

    pub fn swap(&self, other: &ScheduleQueue) {
        std::mem::swap(
            &mut *self.inner.lock().expect("poisoned"),
            &mut *other.inner.lock().expect("poisoned"),
        );
    }

    pub fn extend(&self, other: &ScheduleQueue) {
        let mut tgt = self.inner.lock().expect("poisoned");
        let mut src = other.inner.lock().expect("poisoned");
        for p in 0..=tgt.max_priority {
            if let Some(dq) = src.queue_map.remove(&p) {
                let mut batch = Vec::new();
                for it in dq {
                    tgt.req_index.insert(it.request_id());
                    batch.push(it);
                }
                let entry = tgt.queue_map.entry(p).or_default();
                for it in batch {
                    entry.push_back(it);
                }
            }
        }
    }
}

fn check_item(inner: &ScheduleQueueInner, item: &dyn QueueItem) -> Result<(), Status> {
    if item.request_id().is_empty() {
        return Err(Status::new(
            StatusCode::ErrParamInvalid,
            "invalid request without id",
        ));
    }
    if item.priority() > inner.max_priority {
        return Err(Status::new(
            StatusCode::ErrParamInvalid,
            "priority of request is greater than maxPriority",
        ));
    }
    Ok(())
}

/// Per-priority map ordered by `(created_timestamp, request_id)` (`TimeSortedQueue` in C++).
#[derive(Debug, Clone)]
pub struct TimeSortedQueue {
    inner: Arc<Mutex<TimeSortedInner>>,
}

#[derive(Debug)]
struct TimeSortedInner {
    max_priority: u16,
    queue_map: HashMap<u16, BTreeMap<(i64, String), DynItem>>,
    req_index: HashSet<String>,
}

impl TimeSortedQueue {
    pub fn new(max_priority: u16) -> Self {
        Self {
            inner: Arc::new(Mutex::new(TimeSortedInner {
                max_priority,
                queue_map: HashMap::new(),
                req_index: HashSet::new(),
            })),
        }
    }

    pub fn enqueue(&self, item: DynItem) -> Result<(), Status> {
        let mut g = self.inner.lock().expect("poisoned");
        check_ts_item(&g, &*item)?;
        let id = item.request_id();
        let ts = item.created_timestamp();
        let pri = item.priority();
        g.req_index.insert(id.clone());
        g.queue_map
            .entry(pri)
            .or_default()
            .insert((ts, id), item);
        Ok(())
    }

    pub fn front(&self) -> Option<DynItem> {
        let g = self.inner.lock().expect("poisoned");
        for p in (0..=g.max_priority).rev() {
            if let Some(m) = g.queue_map.get(&p) {
                if let Some((_, it)) = m.iter().next() {
                    return Some(Arc::clone(it));
                }
            }
        }
        None
    }

    pub fn dequeue(&self) -> Result<(), Status> {
        let mut g = self.inner.lock().expect("poisoned");
        for p in (0..=g.max_priority).rev() {
            let popped = g.queue_map.get_mut(&p).and_then(|m| m.pop_first());
            if let Some(((_ts, id), _item)) = popped {
                g.req_index.remove(&id);
                if g.queue_map.get(&p).map(|m| m.is_empty()).unwrap_or(true) {
                    g.queue_map.remove(&p);
                }
                return Ok(());
            }
        }
        Ok(())
    }

    pub fn is_empty(&self) -> bool {
        self.inner.lock().expect("poisoned").req_index.is_empty()
    }

    pub fn size(&self) -> usize {
        self.inner.lock().expect("poisoned").req_index.len()
    }

    pub fn cancel(&self, request_id: &str) -> bool {
        let mut g = self.inner.lock().expect("poisoned");
        if !g.req_index.remove(request_id) {
            return false;
        }
        for m in g.queue_map.values_mut() {
            let keys: Vec<(i64, String)> = m
                .keys()
                .filter(|(_, id)| id == request_id)
                .cloned()
                .collect();
            for k in keys {
                m.remove(&k);
            }
        }
        true
    }

    pub fn swap(&self, other: &TimeSortedQueue) {
        std::mem::swap(
            &mut *self.inner.lock().expect("poisoned"),
            &mut *other.inner.lock().expect("poisoned"),
        );
    }

    pub fn extend(&self, other: &TimeSortedQueue) {
        let mut tgt = self.inner.lock().expect("poisoned");
        let mut src = other.inner.lock().expect("poisoned");
        for p in 0..=tgt.max_priority {
            if let Some(mut src_m) = src.queue_map.remove(&p) {
                let mut batch = Vec::new();
                while let Some(kv) = src_m.pop_first() {
                    batch.push(kv);
                }
                for (k, it) in batch {
                    tgt.req_index.insert(k.1.clone());
                    tgt.queue_map.entry(p).or_default().insert(k, it);
                }
            }
        }
    }
}

fn check_ts_item(inner: &TimeSortedInner, item: &dyn QueueItem) -> Result<(), Status> {
    if item.request_id().is_empty() {
        return Err(Status::new(
            StatusCode::ErrParamInvalid,
            "invalid request without id",
        ));
    }
    if item.priority() > inner.max_priority {
        return Err(Status::new(
            StatusCode::ErrParamInvalid,
            "priority of request is greater than maxPriority",
        ));
    }
    Ok(())
}

/// Aggregation strategy for instance requests (`AggregatedStrategy` in C++; string constants
/// `no_aggregate` / `strictly` / `relaxed`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum AggregatedStrategy {
    NoAggregate,
    Strict,
    Relax,
}

impl AggregatedStrategy {
    pub fn from_str(s: &str) -> Self {
        match s {
            "strictly" | "strict" => Self::Strict,
            "relaxed" | "relax" => Self::Relax,
            _ => Self::NoAggregate,
        }
    }
}

/// Aggregated per-priority deque (`AggregatedQueue` in C++).
#[derive(Debug, Clone)]
pub struct AggregatedQueue {
    inner: Arc<Mutex<AggregatedInner>>,
}

#[derive(Debug)]
struct AggregatedInner {
    max_priority: u16,
    strategy: AggregatedStrategy,
    queue_size: usize,
    aggregated_reqs: HashMap<u16, VecDeque<DynItem>>,
    aggregated_item_index: HashMap<String, AggregatedItem>,
    front_item: Option<DynItem>,
    front_priority: i16,
}

impl AggregatedQueue {
    pub fn new(max_priority: u16, strategy: AggregatedStrategy) -> Self {
        Self {
            inner: Arc::new(Mutex::new(AggregatedInner {
                max_priority,
                strategy,
                queue_size: 0,
                aggregated_reqs: HashMap::new(),
                aggregated_item_index: HashMap::new(),
                front_item: None,
                front_priority: -1,
            })),
        }
    }

    pub fn strategy(&self) -> AggregatedStrategy {
        self.inner.lock().expect("poisoned").strategy
    }

    fn check_valid(inner: &AggregatedInner, item: &dyn QueueItem) -> Result<(), Status> {
        if item.request_id().is_empty() {
            return Err(Status::new(
                StatusCode::ErrParamInvalid,
                "get instance requestId failed",
            ));
        }
        if item.priority() > inner.max_priority {
            return Err(Status::new(
                StatusCode::ErrParamInvalid,
                "instance priority is greater than maxPriority",
            ));
        }
        Ok(())
    }

    fn is_item_need_aggregate(item: &dyn QueueItem) -> bool {
        if item.item_type() == QueueItemType::Group {
            return false;
        }
        item.as_any()
            .downcast_ref::<InstanceItem>()
            .map(|i| i.aggregate_eligible())
            .unwrap_or(false)
    }

    fn generate_key(instance: &InstanceItem) -> Result<String, Status> {
        let k = instance.generate_aggregate_key();
        if k.is_empty() {
            return Err(Status::new(StatusCode::Failed, "queueItem is invalid"));
        }
        Ok(k)
    }

    pub fn enqueue(&self, item: DynItem) -> Result<(), Status> {
        let mut g = self.inner.lock().expect("poisoned");
        Self::check_valid(&g, &*item)?;
        let priority = item.priority();

        if !Self::is_item_need_aggregate(&*item) {
            g.aggregated_reqs
                .entry(priority)
                .or_default()
                .push_back(item);
            g.queue_size += 1;
            return Ok(());
        }

        let inst = item
            .as_any()
            .downcast_ref::<InstanceItem>()
            .ok_or_else(|| Status::new(StatusCode::Failed, "expected InstanceItem"))?;
        let key_str = Self::generate_key(inst)?;
        let inst = inst.clone();

        match g.strategy {
            AggregatedStrategy::NoAggregate => {
                g.aggregated_reqs
                    .entry(priority)
                    .or_default()
                    .push_back(Arc::new(inst));
                g.queue_size += 1;
            }
            AggregatedStrategy::Strict => {
                let deque = g.aggregated_reqs.entry(priority).or_default();
                let merge_target = deque.back().and_then(|b| {
                    b.as_any()
                        .downcast_ref::<AggregatedItem>()
                        .filter(|ab| ab.aggregated_key() == key_str)
                        .cloned()
                });
                if let Some(ab) = merge_target {
                    ab.push_instance(inst);
                } else {
                    let agg = AggregatedItem::new(key_str.clone(), inst);
                    deque.push_back(Arc::new(agg) as DynItem);
                    g.queue_size += 1;
                }
            }
            AggregatedStrategy::Relax => {
                if let Some(existing) = g.aggregated_item_index.get(&key_str) {
                    existing.push_instance(inst);
                } else {
                    let agg = AggregatedItem::new(key_str.clone(), inst);
                    let clone_for_index = agg.clone();
                    g.aggregated_item_index.insert(key_str, clone_for_index);
                    g.aggregated_reqs
                        .entry(priority)
                        .or_default()
                        .push_back(Arc::new(agg) as DynItem);
                    g.queue_size += 1;
                }
            }
        }

        Ok(())
    }

    pub fn front(&self) -> Option<DynItem> {
        let mut g = self.inner.lock().expect("poisoned");
        if g.queue_size == 0 {
            return None;
        }
        let mut picked: Option<(u16, DynItem)> = None;
        for p in (0..=g.max_priority).rev() {
            if let Some(dq) = g.aggregated_reqs.get(&p) {
                if let Some(it) = dq.front() {
                    picked = Some((p, Arc::clone(it)));
                    break;
                }
            }
        }
        let (p, it) = picked?;
        g.front_item = Some(Arc::clone(&it));
        g.front_priority = p as i16;
        Some(it)
    }

    pub fn dequeue(&self) -> Result<(), Status> {
        let mut g = self.inner.lock().expect("poisoned");
        if g.queue_size == 0 {
            return Err(Status::new(StatusCode::Failed, "queue is empty"));
        }
        if g.front_item.is_none() {
            for p in (0..=g.max_priority).rev() {
                let front = g
                    .aggregated_reqs
                    .get(&p)
                    .and_then(|dq| dq.front().map(Arc::clone));
                if let Some(it) = front {
                    g.front_item = Some(Arc::clone(&it));
                    g.front_priority = p as i16;
                    break;
                }
            }
        }
        let strategy = g.strategy;
        let relax_key = {
            let front = g
                .front_item
                .as_ref()
                .cloned()
                .ok_or_else(|| Status::new(StatusCode::Failed, "front item missing"))?;
            if front.item_type() == QueueItemType::Aggregated {
                let agg = front
                    .as_any()
                    .downcast_ref::<AggregatedItem>()
                    .expect("aggregated");
                if !agg.is_req_queue_empty() {
                    return Err(Status::new(
                        StatusCode::Failed,
                        "aggregateItem.reqQueue is not empty",
                    ));
                }
                if strategy == AggregatedStrategy::Relax {
                    Some(agg.aggregated_key())
                } else {
                    None
                }
            } else {
                None
            }
        };
        if let Some(k) = relax_key {
            g.aggregated_item_index.remove(&k);
        }
        let p = g.front_priority as u16;
        let empty_after = if let Some(dq) = g.aggregated_reqs.get_mut(&p) {
            dq.pop_front();
            dq.is_empty()
        } else {
            false
        };
        g.queue_size = g.queue_size.saturating_sub(1);
        if empty_after {
            g.aggregated_reqs.remove(&p);
        }
        g.front_priority = -1;
        g.front_item = None;
        Ok(())
    }

    pub fn is_empty(&self) -> bool {
        self.inner.lock().expect("poisoned").queue_size == 0
    }

    pub fn size(&self) -> usize {
        self.inner.lock().expect("poisoned").queue_size
    }

    pub fn cancel(&self, request_id: &str) -> bool {
        let mut g = self.inner.lock().expect("poisoned");
        let mut changed = false;
        let priorities: Vec<u16> = g.aggregated_reqs.keys().copied().collect();
        for p in priorities {
            let Some(old) = g.aggregated_reqs.remove(&p) else {
                continue;
            };
            let mut new_dq = VecDeque::new();
            for it in old {
                if it.item_type() != QueueItemType::Aggregated {
                    if it.request_id() == request_id {
                        g.queue_size = g.queue_size.saturating_sub(1);
                        changed = true;
                        continue;
                    }
                    new_dq.push_back(it);
                    continue;
                }
                if let Some(agg) = it.as_any().downcast_ref::<AggregatedItem>() {
                    let had = agg
                        .instance_items_snapshot()
                        .iter()
                        .any(|x| x.request_id() == request_id);
                    if !had {
                        new_dq.push_back(it);
                        continue;
                    }
                    agg.retain_instances(|x| x.request_id() != request_id);
                    if agg.is_req_queue_empty() {
                        g.queue_size = g.queue_size.saturating_sub(1);
                        if g.strategy == AggregatedStrategy::Relax {
                            g.aggregated_item_index.remove(&agg.aggregated_key());
                        }
                        changed = true;
                        continue;
                    }
                    new_dq.push_back(it);
                    changed = true;
                } else {
                    new_dq.push_back(it);
                }
            }
            if !new_dq.is_empty() {
                g.aggregated_reqs.insert(p, new_dq);
            }
        }
        changed
    }

    pub fn swap(&self, other: &AggregatedQueue) {
        let mut a = self.inner.lock().expect("poisoned");
        let mut b = other.inner.lock().expect("poisoned");
        std::mem::swap(&mut a.aggregated_reqs, &mut b.aggregated_reqs);
        std::mem::swap(&mut a.queue_size, &mut b.queue_size);
        if a.strategy == AggregatedStrategy::Relax && b.strategy == AggregatedStrategy::Relax {
            std::mem::swap(&mut a.aggregated_item_index, &mut b.aggregated_item_index);
        }
    }

    pub fn extend(&self, other: &AggregatedQueue) {
        let drained: Vec<DynItem> = {
            let mut src = other.inner.lock().expect("poisoned");
            let mut out = Vec::new();
            for dq in src.aggregated_reqs.values_mut() {
                while let Some(x) = dq.pop_front() {
                    out.push(x);
                }
            }
            src.aggregated_reqs.clear();
            src.queue_size = 0;
            src.front_item = None;
            src.front_priority = -1;
            src.aggregated_item_index.clear();
            out
        };
        for item in drained {
            if item.item_type() != QueueItemType::Aggregated {
                let _ = self.enqueue(item);
            } else if let Some(agg) = item.as_any().downcast_ref::<AggregatedItem>() {
                for inst in agg.instance_items_snapshot() {
                    let _ = self.enqueue(Arc::new(inst));
                }
            }
        }
    }
}

/// Wrap a concrete item as a trait object.
pub fn wrap_item(item: InstanceItem) -> DynItem {
    Arc::new(item)
}

pub fn wrap_group(item: GroupItem) -> DynItem {
    Arc::new(item)
}

pub fn wrap_aggregated(item: AggregatedItem) -> DynItem {
    Arc::new(item)
}

/// Running / pending queue backend (`TimeSortedQueue` vs `AggregatedQueue` in C++ `PriorityScheduler`).
#[derive(Debug, Clone)]
pub enum QueueKind {
    TimeSorted(TimeSortedQueue),
    Aggregated(AggregatedQueue),
}

impl QueueKind {
    pub fn new_pair(max_priority: u16, strat: AggregatedStrategy) -> (Self, Self) {
        if strat == AggregatedStrategy::NoAggregate {
            (
                Self::TimeSorted(TimeSortedQueue::new(max_priority)),
                Self::TimeSorted(TimeSortedQueue::new(max_priority)),
            )
        } else {
            (
                Self::Aggregated(AggregatedQueue::new(max_priority, strat)),
                Self::Aggregated(AggregatedQueue::new(max_priority, strat)),
            )
        }
    }

    pub fn new_empty_pending(max_priority: u16, strat: AggregatedStrategy) -> Self {
        if strat == AggregatedStrategy::NoAggregate {
            Self::TimeSorted(TimeSortedQueue::new(max_priority))
        } else {
            Self::Aggregated(AggregatedQueue::new(max_priority, strat))
        }
    }

    pub fn extend(&self, other: &QueueKind) -> Result<(), Status> {
        match (self, other) {
            (Self::TimeSorted(a), Self::TimeSorted(b)) => {
                a.extend(b);
                Ok(())
            }
            (Self::Aggregated(a), Self::Aggregated(b)) => {
                a.extend(b);
                Ok(())
            }
            _ => Err(Status::new(
                StatusCode::Failed,
                "queue kind mismatch for extend",
            )),
        }
    }

    pub fn enqueue(&self, item: DynQueueItem) -> Result<(), Status> {
        match self {
            Self::TimeSorted(q) => q.enqueue(item),
            Self::Aggregated(q) => q.enqueue(item),
        }
    }

    pub fn front(&self) -> Option<DynQueueItem> {
        match self {
            Self::TimeSorted(q) => q.front(),
            Self::Aggregated(q) => q.front(),
        }
    }

    pub fn dequeue(&self) -> Result<(), Status> {
        match self {
            Self::TimeSorted(q) => q.dequeue(),
            Self::Aggregated(q) => q.dequeue(),
        }
    }

    pub fn is_empty(&self) -> bool {
        match self {
            Self::TimeSorted(q) => q.is_empty(),
            Self::Aggregated(q) => q.is_empty(),
        }
    }

    pub fn cancel(&self, request_id: &str) -> bool {
        match self {
            Self::TimeSorted(q) => q.cancel(request_id),
            Self::Aggregated(q) => q.cancel(request_id),
        }
    }
}
