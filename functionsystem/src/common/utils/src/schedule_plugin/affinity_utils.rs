//! Affinity scoring / filtering (`affinity_utils.h` / `.cpp`).

use super::preallocated::{PreAllocatedContext, SchedulerLevel};
use std::collections::HashMap;
use yr_proto::affinity::{self, AffinityScope};
use yr_proto::resources::value;
use yr_proto::resources::{InstanceInfo, ResourceUnit};

pub const ZERO_SCORE: f64 = 0.0;
pub const SCORE_EPSILON: f64 = 1e-9;
pub const RESOURCE_AFFINITY_WEIGHT: i64 = 100;

fn label_in_values(
    labels: &HashMap<String, value::Counter>,
    key: &str,
    values: &[String],
) -> bool {
    let Some(vc) = labels.get(key) else {
        return false;
    };
    values.iter().any(|v| vc.items.contains_key(v))
}

fn label_key_exists(labels: &HashMap<String, value::Counter>, key: &str) -> bool {
    labels.contains_key(key)
}

fn is_match_label_expression(
    labels: &HashMap<String, value::Counter>,
    expression: &affinity::LabelExpression,
) -> bool {
    let Some(op) = &expression.op else {
        return true;
    };
    use yr_proto::affinity::label_operator::LabelOperator::*;
    match &op.label_operator {
        Some(In(i)) => label_in_values(labels, &expression.key, &i.values),
        Some(NotIn(n)) => !label_in_values(labels, &expression.key, &n.values),
        Some(Exists(_)) => label_key_exists(labels, &expression.key),
        Some(NotExist(_)) => !label_key_exists(labels, &expression.key),
        None => true,
    }
}

pub fn is_affinity_priority(selector: &affinity::Selector) -> bool {
    selector
        .condition
        .as_ref()
        .is_some_and(|c| c.order_priority)
}

fn get_affinity_score(
    _unit_id: &str,
    selector: &affinity::Selector,
    labels: &HashMap<String, value::Counter>,
    anti: bool,
) -> f64 {
    let Some(cond) = &selector.condition else {
        return 0.0;
    };
    for sub in &cond.sub_conditions {
        let mut group_ok = true;
        for expr in &sub.expressions {
            let m = is_match_label_expression(labels, expr);
            group_ok = group_ok && m;
        }
        let mut sat = group_ok;
        if anti {
            sat = !sat;
        }
        if sat {
            return sub.weight as f64;
        }
    }
    0.0
}

fn filter_required_without_priority(
    selector: &affinity::Selector,
    labels: &HashMap<String, value::Counter>,
    anti: bool,
) -> bool {
    let Some(cond) = &selector.condition else {
        return true;
    };
    let mut required = true;
    for sub in &cond.sub_conditions {
        for expr in &sub.expressions {
            let m = is_match_label_expression(labels, expr);
            required = required && m;
        }
    }
    if anti {
        required = !required;
    }
    required
}

fn filter_required_with_priority(
    unit_id: &str,
    selector: &affinity::Selector,
    labels: &HashMap<String, value::Counter>,
    anti: bool,
) -> bool {
    let s = get_affinity_score(unit_id, selector, labels, anti);
    (s - ZERO_SCORE).abs() > SCORE_EPSILON
}

pub fn required_affinity_filter(
    unit_id: &str,
    selector: &affinity::Selector,
    labels: &HashMap<String, value::Counter>,
) -> bool {
    if is_affinity_priority(selector) {
        filter_required_with_priority(unit_id, selector, labels, false)
    } else {
        filter_required_without_priority(selector, labels, false)
    }
}

pub fn required_anti_affinity_filter(
    unit_id: &str,
    selector: &affinity::Selector,
    labels: &HashMap<String, value::Counter>,
) -> bool {
    if is_affinity_priority(selector) {
        filter_required_with_priority(unit_id, selector, labels, true)
    } else {
        filter_required_without_priority(selector, labels, true)
    }
}

pub fn affinity_scorer(
    unit_id: &str,
    selector: &affinity::Selector,
    labels: &HashMap<String, value::Counter>,
) -> f64 {
    get_affinity_score(unit_id, selector, labels, false)
}

pub fn anti_affinity_scorer(
    unit_id: &str,
    selector: &affinity::Selector,
    labels: &HashMap<String, value::Counter>,
) -> f64 {
    get_affinity_score(unit_id, selector, labels, true)
}

pub fn is_node_affinity_scope(instance: &InstanceInfo) -> bool {
    let Some(so) = &instance.schedule_option else {
        return false;
    };
    let Some(aff) = &so.affinity else {
        return false;
    };
    let Some(inst) = &aff.instance else {
        return false;
    };
    inst.scope == AffinityScope::Node as i32
}

pub fn need_affinity_scorer(instance: &InstanceInfo) -> bool {
    let Some(so) = &instance.schedule_option else {
        return false;
    };
    let Some(aff) = &so.affinity else {
        return false;
    };
    if let Some(inst) = &aff.instance {
        if inst.required_affinity.as_ref().is_some_and(is_affinity_priority) {
            return true;
        }
        if inst
            .required_anti_affinity
            .as_ref()
            .is_some_and(is_affinity_priority)
        {
            return true;
        }
        if inst.preferred_affinity.is_some() || inst.preferred_anti_affinity.is_some() {
            return true;
        }
    }
    if let Some(res) = &aff.resource {
        if res.required_affinity.as_ref().is_some_and(is_affinity_priority) {
            return true;
        }
        if res
            .required_anti_affinity
            .as_ref()
            .is_some_and(is_affinity_priority)
        {
            return true;
        }
        if res.preferred_affinity.is_some() || res.preferred_anti_affinity.is_some() {
            return true;
        }
    }
    if let Some(inner) = &aff.inner {
        if inner.tenant.is_some()
            || inner.data.is_some()
            || inner.preempt.is_some()
            || inner.grouplb.is_some()
        {
            return true;
        }
    }
    false
}

pub fn need_optimal_affinity_check(is_relaxed: bool, is_top_down_scheduling: bool) -> bool {
    !is_relaxed && !is_top_down_scheduling
}

pub fn get_local_node_labels(
    resource_unit: &ResourceUnit,
    ctx: &PreAllocatedContext,
) -> HashMap<String, value::Counter> {
    get_local_node_labels_parts(
        resource_unit,
        &ctx.all_local_labels,
        ctx.scheduler_level,
    )
}

pub fn get_local_node_labels_parts(
    resource_unit: &ResourceUnit,
    all_local_labels: &HashMap<String, HashMap<String, value::Counter>>,
    scheduler_level: SchedulerLevel,
) -> HashMap<String, value::Counter> {
    if all_local_labels.is_empty() {
        return HashMap::new();
    }
    if scheduler_level == SchedulerLevel::Local {
        all_local_labels
            .values()
            .next()
            .cloned()
            .unwrap_or_default()
    } else {
        all_local_labels
            .get(&resource_unit.owner_id)
            .cloned()
            .unwrap_or_default()
    }
}
