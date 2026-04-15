//! Orchestrates pre-filter, filter, and score plugins.

use std::cmp::Ordering;

use yr_proto::internal::ScheduleRequest;

use super::policy::{
    FilterPlugin, FilterResult, NodeInfo, PreFilterPlugin, ScheduleContext, ScorePlugin,
};
use crate::function_meta::parse_function_schedule_meta;

pub struct SchedulerFramework {
    prefilters: Vec<std::sync::Arc<dyn PreFilterPlugin>>,
    filters: Vec<std::sync::Arc<dyn FilterPlugin>>,
    scorers: Vec<std::sync::Arc<dyn ScorePlugin>>,
}

impl SchedulerFramework {
    pub fn new(
        prefilters: Vec<std::sync::Arc<dyn PreFilterPlugin>>,
        filters: Vec<std::sync::Arc<dyn FilterPlugin>>,
        scorers: Vec<std::sync::Arc<dyn ScorePlugin>>,
    ) -> Self {
        Self {
            prefilters,
            filters,
            scorers,
        }
    }

    pub fn from_register(reg: &std::sync::Arc<super::policy::PluginRegister>) -> Self {
        Self {
            prefilters: reg.snapshot_prefilters(),
            filters: reg.snapshot_filters(),
            scorers: reg.snapshot_scorers(),
        }
    }

    /// Returns the highest-scoring node after running the full pipeline.
    pub fn select_best<'a>(
        &self,
        ctx: &ScheduleContext<'a>,
        req: &ScheduleRequest,
        nodes: &[NodeInfo],
    ) -> Option<NodeInfo> {
        for pf in &self.prefilters {
            match pf.pre_filter(ctx, req) {
                FilterResult::Pass => {}
                FilterResult::Fail { reason } => {
                    tracing::debug!(plugin = pf.name(), %reason, "pre_filter rejected");
                    return None;
                }
            }
        }

        let meta = parse_function_schedule_meta(req);
        let ctx_with_meta = ScheduleContext {
            resource_view: ctx.resource_view,
            exclude_node_id: ctx.exclude_node_id,
            function_meta: Some(&meta),
        };

        let mut candidates: Vec<NodeInfo> = Vec::new();
        for node in nodes {
            let mut ok = true;
            for f in &self.filters {
                match f.filter(&ctx_with_meta, node, req) {
                    FilterResult::Pass => {}
                    FilterResult::Fail { reason } => {
                        tracing::trace!(
                            plugin = f.name(),
                            node_id = %node.node_id,
                            %reason,
                            "filter rejected node"
                        );
                        ok = false;
                        break;
                    }
                }
            }
            if ok {
                candidates.push(node.clone());
            }
        }

        if candidates.is_empty() {
            return None;
        }

        candidates.into_iter().max_by(|a, b| {
            let sa = self.total_score(&ctx_with_meta, req, a);
            let sb = self.total_score(&ctx_with_meta, req, b);
            sa.partial_cmp(&sb).unwrap_or(Ordering::Equal)
        })
    }

    fn total_score(&self, ctx: &ScheduleContext<'_>, req: &ScheduleRequest, node: &NodeInfo) -> f64 {
        self.scorers
            .iter()
            .map(|s| s.score(ctx, node, req).0)
            .sum()
    }
}
