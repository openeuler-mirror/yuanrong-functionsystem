//! Built-in schedule plugins and factory (`schedule_plugin/` in yuanrong-functionsystem).

pub mod affinity_utils;
pub mod constants;
pub mod label_affinity;
pub mod plugin_factory;
pub mod plugins;
pub mod preallocated;
pub mod resource;

pub use affinity_utils::{
    affinity_scorer, anti_affinity_scorer, get_local_node_labels, is_affinity_priority,
    is_node_affinity_scope, need_affinity_scorer, need_optimal_affinity_check,
    required_affinity_filter, required_anti_affinity_filter, RESOURCE_AFFINITY_WEIGHT,
    SCORE_EPSILON, ZERO_SCORE,
};
pub use constants::*;
pub use label_affinity::affinity_ctx_mut;
pub use plugin_factory::{PluginCreator, PluginFactory, PluginRegister};
pub use plugins::{
    register_builtin_plugins, DefaultFilter, DefaultHeterogeneousFilter, DefaultHeterogeneousScorer,
    DefaultPreFilter, DefaultScorer, DiskFilter, DiskScorer, LabelAffinityFilter, LabelAffinityScorer,
    ResourceSelectorFilter,
};
pub use preallocated::{
    NodeInfos, PodInfo, PodSpec, PodSpecScore, PreAllocatedContext, SchedulerLevel, UnitResource,
};
