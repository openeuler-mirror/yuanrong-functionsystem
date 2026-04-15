use std::sync::Arc;
use std::time::{Duration, Instant};

use yr_proto::internal::ScheduleRequest;

use crate::config::DomainSchedulerConfig;
use crate::function_meta::parse_function_schedule_meta;
use crate::nodes::LocalNodeManager;
use crate::resource_view::ResourceView;
use crate::schedule_decision::{PreemptionController, ScheduleQueue, ScheduleRecorder};
use crate::scheduler_framework::{default_plugin_register, NodeInfo, ScheduleContext, SchedulerFramework};

pub use crate::scheduler_framework::NodeInfo as CandidateNode;

pub struct SchedulingEngine {
    config: Arc<DomainSchedulerConfig>,
    resource_view: Arc<ResourceView>,
    nodes: Arc<LocalNodeManager>,
    framework: SchedulerFramework,
    pending: ScheduleQueue,
    pub recorder: Arc<ScheduleRecorder>,
    pub preemption: Arc<PreemptionController>,
}

impl SchedulingEngine {
    pub fn new(
        config: Arc<DomainSchedulerConfig>,
        resource_view: Arc<ResourceView>,
        nodes: Arc<LocalNodeManager>,
    ) -> Self {
        let reg = default_plugin_register();
        Self {
            config: config.clone(),
            resource_view,
            nodes,
            framework: SchedulerFramework::from_register(&reg),
            pending: ScheduleQueue::new(),
            recorder: Arc::new(ScheduleRecorder::new(256)),
            preemption: Arc::new(PreemptionController::new()),
        }
    }

    fn schedule_ttl(&self, req: &ScheduleRequest) -> Duration {
        let ms = if req.schedule_timeout_ms > 0 {
            req.schedule_timeout_ms as u64
        } else {
            120_000
        };
        Duration::from_millis(ms)
    }

    fn clamp_priority(&self, p: i32) -> i32 {
        p.clamp(0, self.config.max_priority)
    }

    /// Pick best healthy node; does not reserve or call remote.
    pub fn select_node(&self, req: &ScheduleRequest, exclude_source: bool) -> Option<NodeInfo> {
        let candidates = self.nodes.healthy_node_infos();
        if candidates.is_empty() {
            self.recorder.record(
                &req.request_id,
                &req.function_name,
                None,
                "no_candidates",
                "no healthy local nodes",
            );
            return None;
        }
        let exclude = if exclude_source {
            let s = req.source_node_id.as_str();
            if s.is_empty() {
                None
            } else {
                Some(s)
            }
        } else {
            None
        };
        let meta = parse_function_schedule_meta(req);
        let ctx = ScheduleContext {
            resource_view: &self.resource_view,
            exclude_node_id: exclude,
            function_meta: Some(&meta),
        };
        let picked = self.framework.select_best(&ctx, req, &candidates);
        if picked.is_none() {
            self.recorder.record(
                &req.request_id,
                &req.function_name,
                None,
                "no_fit",
                "all nodes failed filter pipeline",
            );
            if self.config.enable_preemption {
                tracing::warn!(
                    request_id = %req.request_id,
                    "no fit; enable preemption and retry from service layer with eviction"
                );
            }
        }
        picked
    }

    pub fn pending_len(&self) -> usize {
        self.pending.len()
    }

    pub fn pending_snapshot_json(&self) -> serde_json::Value {
        self.pending.snapshot_json()
    }

    pub fn enqueue_pending(&self, mut req: ScheduleRequest) {
        let ttl = self.schedule_ttl(&req);
        let deadline = Instant::now() + ttl;
        let p = self.clamp_priority(req.priority);
        req.priority = p;
        self.pending.enqueue(req, p, deadline);
    }

    pub fn drain_one_ready(&self) -> Option<ScheduleRequest> {
        self.pending.pop_ready(Instant::now())
    }
}
