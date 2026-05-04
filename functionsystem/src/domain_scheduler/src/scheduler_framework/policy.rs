//! Filter / score plugin traits, registration, and default plugin implementations.

use std::collections::HashMap;
use std::sync::{Arc, RwLock};

use yr_proto::internal::ScheduleRequest;

use crate::function_meta::{parse_function_schedule_meta, FunctionScheduleMeta};
use crate::resource_view::ResourceView;

/// Per-schedule context passed through the plugin pipeline.
pub struct ScheduleContext<'a> {
    pub resource_view: &'a ResourceView,
    pub exclude_node_id: Option<&'a str>,
    pub function_meta: Option<&'a FunctionScheduleMeta>,
}

/// Snapshot of a worker node for scheduling plugins.
#[derive(Debug, Clone)]
pub struct NodeInfo {
    pub node_id: String,
    pub address: String,
    pub labels: HashMap<String, String>,
    /// Optional topology / failure-domain hints (from resource_json or agent metadata).
    pub failure_domain: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum FilterResult {
    Pass,
    Fail { reason: String },
}

impl FilterResult {
    pub fn fail(reason: impl Into<String>) -> Self {
        FilterResult::Fail {
            reason: reason.into(),
        }
    }
}

/// Normalized score from a single plugin (higher is better).
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Score(pub f64);

pub trait PreFilterPlugin: Send + Sync {
    fn name(&self) -> &'static str;
    fn pre_filter(&self, ctx: &ScheduleContext<'_>, req: &ScheduleRequest) -> FilterResult;
}

pub trait FilterPlugin: Send + Sync {
    fn name(&self) -> &'static str;
    fn filter(&self, ctx: &ScheduleContext<'_>, node: &NodeInfo, req: &ScheduleRequest) -> FilterResult;
}

pub trait ScorePlugin: Send + Sync {
    fn name(&self) -> &'static str;
    fn score(&self, ctx: &ScheduleContext<'_>, node: &NodeInfo, req: &ScheduleRequest) -> Score;
}

// --- Default plugins ------------------------------------------------------------

pub struct DefaultFilter;

impl FilterPlugin for DefaultFilter {
    fn name(&self) -> &'static str {
        "DefaultFilter"
    }

    fn filter(&self, ctx: &ScheduleContext<'_>, node: &NodeInfo, req: &ScheduleRequest) -> FilterResult {
        if let Some(ex) = ctx.exclude_node_id {
            if ex == node.node_id.as_str() {
                return FilterResult::fail("excluded source node");
            }
        }
        if ctx
            .resource_view
            .has_room_for(&node.node_id, &req.required_resources)
        {
            FilterResult::Pass
        } else {
            FilterResult::fail("insufficient cpu/memory (aggregate resources)")
        }
    }
}

pub struct DefaultScorer;

impl ScorePlugin for DefaultScorer {
    fn name(&self) -> &'static str {
        "DefaultScorer"
    }

    fn score(&self, ctx: &ScheduleContext<'_>, node: &NodeInfo, _req: &ScheduleRequest) -> Score {
        let mut s = ctx.resource_view.free_score(&node.node_id);
        if let Some(u) = ctx.resource_view.snapshot_unit(&node.node_id) {
            let cap_cpu = u.capacity.get("cpu").copied().unwrap_or(0.0).max(1e-6);
            let used_cpu = u.used.get("cpu").copied().unwrap_or(0.0);
            let ratio = 1.0 - (used_cpu / cap_cpu).clamp(0.0, 1.0);
            s += ratio;
        }
        Score(s)
    }
}

fn disk_need(req: &ScheduleRequest) -> HashMap<String, f64> {
    let mut m = HashMap::new();
    for (k, v) in &req.required_resources {
        let kl = k.to_lowercase();
        if kl.contains("disk") || kl.contains("storage") || kl == "ephemeral" {
            m.insert(k.clone(), *v);
        }
    }
    m
}

pub struct DiskFilter;

impl FilterPlugin for DiskFilter {
    fn name(&self) -> &'static str {
        "DiskFilter"
    }

    fn filter(&self, ctx: &ScheduleContext<'_>, node: &NodeInfo, req: &ScheduleRequest) -> FilterResult {
        let need = disk_need(req);
        if need.is_empty() {
            return FilterResult::Pass;
        }
        if ctx.resource_view.has_room_for(&node.node_id, &need) {
            FilterResult::Pass
        } else {
            FilterResult::fail("insufficient disk/storage capacity")
        }
    }
}

pub struct DiskScorer;

impl ScorePlugin for DiskScorer {
    fn name(&self) -> &'static str {
        "DiskScorer"
    }

    fn score(&self, ctx: &ScheduleContext<'_>, node: &NodeInfo, req: &ScheduleRequest) -> Score {
        let need = disk_need(req);
        if need.is_empty() {
            return Score(0.0);
        }
        let Some(u) = ctx.resource_view.snapshot_unit(&node.node_id) else {
            return Score(f64::NEG_INFINITY);
        };
        let mut total = 0.0;
        for k in need.keys() {
            let cap = u.capacity.get(k).copied().unwrap_or(0.0);
            let used = u.used.get(k).copied().unwrap_or(0.0);
            if cap <= 0.0 {
                continue;
            }
            total += ((cap - used).max(0.0)) / cap;
        }
        Score(total)
    }
}

pub struct LabelAffinityFilter;

impl FilterPlugin for LabelAffinityFilter {
    fn name(&self) -> &'static str {
        "LabelAffinityFilter"
    }

    fn filter(&self, _ctx: &ScheduleContext<'_>, node: &NodeInfo, req: &ScheduleRequest) -> FilterResult {
        let meta = parse_function_schedule_meta(req);
        for (k, want) in &meta.affinity_labels {
            match node.labels.get(k) {
                Some(got) if got == want => {}
                _ => return FilterResult::fail(format!("missing affinity label {k}={want}")),
            }
        }
        for (k, avoid) in &meta.anti_affinity_labels {
            if node.labels.get(k) == Some(avoid) {
                return FilterResult::fail(format!("anti-affinity hit on {k}={avoid}"));
            }
        }
        for (k, v) in &req.labels {
            if v.is_empty() {
                continue;
            }
            match node.labels.get(k) {
                Some(got) if got == v => {}
                _ => return FilterResult::fail(format!("request label {k} not satisfied")),
            }
        }
        FilterResult::Pass
    }
}

pub struct LabelAffinityScorer;

impl ScorePlugin for LabelAffinityScorer {
    fn name(&self) -> &'static str {
        "LabelAffinityScorer"
    }

    fn score(&self, _ctx: &ScheduleContext<'_>, node: &NodeInfo, req: &ScheduleRequest) -> Score {
        let mut pts = 0.0;
        let mut total = 0.0;
        for (k, want) in &req.labels {
            if want.is_empty() {
                continue;
            }
            total += 1.0;
            if node.labels.get(k) == Some(want) {
                pts += 1.0;
            }
        }
        if total > 0.0 {
            Score(pts / total)
        } else {
            Score(0.0)
        }
    }
}

/// Selector from `extension["resource_selector"]` JSON: `{ "matchLabels": {..}, "matchExpressions": [ ... ] }`.
pub struct ResourceSelectorFilter;

#[derive(Debug, Clone, Default)]
struct SelectorSpec {
    match_labels: HashMap<String, String>,
    expressions: Vec<SelectorExpr>,
}

#[derive(Debug, Clone)]
enum SelectorExpr {
    In { key: String, values: Vec<String> },
    NotIn { key: String, values: Vec<String> },
    Exists { key: String },
    NotExist { key: String },
}

fn parse_selector_json(s: &str) -> Option<SelectorSpec> {
    let v: serde_json::Value = serde_json::from_str(s).ok()?;
    let mut spec = SelectorSpec::default();
    if let Some(ml) = v.get("matchLabels").and_then(|x| x.as_object()) {
        for (k, val) in ml {
            if let Some(s) = val.as_str() {
                spec.match_labels.insert(k.clone(), s.to_string());
            }
        }
    }
    if let Some(arr) = v.get("matchExpressions").and_then(|x| x.as_array()) {
        for e in arr {
            let key = e.get("key")?.as_str()?.to_string();
            let op = e.get("operator")?.as_str()?.to_lowercase();
            match op.as_str() {
                "in" | "notin" => {
                    let vals = e
                        .get("values")
                        .and_then(|x| x.as_array())
                        .map(|a| {
                            a.iter()
                                .filter_map(|i| i.as_str().map(|s| s.to_string()))
                                .collect::<Vec<_>>()
                        })
                        .unwrap_or_default();
                    if op == "in" {
                        spec.expressions.push(SelectorExpr::In { key, values: vals });
                    } else {
                        spec.expressions
                            .push(SelectorExpr::NotIn { key, values: vals });
                    }
                }
                "exists" => spec.expressions.push(SelectorExpr::Exists { key }),
                "notexist" => spec.expressions.push(SelectorExpr::NotExist { key }),
                _ => {}
            }
        }
    }
    Some(spec)
}

fn selector_for_request(req: &ScheduleRequest) -> Option<SelectorSpec> {
    let raw = req.extension.get("resource_selector")?;
    parse_selector_json(raw)
}

impl FilterPlugin for ResourceSelectorFilter {
    fn name(&self) -> &'static str {
        "ResourceSelectorFilter"
    }

    fn filter(&self, _ctx: &ScheduleContext<'_>, node: &NodeInfo, req: &ScheduleRequest) -> FilterResult {
        let Some(spec) = selector_for_request(req) else {
            return FilterResult::Pass;
        };
        for (k, want) in &spec.match_labels {
            match node.labels.get(k) {
                Some(got) if got == want => {}
                _ => return FilterResult::fail(format!("selector matchLabels {k}")),
            }
        }
        for ex in &spec.expressions {
            match ex {
                SelectorExpr::In { key, values } => {
                    let got = node.labels.get(key).map(|s| s.as_str());
                    let ok = got.is_some_and(|g| values.iter().any(|v| v == g));
                    if !ok {
                        return FilterResult::fail(format!("selector In {key}"));
                    }
                }
                SelectorExpr::NotIn { key, values } => {
                    let got = node.labels.get(key).map(|s| s.as_str());
                    if got.is_some_and(|g| values.iter().any(|v| v == g)) {
                        return FilterResult::fail(format!("selector NotIn {key}"));
                    }
                }
                SelectorExpr::Exists { key } => {
                    if !node.labels.contains_key(key) {
                        return FilterResult::fail(format!("selector Exists {key}"));
                    }
                }
                SelectorExpr::NotExist { key } => {
                    if node.labels.contains_key(key) {
                        return FilterResult::fail(format!("selector NotExist {key}"));
                    }
                }
            }
        }
        FilterResult::Pass
    }
}

pub struct DefaultHeterogeneousScorer;

impl ScorePlugin for DefaultHeterogeneousScorer {
    fn name(&self) -> &'static str {
        "DefaultHeterogeneousScorer"
    }

    fn score(&self, ctx: &ScheduleContext<'_>, node: &NodeInfo, req: &ScheduleRequest) -> Score {
        let meta = parse_function_schedule_meta(req);
        let keys: Vec<String> = if !meta.heterogeneous_keys.is_empty() {
            meta.heterogeneous_keys.clone()
        } else {
            req.required_resources
                .keys()
                .filter(|k| {
                    let l = k.to_lowercase();
                    l.contains("gpu")
                        || l.contains("npu")
                        || l.contains("tpu")
                        || l.contains("ascend")
                        || l.contains("cuda")
                })
                .cloned()
                .collect()
        };
        if keys.is_empty() {
            return Score(0.0);
        }
        let mut sum = 0.0;
        for k in &keys {
            let need = req.required_resources.get(k).copied().unwrap_or(0.0);
            if need <= 0.0 {
                continue;
            }
            if ctx.resource_view.has_room_for(&node.node_id, &HashMap::from([(k.clone(), need)])) {
                let Some(u) = ctx.resource_view.snapshot_unit(&node.node_id) else {
                    continue;
                };
                let cap = u.capacity.get(k).copied().unwrap_or(0.0).max(1e-6);
                let used = u.used.get(k).copied().unwrap_or(0.0);
                sum += ((cap - used).max(0.0) / cap).min(1.0);
            }
        }
        Score(sum)
    }
}

pub struct FailureDomainFilter;

impl FilterPlugin for FailureDomainFilter {
    fn name(&self) -> &'static str {
        "FailureDomainFilter"
    }

    fn filter(&self, _ctx: &ScheduleContext<'_>, node: &NodeInfo, req: &ScheduleRequest) -> FilterResult {
        let meta = parse_function_schedule_meta(req);
        if meta.failure_domains.is_empty() {
            return FilterResult::Pass;
        }
        let dom = node.failure_domain.as_deref().unwrap_or("");
        if dom.is_empty() {
            return FilterResult::fail("node has no failure_domain but request requires one");
        }
        if meta.failure_domains.iter().any(|d| d == dom) {
            FilterResult::Pass
        } else {
            FilterResult::fail("failure_domain mismatch")
        }
    }
}

// --- PluginFactory & PluginRegister ---------------------------------------------

/// Builds plugin instances by name (aligned with C++ `PluginFactory`).
pub struct PluginFactory;

impl PluginFactory {
    pub fn prefilter(name: &str) -> Option<Arc<dyn PreFilterPlugin>> {
        match name {
            _ => None,
        }
    }

    pub fn filter(name: &str) -> Option<Arc<dyn FilterPlugin>> {
        Some(match name {
            "DefaultFilter" | "default_filter" => Arc::new(DefaultFilter),
            "DiskFilter" | "disk_filter" => Arc::new(DiskFilter),
            "LabelAffinityFilter" | "label_affinity_filter" => Arc::new(LabelAffinityFilter),
            "ResourceSelectorFilter" | "resource_selector_filter" => Arc::new(ResourceSelectorFilter),
            "FailureDomainFilter" | "failure_domain_filter" => Arc::new(FailureDomainFilter),
            _ => return None,
        })
    }

    pub fn scorer(name: &str) -> Option<Arc<dyn ScorePlugin>> {
        Some(match name {
            "DefaultScorer" | "default_scorer" => Arc::new(DefaultScorer),
            "DiskScorer" | "disk_scorer" => Arc::new(DiskScorer),
            "LabelAffinityScorer" | "label_affinity_scorer" => Arc::new(LabelAffinityScorer),
            "DefaultHeterogeneousScorer" | "default_heterogeneous_scorer" => {
                Arc::new(DefaultHeterogeneousScorer)
            }
            _ => return None,
        })
    }
}

/// Runtime registration of plugins (aligned with C++ `PluginRegister`).
#[derive(Default)]
pub struct PluginRegister {
    prefilters: RwLock<Vec<(String, Arc<dyn PreFilterPlugin>)>>,
    filters: RwLock<Vec<(String, Arc<dyn FilterPlugin>)>>,
    scorers: RwLock<Vec<(String, Arc<dyn ScorePlugin>)>>,
}

impl PluginRegister {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn register_prefilter(&self, name: impl Into<String>, plugin: Arc<dyn PreFilterPlugin>) {
        self.prefilters.write().expect("poisoned lock").push((name.into(), plugin));
    }

    pub fn register_filter(&self, name: impl Into<String>, plugin: Arc<dyn FilterPlugin>) {
        self.filters.write().expect("poisoned lock").push((name.into(), plugin));
    }

    pub fn register_scorer(&self, name: impl Into<String>, plugin: Arc<dyn ScorePlugin>) {
        self.scorers.write().expect("poisoned lock").push((name.into(), plugin));
    }

    pub fn snapshot_prefilters(&self) -> Vec<Arc<dyn PreFilterPlugin>> {
        self.prefilters
            .read()
            .expect("poisoned lock")
            .iter()
            .map(|(_, p)| p.clone())
            .collect()
    }

    pub fn snapshot_filters(&self) -> Vec<Arc<dyn FilterPlugin>> {
        self.filters
            .read()
            .expect("poisoned lock")
            .iter()
            .map(|(_, p)| p.clone())
            .collect()
    }

    pub fn snapshot_scorers(&self) -> Vec<Arc<dyn ScorePlugin>> {
        self.scorers
            .read()
            .expect("poisoned lock")
            .iter()
            .map(|(_, p)| p.clone())
            .collect()
    }
}

pub fn default_plugin_register() -> Arc<PluginRegister> {
    let r = PluginRegister::new();
    r.register_filter("DefaultFilter", Arc::new(DefaultFilter));
    r.register_filter("FailureDomainFilter", Arc::new(FailureDomainFilter));
    r.register_filter("LabelAffinityFilter", Arc::new(LabelAffinityFilter));
    r.register_filter("ResourceSelectorFilter", Arc::new(ResourceSelectorFilter));
    r.register_filter("DiskFilter", Arc::new(DiskFilter));
    r.register_scorer("DefaultScorer", Arc::new(DefaultScorer));
    r.register_scorer("DiskScorer", Arc::new(DiskScorer));
    r.register_scorer("LabelAffinityScorer", Arc::new(LabelAffinityScorer));
    r.register_scorer(
        "DefaultHeterogeneousScorer",
        Arc::new(DefaultHeterogeneousScorer),
    );
    Arc::new(r)
}
