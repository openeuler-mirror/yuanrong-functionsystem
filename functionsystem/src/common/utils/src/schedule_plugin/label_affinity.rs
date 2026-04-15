//! Label affinity filter/scorer helpers (`label_affinity_filter.cpp`, `label_affinity_scorer.cpp`).

use crate::scheduler_framework::ScheduleContext;
use super::affinity_utils::{
    affinity_scorer, anti_affinity_scorer, get_local_node_labels, get_local_node_labels_parts,
    is_affinity_priority, required_affinity_filter, required_anti_affinity_filter,
};
use super::constants::LABEL_AFFINITY_PLUGIN;
use super::preallocated::{PreAllocatedContext, SchedulerLevel};
use super::resource::merge_node_labels;
use crate::status::StatusCode;
use std::collections::HashMap;
use yr_proto::affinity::Selector;
use yr_proto::messages::plugin_context::PluginContext as PcInner;
use yr_proto::messages::{AffinityContext, PluginContext};
use yr_proto::resources::value;
use yr_proto::resources::{InstanceInfo, ResourceUnit};

fn affinity_scorer_meet_optimal(
    unit_id: &str,
    selector: &Selector,
    labels: &HashMap<String, value::Counter>,
    anti: bool,
) -> bool {
    let Some(cond) = &selector.condition else {
        return true;
    };
    if cond.sub_conditions.is_empty() {
        return true;
    }
    let w0 = cond.sub_conditions[0].weight as f64;
    let score = if anti {
        anti_affinity_scorer(unit_id, selector, labels)
    } else {
        affinity_scorer(unit_id, selector, labels)
    };
    (score - w0).abs() < 1e-9
}

pub fn affinity_ctx_mut(ctxs: &mut HashMap<String, PluginContext>) -> Option<&mut AffinityContext> {
    let pc = ctxs
        .entry(LABEL_AFFINITY_PLUGIN.to_string())
        .or_insert_with(|| PluginContext {
            plugin_context: Some(PcInner::AffinityCtx(AffinityContext::default())),
        });
    match &mut pc.plugin_context {
        Some(PcInner::AffinityCtx(a)) => Some(a),
        _ => None,
    }
}

// ---- filter-side helpers (abbreviated names matching C++) ----

pub fn check_agent_available(
    instance: &InstanceInfo,
    affinity_ctx: &AffinityContext,
    resource_unit: &ResourceUnit,
    pre_ctx: &dyn ScheduleContext,
) -> bool {
    let unit_id = &resource_unit.id;
    if !pre_ctx.check_node_feasible(&resource_unit.owner_id) {
        return false;
    }
    if let Some(&code) = affinity_ctx.scheduled_result.get(unit_id) {
        if code == StatusCode::AffinityScheduleFailed as i32 {
            return false;
        }
    }
    let _ = instance;
    true
}

pub fn need_label_filter(instance: &InstanceInfo) -> bool {
    let Some(so) = &instance.schedule_option else {
        return false;
    };
    let Some(aff) = &so.affinity else {
        return false;
    };
    if let Some(inst) = &aff.instance {
        if inst.required_affinity.is_some() || inst.required_anti_affinity.is_some() {
            return true;
        }
    }
    if let Some(res) = &aff.resource {
        if res.required_affinity.is_some() || res.required_anti_affinity.is_some() {
            return true;
        }
    }
    if let Some(inner) = &aff.inner {
        if inner.tenant.as_ref().is_some_and(|t| t.required_anti_affinity.is_some()) {
            return true;
        }
        if inner.pending.as_ref().is_some_and(|p| !p.resources.is_empty()) {
            return true;
        }
        if inner.rgroup.as_ref().is_some_and(|r| r.required_affinity.is_some()) {
            return true;
        }
        if inner.grouplb.as_ref().is_some_and(|g| g.required_anti_affinity.is_some()) {
            return true;
        }
    }
    false
}

pub fn is_instance_required_affinity_passed(
    unit_id: &str,
    instance: &InstanceInfo,
    labels: &HashMap<String, value::Counter>,
) -> bool {
    let Some(so) = &instance.schedule_option else {
        return true;
    };
    let Some(aff) = &so.affinity else {
        return true;
    };
    let Some(inst) = &aff.instance else {
        return true;
    };
    if let Some(sel) = &inst.required_affinity {
        if !required_affinity_filter(unit_id, sel, labels) {
            return false;
        }
    }
    if let Some(sel) = &inst.required_anti_affinity {
        if !required_anti_affinity_filter(unit_id, sel, labels) {
            return false;
        }
    }
    true
}

pub fn is_resource_required_affinity_passed(
    unit_id: &str,
    instance: &InstanceInfo,
    labels: &HashMap<String, value::Counter>,
) -> bool {
    let Some(so) = &instance.schedule_option else {
        return true;
    };
    let Some(aff) = &so.affinity else {
        return true;
    };
    let Some(res) = &aff.resource else {
        return true;
    };
    if let Some(sel) = &res.required_affinity {
        if !required_affinity_filter(unit_id, sel, labels) {
            return false;
        }
    }
    if let Some(sel) = &res.required_anti_affinity {
        if !required_anti_affinity_filter(unit_id, sel, labels) {
            return false;
        }
    }
    true
}

pub fn is_inner_tenant_required_affinity_passed(
    unit_id: &str,
    instance: &InstanceInfo,
    labels: &HashMap<String, value::Counter>,
) -> bool {
    let Some(so) = &instance.schedule_option else {
        return true;
    };
    let Some(aff) = &so.affinity else {
        return true;
    };
    let Some(inner) = &aff.inner else {
        return true;
    };
    let Some(t) = &inner.tenant else {
        return true;
    };
    let Some(sel) = &t.required_anti_affinity else {
        return true;
    };
    required_anti_affinity_filter(unit_id, sel, labels)
}

pub fn is_inner_pending_required_affinity_passed(
    unit_id: &str,
    instance: &InstanceInfo,
    labels: &HashMap<String, value::Counter>,
) -> bool {
    let Some(so) = &instance.schedule_option else {
        return true;
    };
    let Some(aff) = &so.affinity else {
        return true;
    };
    let Some(inner) = &aff.inner else {
        return true;
    };
    let Some(pending) = &inner.pending else {
        return true;
    };
    for pr in &pending.resources {
        let mut met = true;
        if let Some(sel) = &pr.required_affinity {
            met &= required_affinity_filter(unit_id, sel, labels);
        }
        if let Some(sel) = &pr.required_anti_affinity {
            met &= required_anti_affinity_filter(unit_id, sel, labels);
        }
        if met {
            return false;
        }
    }
    true
}

pub fn is_inner_rgroup_required_affinity_passed(
    unit_id: &str,
    instance: &InstanceInfo,
    labels: &HashMap<String, value::Counter>,
) -> bool {
    let Some(so) = &instance.schedule_option else {
        return true;
    };
    let Some(aff) = &so.affinity else {
        return true;
    };
    let Some(inner) = &aff.inner else {
        return true;
    };
    let Some(rg) = &inner.rgroup else {
        return true;
    };
    let Some(sel) = &rg.required_affinity else {
        return true;
    };
    required_affinity_filter(unit_id, sel, labels)
}

pub fn is_inner_grouplb_required_affinity_passed(
    unit_id: &str,
    instance: &InstanceInfo,
    labels: &HashMap<String, value::Counter>,
) -> bool {
    let Some(so) = &instance.schedule_option else {
        return true;
    };
    let Some(aff) = &so.affinity else {
        return true;
    };
    let Some(inner) = &aff.inner else {
        return true;
    };
    let Some(g) = &inner.grouplb else {
        return true;
    };
    let Some(sel) = &g.required_anti_affinity else {
        return true;
    };
    required_anti_affinity_filter(unit_id, sel, labels)
}

fn check_agent_available_inner(
    instance: &InstanceInfo,
    affinity_ctx: &AffinityContext,
    resource_unit: &ResourceUnit,
    unfeasible_nodes: &std::collections::HashSet<String>,
) -> bool {
    let unit_id = &resource_unit.id;
    let owner_id = resource_unit.owner_id.as_str();
    if unfeasible_nodes.contains(owner_id) {
        return false;
    }
    if let Some(&code) = affinity_ctx.scheduled_result.get(unit_id) {
        if code == StatusCode::AffinityScheduleFailed as i32 {
            return false;
        }
    }
    let _ = instance;
    true
}

pub fn perform_label_filter(
    instance: &InstanceInfo,
    affinity_ctx: &AffinityContext,
    resource_unit: &ResourceUnit,
    unfeasible_nodes: &mut std::collections::HashSet<String>,
    allocated_labels: &HashMap<String, HashMap<String, value::Counter>>,
    all_local_labels: &HashMap<String, HashMap<String, value::Counter>>,
    scheduler_level: SchedulerLevel,
    is_node_scope: bool,
) -> bool {
    let unit_id = resource_unit.id.as_str();
    let owner_id = resource_unit.owner_id.as_str();
    if !check_agent_available_inner(instance, affinity_ctx, resource_unit, unfeasible_nodes) {
        return false;
    }
    let unit_labels = merge_node_labels(
        &resource_unit.node_labels,
        allocated_labels.get(unit_id).unwrap_or(&HashMap::new()),
    );
    let local_node_labels =
        get_local_node_labels_parts(resource_unit, all_local_labels, scheduler_level);
    if is_node_scope {
        if !is_instance_required_affinity_passed(owner_id, instance, &local_node_labels) {
            unfeasible_nodes.insert(owner_id.to_string());
            return false;
        }
    } else if !is_instance_required_affinity_passed(unit_id, instance, &unit_labels) {
        return false;
    }
    if !is_resource_required_affinity_passed(unit_id, instance, &resource_unit.node_labels) {
        return false;
    }
    if !is_inner_tenant_required_affinity_passed(unit_id, instance, &unit_labels) {
        return false;
    }
    if !is_inner_pending_required_affinity_passed(unit_id, instance, &resource_unit.node_labels) {
        return false;
    }
    if !is_inner_rgroup_required_affinity_passed(unit_id, instance, &resource_unit.node_labels) {
        return false;
    }
    if !is_inner_grouplb_required_affinity_passed(unit_id, instance, &unit_labels) {
        return false;
    }
    true
}

// Optimal checks reuse scorer weights — full tree omitted for inner/data/tenant/group like C++.
pub fn perform_score_optimality_check(
    resource_unit: &ResourceUnit,
    instance: &InstanceInfo,
    pre_ctx: &PreAllocatedContext,
    is_node_scope: bool,
) -> bool {
    let unit_id = resource_unit.id.as_str();
    let Some(so) = &instance.schedule_option else {
        return true;
    };
    let Some(aff) = &so.affinity else {
        return true;
    };
    let labels_merged = if is_node_scope {
        get_local_node_labels(resource_unit, pre_ctx)
    } else {
        merge_node_labels(
            &resource_unit.node_labels,
            pre_ctx.allocated_labels.get(unit_id).unwrap_or(&HashMap::new()),
        )
    };
    if let Some(inst) = &aff.instance {
        if let Some(sel) = &inst.preferred_affinity {
            if !affinity_scorer_meet_optimal(unit_id, sel, &labels_merged, false) {
                return false;
            }
        }
        if let Some(sel) = &inst.preferred_anti_affinity {
            if !affinity_scorer_meet_optimal(unit_id, sel, &labels_merged, true) {
                return false;
            }
        }
        if let Some(sel) = &inst.required_affinity {
            if is_affinity_priority(sel)
                && !affinity_scorer_meet_optimal(unit_id, sel, &labels_merged, false)
            {
                return false;
            }
        }
        if let Some(sel) = &inst.required_anti_affinity {
            if is_affinity_priority(sel)
                && !affinity_scorer_meet_optimal(unit_id, sel, &labels_merged, true)
            {
                return false;
            }
        }
    }
    let _ = aff.resource;
    true
}
