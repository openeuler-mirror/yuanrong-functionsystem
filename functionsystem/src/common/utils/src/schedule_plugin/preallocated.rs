//! `PreAllocatedContext` and related structs (`preallocated_context.h`).

use crate::scheduler_framework::ScheduleContext;
use std::collections::{HashMap, HashSet};
use yr_proto::messages::PluginContext;
use yr_proto::resources::{Resources, value};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SchedulerLevel {
    Local,
    NonRootDomain,
    RootDomain,
}

#[derive(Debug, Clone)]
pub struct UnitResource {
    pub resource: Resources,
}

#[derive(Debug, Clone)]
pub struct PodInfo {
    pub mono_num: i32,
    pub shared_num: i32,
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct PodSpec {
    pub proportion: String,
    pub mem: String,
}

#[derive(Debug, Clone)]
pub struct PodSpecScore {
    pub capacity_score: f64,
    pub angle_score: f64,
}

#[derive(Debug, Clone)]
pub struct NodeInfos {
    pub pod_spec_with_info: Vec<(PodSpec, PodInfo)>,
    pub score_with_pod_spec: HashMap<i64, PodSpec>,
    pub select_pod_spec: Option<PodSpec>,
    pub select_pod_type: bool,
}

/// Extended scheduling context (`PreAllocatedContext` in C++).
#[derive(Debug)]
pub struct PreAllocatedContext {
    pub scheduler_level: SchedulerLevel,
    pub allocated: HashMap<String, UnitResource>,
    pub conflict_nodes: HashSet<String>,
    /// Nodes tagged infeasible during the current schedule pass (`ScheduleContext::unfeasiblesNode`).
    pub unfeasible_nodes: HashSet<String>,
    pub instance_feasible_pod_spec: HashMap<String, Vec<PodSpec>>,
    pub pre_allocated_selected_function_agent_map: HashMap<String, String>,
    pub pre_allocated_selected_function_agent_set: HashSet<String>,
    pub instance_feasible_node_with_info: HashMap<String, HashMap<String, NodeInfos>>,
    pub plugin_ctx: Option<HashMap<String, PluginContext>>,
    pub allocated_labels: HashMap<String, HashMap<String, value::Counter>>,
    pub request_default_scores: HashMap<String, HashMap<String, i64>>,
    pub all_local_labels: HashMap<String, HashMap<String, value::Counter>>,
    pub all_labels: Option<HashMap<String, value::Counter>>,
}

impl Default for PreAllocatedContext {
    fn default() -> Self {
        Self {
            scheduler_level: SchedulerLevel::Local,
            allocated: HashMap::new(),
            conflict_nodes: HashSet::new(),
            unfeasible_nodes: HashSet::new(),
            instance_feasible_pod_spec: HashMap::new(),
            pre_allocated_selected_function_agent_map: HashMap::new(),
            pre_allocated_selected_function_agent_set: HashSet::new(),
            instance_feasible_node_with_info: HashMap::new(),
            plugin_ctx: Some(HashMap::new()),
            allocated_labels: HashMap::new(),
            request_default_scores: HashMap::new(),
            all_local_labels: HashMap::new(),
            all_labels: None,
        }
    }
}

impl ScheduleContext for PreAllocatedContext {
    fn clear_unfeasible(&mut self) {
        self.unfeasible_nodes.clear();
    }

    fn check_node_feasible(&self, id: &str) -> bool {
        !self.unfeasible_nodes.contains(id)
    }

    fn tag_node_unfeasible(&mut self, id: &str) {
        self.unfeasible_nodes.insert(id.to_string());
    }
}
