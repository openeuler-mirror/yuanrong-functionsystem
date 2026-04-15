//! `Framework` / `FrameworkImpl` (`framework.h`, `framework_impl.cpp`).

use super::{NodeScore, PolicyPlugin, PolicyType, PreFilterResult, ScheduleResults};
use crate::schedule_plugin::constants::{
    DEFAULT_SCORER_NAME, DISK_SCORER_NAME, LABEL_AFFINITY_SCORER_NAME,
    RELAXED_LABEL_AFFINITY_SCORER_NAME, STRICT_LABEL_AFFINITY_SCORER_NAME,
};
use crate::status::{Status, StatusCode};
use std::collections::{BTreeMap, BinaryHeap};
use yr_proto::resources::{InstanceInfo, ResourceUnit};

fn default_score_weights() -> BTreeMap<String, f64> {
    BTreeMap::from([
        (DEFAULT_SCORER_NAME.to_string(), 1.0),
        ("DefaultHeterogeneousScorer".to_string(), 1.0),
        (DISK_SCORER_NAME.to_string(), 1.0),
        (LABEL_AFFINITY_SCORER_NAME.to_string(), 100.0),
        (RELAXED_LABEL_AFFINITY_SCORER_NAME.to_string(), 100.0),
        (STRICT_LABEL_AFFINITY_SCORER_NAME.to_string(), 100.0),
    ])
}

pub trait Framework {
    fn register_policy(&mut self, plugin: PolicyPlugin) -> bool;
    fn unregister_policy(&mut self, name: &str) -> bool;
    fn select_feasible(
        &mut self,
        ctx: &mut dyn super::ScheduleContext,
        instance: &InstanceInfo,
        resource_unit: &ResourceUnit,
        expected_feasible: u32,
    ) -> ScheduleResults;
}

pub struct FrameworkImpl {
    score_plugin_weight: BTreeMap<String, f64>,
    plugins: BTreeMap<PolicyType, BTreeMap<String, PolicyPlugin>>,
    lately_selected: String,
    relaxed: i32,
}

impl Default for FrameworkImpl {
    fn default() -> Self {
        Self::new(-1)
    }
}

impl FrameworkImpl {
    pub fn new(relaxed: i32) -> Self {
        Self {
            score_plugin_weight: default_score_weights(),
            plugins: BTreeMap::new(),
            lately_selected: String::new(),
            relaxed,
        }
    }

    fn is_reach_relaxed(&self, feasible: &BinaryHeap<NodeScore>, expected_feasible: u32) -> bool {
        if self.relaxed <= 0 {
            return false;
        }
        let need = std::cmp::max(self.relaxed as u32, expected_feasible);
        feasible.len() >= need as usize
    }

    fn run_prefilter(
        &self,
        ctx: &mut dyn super::ScheduleContext,
        instance: &InstanceInfo,
        resource_unit: &ResourceUnit,
    ) -> Option<Box<dyn PreFilterResult>> {
        let map = self.plugins.get(&PolicyType::PreFilter)?;
        for (_, p) in map {
            let PolicyPlugin::PreFilter(pre) = p else {
                continue;
            };
            if !pre.prefilter_matched(instance) {
                continue;
            }
            return Some(pre.pre_filter(ctx, instance, resource_unit));
        }
        None
    }

    fn run_filter(
        &self,
        ctx: &mut dyn super::ScheduleContext,
        instance: &InstanceInfo,
        unit: &ResourceUnit,
    ) -> FilterStatus {
        let Some(policy) = self.plugins.get(&PolicyType::Filter) else {
            return FilterStatus {
                status: Status::new(
                    StatusCode::ErrSchedulePluginConfig,
                    "empty filter plugin, please check --schedule_plugins configure.",
                ),
                is_fatal_err: true,
                available_for_request: 0,
                required: String::new(),
            };
        };
        if policy.is_empty() {
            return FilterStatus {
                status: Status::new(
                    StatusCode::ErrSchedulePluginConfig,
                    "empty filter plugin, please check --schedule_plugins configure.",
                ),
                is_fatal_err: true,
                available_for_request: 0,
                required: String::new(),
            };
        }
        let mut available_for_request: i32 = -1;
        for (_, plug) in policy {
            let PolicyPlugin::Filter(filter) = plug else {
                continue;
            };
            let filtered = filter.filter(ctx, instance, unit);
            if filtered.status.is_ok() {
                if filtered.available_for_request > 0 {
                    available_for_request = if available_for_request == -1 {
                        filtered.available_for_request
                    } else {
                        std::cmp::min(available_for_request, filtered.available_for_request)
                    };
                }
                continue;
            }
            if filtered.is_fatal_err {
                return FilterStatus {
                    status: filtered.status,
                    is_fatal_err: true,
                    available_for_request: 0,
                    required: filtered.required,
                };
            }
            return FilterStatus {
                status: filtered.status,
                is_fatal_err: false,
                available_for_request: 0,
                required: filtered.required,
            };
        }
        FilterStatus {
            status: Status::ok(),
            is_fatal_err: false,
            available_for_request,
            required: String::new(),
        }
    }

    fn run_score(
        &self,
        ctx: &mut dyn super::ScheduleContext,
        instance: &InstanceInfo,
        unit: &ResourceUnit,
    ) -> NodeScore {
        let id = unit.id.clone();
        let mut result = NodeScore::new(id.clone(), 0.0);
        let Some(policy) = self.plugins.get(&PolicyType::Score) else {
            return result;
        };
        if policy.is_empty() {
            return result;
        }
        for (_, plug) in policy {
            let PolicyPlugin::Score(scorer) = plug else {
                continue;
            };
            let name = scorer.plugin_name().to_string();
            let mut plugin_score = scorer.score(ctx, instance, unit);
            let w = self
                .score_plugin_weight
                .get(&name)
                .copied()
                .unwrap_or(1.0);
            plugin_score.score *= w;
            result.merge_from(&plugin_score);
        }
        result
    }
}

struct FilterStatus {
    status: Status,
    is_fatal_err: bool,
    available_for_request: i32,
    required: String,
}

struct AggregatedStatus {
    results: BTreeMap<String, u32>,
    requests: BTreeMap<String, String>,
}

impl AggregatedStatus {
    fn insert(&mut self, status: &Status, request: String) {
        let key = status.message.clone();
        *self.results.entry(key.clone()).or_insert(0) += 1;
        self.requests.entry(key).or_insert(request);
    }

    fn dump(&self, desc: &str) -> String {
        let mut oss = String::new();
        oss.push_str(desc);
        oss.push_str(if self.results.is_empty() {
            ", "
        } else {
            ", The reasons are as follows:\n"
        });
        for (msg, count) in &self.results {
            oss.push_str(&format!("\t{} unit with [{}]", count, msg));
            if let Some(req) = self.requests.get(msg) {
                if !req.is_empty() {
                    oss.push_str(&format!(" requirements: [{}]", req));
                }
            }
            oss.push_str(".\n");
        }
        oss
    }
}

impl Framework for FrameworkImpl {
    fn register_policy(&mut self, plugin: PolicyPlugin) -> bool {
        let ty = plugin.policy_type();
        let name = plugin.plugin_name().to_string();
        let map = self.plugins.entry(ty).or_default();
        let ret = map.insert(name.clone(), plugin).is_none();
        if !ret {
            return false;
        }
        if ty == PolicyType::Score {
            if let Some(w) = default_score_weights().get(&name) {
                self.score_plugin_weight.insert(name.clone(), *w);
            } else {
                self.score_plugin_weight.insert(name, 1.0);
            }
        }
        ret
    }

    fn unregister_policy(&mut self, name: &str) -> bool {
        for (_, map) in &mut self.plugins {
            if map.remove(name).is_some() {
                return true;
            }
        }
        false
    }

    fn select_feasible(
        &mut self,
        ctx: &mut dyn super::ScheduleContext,
        instance: &InstanceInfo,
        resource_unit: &ResourceUnit,
        expected_feasible: u32,
    ) -> ScheduleResults {
        ctx.clear_unfeasible();
        let Some(mut prefiltered) = self.run_prefilter(ctx, instance, resource_unit) else {
            return ScheduleResults {
                code: i32::from(StatusCode::ErrSchedulePluginConfig),
                reason: "invalid prefilter plugin, please check --schedule_plugins configure.".into(),
                sorted_feasible_nodes: BinaryHeap::new(),
            };
        };
        if !prefiltered.status().is_ok() {
            let st = prefiltered.status().clone();
            return ScheduleResults {
                code: i32::from(st.code),
                reason: st.message,
                sorted_feasible_nodes: BinaryHeap::new(),
            };
        }
        let mut sorted: BinaryHeap<NodeScore> = BinaryHeap::new();
        let mut aggregate = AggregatedStatus {
            results: BTreeMap::new(),
            requests: BTreeMap::new(),
        };
        prefiltered.reset(&self.lately_selected);
        while !prefiltered.end() && !self.is_reach_relaxed(&sorted, expected_feasible) {
            let cur = prefiltered.current().to_string();
            if cur.is_empty() {
                prefiltered.next();
                continue;
            }
            let Some(unit) = resource_unit.fragment.get(&cur) else {
                prefiltered.next();
                continue;
            };
            if unit.status != 0 {
                aggregate.insert(
                    &Status::new(
                        StatusCode::ResourceNotEnough,
                        "unavailable to schedule, the status of resource unit is non-NORMAL",
                    ),
                    String::new(),
                );
                prefiltered.next();
                continue;
            }
            let filter_status = self.run_filter(ctx, instance, unit);
            if filter_status.status.is_error() {
                if filter_status.is_fatal_err {
                    return ScheduleResults {
                        code: i32::from(filter_status.status.code),
                        reason: filter_status.status.message,
                        sorted_feasible_nodes: BinaryHeap::new(),
                    };
                }
                aggregate.insert(&filter_status.status, filter_status.required);
                prefiltered.next();
                continue;
            }
            let mut score = self.run_score(ctx, instance, unit);
            score.available_for_request = filter_status.available_for_request;
            self.lately_selected = unit.id.clone();
            sorted.push(score);
            prefiltered.next();
        }
        if sorted.is_empty() {
            let reason = aggregate.dump("no available resource that meets the request requirements");
            return ScheduleResults {
                code: i32::from(StatusCode::ResourceNotEnough),
                reason,
                sorted_feasible_nodes: BinaryHeap::new(),
            };
        }
        ScheduleResults {
            code: i32::from(StatusCode::Success),
            reason: String::new(),
            sorted_feasible_nodes: sorted,
        }
    }
}
