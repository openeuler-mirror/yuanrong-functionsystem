//! Scheduler plugin pipeline (C++ domain_scheduler framework port).

mod framework;
mod policy;

pub use framework::SchedulerFramework;
pub use policy::{
    default_plugin_register, DefaultFilter, DefaultHeterogeneousScorer, DefaultScorer, DiskFilter,
    DiskScorer, FailureDomainFilter, FilterPlugin, FilterResult, LabelAffinityFilter,
    LabelAffinityScorer, NodeInfo, PluginFactory, PluginRegister, PreFilterPlugin, ResourceSelectorFilter,
    ScheduleContext, Score, ScorePlugin,
};
