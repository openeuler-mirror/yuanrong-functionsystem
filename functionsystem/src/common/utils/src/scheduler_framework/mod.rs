//! Scheduler policy plugin framework (port of `scheduler_framework/` in yuanrong-functionsystem).

mod framework_impl;
mod plugin_to_status;
mod pre_filter_result;
mod score_types;

pub use framework_impl::{Framework, FrameworkImpl};
pub use plugin_to_status::PluginToStatus;
pub use pre_filter_result::{MapPreFilterResult, PreFilterResult, SetPreFilterResult};
pub use score_types::{
    NodePluginScores, NodeScore, PluginScore, VectorResourceAllocation, MAX_UNIT_SCORE, MIN_UNIT_SCORE,
};

use crate::status::Status;
use std::collections::{BinaryHeap, HashSet};

/// Policy kinds registered with the framework (matches C++ `PolicyType`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub enum PolicyType {
    Filter,
    Score,
    Bind,
    PreFilter,
    Undefined,
}

/// Scheduling context: tracks nodes tagged infeasible during a schedule pass.
pub trait ScheduleContext: Send + Sync {
    fn clear_unfeasible(&mut self);
    fn check_node_feasible(&self, id: &str) -> bool;
    fn tag_node_unfeasible(&mut self, id: &str);
}

/// Result of a single filter plugin invocation (`Filtered` in C++ `policy.h`).
#[derive(Debug, Clone)]
pub struct Filtered {
    pub status: Status,
    pub is_fatal_err: bool,
    pub available_for_request: i32,
    pub required: String,
}

impl Filtered {
    pub fn ok(available_for_request: i32) -> Self {
        Self {
            status: Status::ok(),
            is_fatal_err: false,
            available_for_request,
            required: String::new(),
        }
    }
}

/// Aggregated filter outcome (`FilterResult` in `framework.h`).
#[derive(Debug, Clone)]
pub struct FilterResult {
    pub status: Status,
    pub feasible_nodes: HashSet<String>,
}

/// Aggregated score outcome (`ScoreResult` in `framework.h`).
#[derive(Debug, Clone)]
pub struct ScoreResult {
    pub status: Status,
    pub node_score_lists: Vec<NodeScore>,
}

/// Single schedule outcome row (`ScheduleResult` in `framework.h`).
#[derive(Debug, Clone)]
pub struct ScheduleResultRow {
    pub id: String,
    pub code: i32,
    pub reason: String,
}

/// Output of `Framework::select_feasible` (`ScheduleResults` in `framework.h`).
#[derive(Debug)]
pub struct ScheduleResults {
    pub code: i32,
    pub reason: String,
    pub sorted_feasible_nodes: BinaryHeap<NodeScore>,
}

/// Registered policy plugin (pre-filter, filter, or score).
pub enum PolicyPlugin {
    PreFilter(Box<dyn PreFilterPlugin>),
    Filter(Box<dyn FilterPlugin>),
    Score(Box<dyn ScorePlugin>),
}

impl PolicyPlugin {
    pub fn plugin_name(&self) -> &str {
        match self {
            PolicyPlugin::PreFilter(p) => p.plugin_name(),
            PolicyPlugin::Filter(p) => p.plugin_name(),
            PolicyPlugin::Score(p) => p.plugin_name(),
        }
    }

    pub fn policy_type(&self) -> PolicyType {
        match self {
            PolicyPlugin::PreFilter(_) => PolicyType::PreFilter,
            PolicyPlugin::Filter(_) => PolicyType::Filter,
            PolicyPlugin::Score(_) => PolicyType::Score,
        }
    }
}

pub trait PreFilterPlugin: Send + Sync {
    fn plugin_name(&self) -> &str;
    fn pre_filter(
        &self,
        ctx: &mut dyn ScheduleContext,
        instance: &yr_proto::resources::InstanceInfo,
        resource_unit: &yr_proto::resources::ResourceUnit,
    ) -> Box<dyn PreFilterResult>;
    fn prefilter_matched(&self, _instance: &yr_proto::resources::InstanceInfo) -> bool {
        true
    }
}

pub trait FilterPlugin: Send + Sync {
    fn plugin_name(&self) -> &str;
    fn filter(
        &self,
        ctx: &mut dyn ScheduleContext,
        instance: &yr_proto::resources::InstanceInfo,
        resource_unit: &yr_proto::resources::ResourceUnit,
    ) -> Filtered;
}

pub trait ScorePlugin: Send + Sync {
    fn plugin_name(&self) -> &str;
    fn score(
        &self,
        ctx: &mut dyn ScheduleContext,
        instance: &yr_proto::resources::InstanceInfo,
        resource_unit: &yr_proto::resources::ResourceUnit,
    ) -> NodeScore;
}
