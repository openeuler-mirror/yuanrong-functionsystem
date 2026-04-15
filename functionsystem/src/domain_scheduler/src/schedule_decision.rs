//! Schedule decision pipeline: queues, performers, preemption bookkeeping, decision log.

use std::cmp::Ordering;
use std::collections::{BinaryHeap, VecDeque};
use std::sync::atomic::{AtomicU64, Ordering as AtomicOrdering};
use std::time::Instant;

use dashmap::DashMap;
use parking_lot::Mutex;
use tracing::{debug, warn};
use yr_proto::internal::ScheduleRequest;

use crate::nodes::LocalNodeManager;
use crate::resource_view::ResourceView;
use crate::scheduler_framework::NodeInfo;

static DECISION_SEQ: AtomicU64 = AtomicU64::new(1);

#[derive(Debug, Clone)]
pub struct RecordedDecision {
    pub seq: u64,
    pub at: Instant,
    pub request_id: String,
    pub function_name: String,
    pub chosen_node: Option<String>,
    pub outcome: String,
    pub detail: String,
}

/// Ring buffer of recent scheduling decisions for debugging (C++ `ScheduleRecorder`).
pub struct ScheduleRecorder {
    cap: usize,
    q: Mutex<VecDeque<RecordedDecision>>,
}

impl ScheduleRecorder {
    pub fn new(cap: usize) -> Self {
        Self {
            cap: cap.max(16),
            q: Mutex::new(VecDeque::new()),
        }
    }

    pub fn record(
        &self,
        request_id: &str,
        function_name: &str,
        chosen_node: Option<&str>,
        outcome: &str,
        detail: &str,
    ) {
        let seq = DECISION_SEQ.fetch_add(1, AtomicOrdering::Relaxed);
        let mut g = self.q.lock();
        if g.len() >= self.cap {
            g.pop_front();
        }
        g.push_back(RecordedDecision {
            seq,
            at: Instant::now(),
            request_id: request_id.to_string(),
            function_name: function_name.to_string(),
            chosen_node: chosen_node.map(|s| s.to_string()),
            outcome: outcome.to_string(),
            detail: detail.to_string(),
        });
    }

    pub fn snapshot_json(&self) -> serde_json::Value {
        let g = self.q.lock();
        serde_json::json!(g
            .iter()
            .rev()
            .take(50)
            .map(|r| {
                serde_json::json!({
                    "seq": r.seq,
                    "request_id": r.request_id,
                    "function_name": r.function_name,
                    "chosen_node": r.chosen_node,
                    "outcome": r.outcome,
                    "detail": r.detail,
                })
            })
            .collect::<Vec<_>>())
    }
}

/// Priority queue with FIFO tie-break within the same priority (fairness hint).
#[derive(Debug)]
struct QueueEntry {
    priority: i32,
    seq: u64,
    deadline: Instant,
    request: ScheduleRequest,
}

impl Eq for QueueEntry {}

impl PartialEq for QueueEntry {
    fn eq(&self, other: &Self) -> bool {
        self.priority == other.priority && self.seq == other.seq
    }
}

impl Ord for QueueEntry {
    fn cmp(&self, other: &Self) -> Ordering {
        self.priority
            .cmp(&other.priority)
            .then_with(|| self.seq.cmp(&other.seq))
    }
}

impl PartialOrd for QueueEntry {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

/// Main pending single-schedule queue (C++ `ScheduleQueue`).
pub struct ScheduleQueue {
    heap: Mutex<BinaryHeap<QueueEntry>>,
    seq: AtomicU64,
}

impl ScheduleQueue {
    pub fn new() -> Self {
        Self {
            heap: Mutex::new(BinaryHeap::new()),
            seq: AtomicU64::new(1),
        }
    }

    pub fn enqueue(&self, mut req: ScheduleRequest, priority: i32, deadline: Instant) {
        let seq = self.seq.fetch_add(1, AtomicOrdering::Relaxed);
        req.priority = priority;
        self.heap.lock().push(QueueEntry {
            priority,
            seq,
            deadline,
            request: req,
        });
    }

    pub fn pop_ready(&self, now: Instant) -> Option<ScheduleRequest> {
        let mut h = self.heap.lock();
        let drained: Vec<QueueEntry> = h.drain().collect();
        let mut kept = Vec::new();
        for e in drained {
            if now > e.deadline {
                debug!(request_id = %e.request.request_id, "schedule queue entry expired");
                continue;
            }
            kept.push(e);
        }
        *h = BinaryHeap::from_iter(kept);
        h.pop().map(|e| e.request)
    }

    pub fn len(&self) -> usize {
        self.heap.lock().len()
    }

    pub fn snapshot_json(&self) -> serde_json::Value {
        let guard = self.heap.lock();
        let mut rows: Vec<_> = guard
            .iter()
            .map(|p| {
                (
                    p.priority,
                    p.seq,
                    p.request.request_id.clone(),
                    p.request.function_name.clone(),
                    p.deadline,
                )
            })
            .collect();
        drop(guard);
        rows.sort_by(|a, b| b.0.cmp(&a.0).then_with(|| b.1.cmp(&a.1)));
        serde_json::json!(rows
            .into_iter()
            .map(|(priority, _seq, request_id, function, deadline)| {
                serde_json::json!({
                    "request_id": request_id,
                    "priority": priority,
                    "deadline_ms_remaining": deadline.saturating_duration_since(Instant::now()).as_millis() as u64,
                    "function": function,
                })
            })
            .collect::<Vec<_>>())
    }
}

/// Batch scheduling queue (C++ `AggregatedQueue`).
pub struct AggregatedQueue {
    inner: Mutex<VecDeque<Vec<ScheduleRequest>>>,
}

impl AggregatedQueue {
    pub fn new() -> Self {
        Self {
            inner: Mutex::new(VecDeque::new()),
        }
    }

    pub fn push_batch(&self, batch: Vec<ScheduleRequest>) {
        if !batch.is_empty() {
            self.inner.lock().push_back(batch);
        }
    }

    pub fn pop_batch(&self) -> Option<Vec<ScheduleRequest>> {
        self.inner.lock().pop_front()
    }

    pub fn len_batches(&self) -> usize {
        self.inner.lock().len()
    }
}

/// Deadline-ordered queue without requiring `Ord` on `ScheduleRequest` (C++ `TimeSortedQueue`).
pub struct TimeSortedQueue {
    items: Mutex<Vec<(Instant, u64, ScheduleRequest)>>,
    seq: AtomicU64,
}

impl TimeSortedQueue {
    pub fn new() -> Self {
        Self {
            items: Mutex::new(Vec::new()),
            seq: AtomicU64::new(1),
        }
    }

    pub fn push(&self, deadline: Instant, req: ScheduleRequest) {
        let s = self.seq.fetch_add(1, AtomicOrdering::Relaxed);
        self.items.lock().push((deadline, s, req));
    }

    /// Remove all items whose deadline is <= `now`, earliest first.
    pub fn drain_due(&self, now: Instant) -> Vec<ScheduleRequest> {
        let mut g = self.items.lock();
        let mut keep = Vec::new();
        let mut due = Vec::new();
        for (d, s, req) in g.drain(..) {
            if d <= now {
                due.push((d, s, req));
            } else {
                keep.push((d, s, req));
            }
        }
        *g = keep;
        due.sort_by(|a, b| a.0.cmp(&b.0).then_with(|| a.1.cmp(&b.1)));
        due.into_iter().map(|(_, _, r)| r).collect()
    }
}

/// Executes a concrete schedule step (hook for extensions / tests).
pub trait SchedulePerformer: Send + Sync {
    fn name(&self) -> &'static str;
    /// Returns true if placement should be considered successful.
    fn on_node_selected(
        &self,
        request_id: &str,
        node: &NodeInfo,
        req: &ScheduleRequest,
    ) -> Result<(), String>;
}

pub struct LoggingSchedulePerformer;

impl SchedulePerformer for LoggingSchedulePerformer {
    fn name(&self) -> &'static str {
        "LoggingSchedulePerformer"
    }

    fn on_node_selected(
        &self,
        request_id: &str,
        node: &NodeInfo,
        req: &ScheduleRequest,
    ) -> Result<(), String> {
        tracing::debug!(
            %request_id,
            node_id = %node.node_id,
            function = %req.function_name,
            "schedule performer: node selected"
        );
        Ok(())
    }
}

#[derive(Debug, Clone)]
pub struct TrackedInstance {
    pub instance_id: String,
    pub node_id: String,
    pub priority: i32,
    pub resources: std::collections::HashMap<String, f64>,
}

/// Tracks placed instances for optional preemption (C++ `PreemptionController` subset).
pub struct PreemptionController {
    by_node: DashMap<String, Vec<TrackedInstance>>,
}

impl PreemptionController {
    pub fn new() -> Self {
        Self {
            by_node: DashMap::new(),
        }
    }

    pub fn record_placement(&self, inst: TrackedInstance) {
        self.by_node
            .entry(inst.node_id.clone())
            .or_default()
            .push(inst);
    }

    pub fn remove_instance(&self, node_id: &str, instance_id: &str) {
        if let Some(mut v) = self.by_node.get_mut(node_id) {
            v.retain(|i| i.instance_id != instance_id);
        }
    }

    /// Pick lower-priority instance IDs on `node_id` that could be evicted for `min_priority`.
    pub fn eviction_candidates(&self, node_id: &str, min_priority: i32) -> Vec<String> {
        let Some(v) = self.by_node.get(node_id) else {
            return vec![];
        };
        let mut ids: Vec<String> = v
            .iter()
            .filter(|i| i.priority < min_priority)
            .map(|i| i.instance_id.clone())
            .collect();
        ids.sort();
        ids
    }

    /// Evict via local scheduler RPC; release reservations handled by caller.
    pub async fn try_preempt_for_schedule(
        &self,
        nodes: &LocalNodeManager,
        resource_view: &ResourceView,
        node_id: &str,
        incoming_priority: i32,
        reason: &str,
    ) -> bool {
        let ids = self.eviction_candidates(node_id, incoming_priority);
        if ids.is_empty() {
            return false;
        }
        match nodes.evict_instances(node_id, &ids, reason).await {
            Ok(resp) => {
                if resp.success {
                    for id in &resp.evicted_ids {
                        self.remove_instance(node_id, id);
                        resource_view.release_instance_usage(node_id, id);
                    }
                    true
                } else {
                    false
                }
            }
            Err(e) => {
                warn!(%node_id, error = %e, "preemption evict RPC failed");
                false
            }
        }
    }
}

impl Default for PreemptionController {
    fn default() -> Self {
        Self::new()
    }
}
