//! Preemption decision structures (`preemption_controller.h` in C++).

use std::sync::{Arc, Mutex};

use crate::status::{Status, StatusCode};

use super::types::{InstanceDescriptor, ResourceUnitDescriptor};

/// Outcome of a preemption attempt (`PreemptResult` in C++).
#[derive(Debug, Clone)]
pub struct PreemptResult {
    pub status: Status,
    pub unit_id: String,
    pub owner_id: String,
    pub preempted_instances: Vec<InstanceDescriptor>,
}

impl PreemptResult {
    pub fn no_preemptable() -> Self {
        Self {
            status: Status::new(
                StatusCode::DomainSchedulerNoPreemptableInstance,
                "no instance can be preempted",
            ),
            unit_id: String::new(),
            owner_id: String::new(),
            preempted_instances: Vec::new(),
        }
    }
}

/// Candidate unit considered for preemption (`PreemptableUnit` in C++).
#[derive(Debug, Clone)]
pub struct PreemptableUnit {
    pub score: f64,
    pub unit_id: String,
    pub owner_id: String,
    pub preempted_instances: Vec<InstanceDescriptor>,
    /// Opaque resource fingerprint (C++ `resource_view::Resources`).
    pub preempted_resources_label: String,
}

type PreemptHook = Arc<dyn Fn(&InstanceDescriptor, &ResourceUnitDescriptor) -> PreemptResult + Send + Sync>;

/// Controller that selects preemptable work (`PreemptionController` in C++).
#[derive(Clone)]
pub struct PreemptionController {
    inner: Arc<Mutex<PreemptionInner>>,
}

impl std::fmt::Debug for PreemptionController {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("PreemptionController")
            .finish_non_exhaustive()
    }
}

#[derive(Clone)]
struct PreemptionInner {
    /// When set, overrides default `no_preemptable` (tests and integration).
    hook: Option<PreemptHook>,
    last: Option<PreemptResult>,
}

impl std::fmt::Debug for PreemptionInner {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("PreemptionInner")
            .field("hook_set", &self.hook.is_some())
            .field("last", &self.last)
            .finish()
    }
}

impl PreemptionController {
    pub fn new() -> Self {
        Self {
            inner: Arc::new(Mutex::new(PreemptionInner {
                hook: None,
                last: None,
            })),
        }
    }

    pub fn set_hook(&self, hook: PreemptHook) {
        self.inner.lock().expect("poisoned").hook = Some(hook);
    }

    pub fn clear_hook(&self) {
        self.inner.lock().expect("poisoned").hook = None;
    }

    pub fn take_last_result(&self) -> Option<PreemptResult> {
        self.inner.lock().expect("poisoned").last.take()
    }

    /// C++ `PreemptDecision`: framework fills real logic in yr-master / domain-scheduler.
    pub fn preempt_decision(
        &self,
        instance: &InstanceDescriptor,
        resource_unit: &ResourceUnitDescriptor,
    ) -> PreemptResult {
        let mut g = self.inner.lock().expect("poisoned");
        let res = if let Some(ref h) = g.hook {
            h(instance, resource_unit)
        } else {
            PreemptResult::no_preemptable()
        };
        g.last = Some(res.clone());
        res
    }

    /// Rank preemptable units (placeholder; production code scores instances).
    pub fn choose_preemptable(
        &self,
        candidates: Vec<PreemptableUnit>,
        _instance: &InstanceDescriptor,
    ) -> Option<PreemptableUnit> {
        candidates.into_iter().max_by(|a, b| {
            a.score
                .partial_cmp(&b.score)
                .unwrap_or(std::cmp::Ordering::Equal)
        })
    }
}

impl Default for PreemptionController {
    fn default() -> Self {
        Self::new()
    }
}
