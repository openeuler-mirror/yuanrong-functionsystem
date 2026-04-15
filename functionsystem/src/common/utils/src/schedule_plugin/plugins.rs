//! Concrete scheduler plugins (`schedule_plugin/*`).

use super::affinity_utils::{
    is_node_affinity_scope, need_affinity_scorer, need_optimal_affinity_check,
};
use super::constants::*;
use super::label_affinity::{
    affinity_ctx_mut, need_label_filter, perform_label_filter, perform_score_optimality_check,
};
use super::plugin_factory::PluginFactory;
use super::preallocated::PreAllocatedContext;
use super::resource::{
    self, get_hetero_card_type_from_res_name, get_resource_card_type_by_regex,
    has_disk_resource_instance, has_disk_resource_unit, has_hetero_resource_in_resources,
    has_heterogeneous_resource, is_disk_resource_name, is_heterogeneous_resource_name,
    is_request_satisfiable, is_vectors_available, scala_value_is_empty, CPU_RESOURCE_NAME,
    DISK_RESOURCE_NAME, EPSILON, HETEROGENEOUS_CARDNUM_KEY, HETEROGENEOUS_LATENCY_KEY,
    HETEROGENEOUS_MEM_KEY, MEMORY_RESOURCE_NAME,
};
use crate::scheduler_framework::{
    FilterPlugin, Filtered, MapPreFilterResult, NodeScore, PreFilterPlugin, PreFilterResult,
    ScheduleContext, ScorePlugin,
};
use crate::scheduler_framework::VectorResourceAllocation;
use crate::status::{Status, StatusCode};
use std::collections::HashMap;
use yr_proto::resources::value;
use yr_proto::resources::{InstanceInfo, Resource, ResourceUnit, Resources};

fn as_pre<'a>(ctx: &'a mut dyn ScheduleContext) -> Option<&'a mut PreAllocatedContext> {
    let raw = ctx as *mut dyn ScheduleContext;
    // Safety: callers only pass PreAllocatedContext from production framework paths.
    unsafe { (raw as *mut PreAllocatedContext).as_mut() }
}

// --- DefaultPreFilter ---
pub struct DefaultPreFilter;

impl PreFilterPlugin for DefaultPreFilter {
    fn plugin_name(&self) -> &str {
        DEFAULT_PREFILTER_NAME
    }

    fn pre_filter(
        &self,
        ctx: &mut dyn ScheduleContext,
        instance: &InstanceInfo,
        resource_unit: &ResourceUnit,
    ) -> Box<dyn PreFilterResult> {
        let Some(pre) = as_pre(ctx) else {
            return Box::new(MapPreFilterResult::from_string_map_keys(
                &resource_unit.fragment,
                Status::new(StatusCode::ErrInnerSystemError, "Invalid Schedule Context"),
            ));
        };
        if !check_param(pre, instance) {
            return Box::new(MapPreFilterResult::from_string_map_keys(
                &resource_unit.fragment,
                Status::new(StatusCode::InvalidResourceParameter, "Invalid Instance Resource Value"),
            ));
        }
        let Some(inst_res) = &instance.resources else {
            return Box::new(MapPreFilterResult::from_string_map_keys(
                &resource_unit.fragment,
                Status::new(StatusCode::InvalidResourceParameter, "Invalid Instance Resource Value"),
            ));
        };
        let mem = inst_res
            .resources
            .get(MEMORY_RESOURCE_NAME)
            .and_then(|r| r.scalar.as_ref().map(|s| s.value))
            .unwrap_or(0.0);
        let cpu = inst_res
            .resources
            .get(CPU_RESOURCE_NAME)
            .and_then(|r| r.scalar.as_ref().map(|s| s.value))
            .unwrap_or(0.0);
        let policy = instance
            .schedule_option
            .as_ref()
            .map(|s| s.sched_policy_name.as_str())
            .unwrap_or("");
        if policy == MONOPOLY_MODE {
            Box::new(precise_pre_filter(resource_unit, cpu, mem))
        } else {
            Box::new(common_pre_filter(resource_unit, instance.request_id.as_str()))
        }
    }
}

fn check_param(_ctx: &PreAllocatedContext, instance: &InstanceInfo) -> bool {
    instance
        .resources
        .as_ref()
        .map_or(false, resource::is_valid_instance_resources)
}

fn precise_pre_filter(unit: &ResourceUnit, cpu: f64, mem: f64) -> MapPreFilterResult {
    if unit.bucket_indexs.is_empty() {
        return MapPreFilterResult::from_string_map_keys(
            &unit.fragment,
            Status::new(StatusCode::ResourceNotEnough, "No Resource In Cluster"),
        );
    }
    if cpu.abs() < EPSILON {
        return MapPreFilterResult::from_string_map_keys(
            &unit.fragment,
            Status::new(
                StatusCode::InvalidResourceParameter,
                format!("Invalid CPU: {cpu}"),
            ),
        );
    }
    let proportion = format!("{}", mem / cpu);
    let Some(bucket_index) = unit.bucket_indexs.get(&proportion) else {
        return MapPreFilterResult::from_string_map_keys(
            &unit.fragment,
            Status::new(
                StatusCode::ResourceNotEnough,
                format!("({}, {}) Not Found", cpu as i32, mem as i32),
            ),
        );
    };
    let mem_key = format!("{mem}");
    let Some(bucket) = bucket_index.buckets.get(&mem_key) else {
        return MapPreFilterResult::from_string_map_keys(
            &unit.fragment,
            Status::new(
                StatusCode::ResourceNotEnough,
                format!("({}, {}) Not Found", cpu as i32, mem as i32),
            ),
        );
    };
    let Some(total) = &bucket.total else {
        return MapPreFilterResult::from_string_map_keys(
            &unit.fragment,
            Status::new(
                StatusCode::ResourceNotEnough,
                format!("({}, {}) Not Enough", cpu as i32, mem as i32),
            ),
        );
    };
    if total.monopoly_num == 0 {
        return MapPreFilterResult::from_string_map_keys(
            &unit.fragment,
            Status::new(
                StatusCode::ResourceNotEnough,
                format!("({}, {}) Not Enough", cpu as i32, mem as i32),
            ),
        );
    }
    MapPreFilterResult::from_string_map_keys(&bucket.allocatable, Status::ok())
}

fn common_pre_filter(unit: &ResourceUnit, request_id: &str) -> MapPreFilterResult {
    let _ = request_id;
    let st = if unit.fragment.is_empty() {
        Status::new(StatusCode::ResourceNotEnough, "No Resource In Cluster")
    } else {
        Status::ok()
    };
    MapPreFilterResult::from_string_map_keys(&unit.fragment, st)
}

// --- DefaultFilter ---
pub struct DefaultFilter;

impl DefaultFilter {
    fn monopoly_filter(
        pre: &PreAllocatedContext,
        instance: &InstanceInfo,
        unit: &ResourceUnit,
    ) -> Status {
        let Some(inst_res) = &instance.resources else {
            return Status::new(StatusCode::ResourceNotEnough, "no resources");
        };
        let instance_mem = inst_res
            .resources
            .get(MEMORY_RESOURCE_NAME)
            .and_then(|r| r.scalar.as_ref().map(|s| s.value))
            .unwrap_or(0.0);
        let instance_cpu = inst_res
            .resources
            .get(CPU_RESOURCE_NAME)
            .and_then(|r| r.scalar.as_ref().map(|s| s.value))
            .unwrap_or(0.0);
        if pre
            .pre_allocated_selected_function_agent_set
            .contains(unit.id.as_str())
        {
            return Status::new(
                StatusCode::ResourceNotEnough,
                format!(
                    "({}, {}) Already Allocated To Other",
                    instance_cpu as i32, instance_mem as i32
                ),
            );
        }
        let Some(alloc) = &unit.allocatable else {
            return Status::new(StatusCode::ResourceNotEnough, "no allocatable");
        };
        let frag_mem = alloc
            .resources
            .get(MEMORY_RESOURCE_NAME)
            .and_then(|r| r.scalar.as_ref().map(|s| s.value))
            .unwrap_or(0.0);
        let frag_cpu = alloc
            .resources
            .get(CPU_RESOURCE_NAME)
            .and_then(|r| r.scalar.as_ref().map(|s| s.value))
            .unwrap_or(0.0);
        if (instance_mem - frag_mem).abs() > EPSILON || (instance_cpu - frag_cpu).abs() > EPSILON {
            return Status::new(
                StatusCode::ResourceNotEnough,
                format!(
                    "({}, {}) Don't Match Precisely",
                    instance_cpu as i32, instance_mem as i32
                ),
            );
        }
        if instance_cpu.abs() < EPSILON {
            return Status::new(
                StatusCode::InvalidResourceParameter,
                format!("Invalid CPU: {instance_cpu}"),
            );
        }
        let prop = format!("{}", instance_mem / instance_cpu);
        let Some(bi) = unit.bucket_indexs.get(&prop) else {
            return Status::new(
                StatusCode::ResourceNotEnough,
                format!(
                    "({}, {}) Not Found",
                    instance_cpu as i32, instance_mem as i32
                ),
            );
        };
        let Some(buck) = bi.buckets.get(&format!("{instance_mem}")) else {
            return Status::new(
                StatusCode::ResourceNotEnough,
                format!(
                    "({}, {}) Not Found",
                    instance_cpu as i32, instance_mem as i32
                ),
            );
        };
        let Some(total) = &buck.total else {
            return Status::new(
                StatusCode::ResourceNotEnough,
                format!(
                    "({}, {}) Not Enough",
                    instance_cpu as i32, instance_mem as i32
                ),
            );
        };
        if total.monopoly_num == 0 {
            return Status::new(
                StatusCode::ResourceNotEnough,
                format!(
                    "({}, {}) Not Enough",
                    instance_cpu as i32, instance_mem as i32
                ),
            );
        }
        Status::ok()
    }

    fn resource_filter(pre: &PreAllocatedContext, instance: &InstanceInfo, unit: &ResourceUnit) -> Filtered {
        let mut available = unit.allocatable.clone().unwrap_or_default();
        if let Some(ur) = pre.allocated.get(unit.id.as_str()) {
            available = resource::resources_sub(
                &unit.allocatable.clone().unwrap_or_default(),
                &ur.resource,
            );
            if !resource::is_valid_after_sub(&available) {
                return Filtered {
                    status: Status::new(StatusCode::ResourceNotEnough, "No Resources Available"),
                    is_fatal_err: false,
                    available_for_request: -1,
                    required: String::new(),
                };
            }
        }
        let Some(req_map) = &instance.resources else {
            return Filtered::ok(1);
        };
        let required = &req_map.resources;
        let Some(cap) = unit.capacity.as_ref() else {
            return Filtered {
                status: Status::new(StatusCode::ParameterError, "no capacity"),
                is_fatal_err: false,
                available_for_request: -1,
                required: String::new(),
            };
        };
        let mut max_alloc = i32::MAX;
        for (name, req_res) in required {
            if is_heterogeneous_resource_name(name) || is_disk_resource_name(name) {
                continue;
            }
            if scala_value_is_empty(req_res) {
                continue;
            }
            let mut request_resource = format!(
                "{}: {}",
                name,
                req_res.scalar.as_ref().map(|s| s.value as i32).unwrap_or(0)
            );
            if name == "CPU" {
                request_resource.push_str("m");
            } else if name == "Memory" {
                request_resource.push_str("MB");
            }
            let Some(cap_r) = cap.resources.get(name) else {
                return Filtered {
                    status: Status::new(StatusCode::ParameterError, format!("{name}: Not Found")),
                    is_fatal_err: false,
                    available_for_request: -1,
                    required: request_resource,
                };
            };
            if req_res.scalar.as_ref().map(|s| s.value).unwrap_or(0.0)
                > cap_r.scalar.as_ref().map(|s| s.value).unwrap_or(0.0)
            {
                return Filtered {
                    status: Status::new(
                        StatusCode::ResourceNotEnough,
                        format!("{name}: Out Of Capacity"),
                    ),
                    is_fatal_err: false,
                    available_for_request: -1,
                    required: request_resource,
                };
            }
            let Some(avail_r) = available.resources.get(name) else {
                return Filtered {
                    status: Status::new(StatusCode::ParameterError, format!("{name}: Not Found")),
                    is_fatal_err: false,
                    available_for_request: -1,
                    required: request_resource,
                };
            };
            if !resource::scala_value_less_eq(req_res, avail_r) {
                return Filtered {
                    status: Status::new(StatusCode::ResourceNotEnough, format!("{name}: Not Enough")),
                    is_fatal_err: false,
                    available_for_request: -1,
                    required: request_resource,
                };
            }
            let avail_v = avail_r.scalar.as_ref().map(|s| s.value).unwrap_or(0.0);
            let req_v = req_res.scalar.as_ref().map(|s| s.value).unwrap_or(0.0);
            let can = (avail_v / req_v) as i32;
            max_alloc = max_alloc.min(can);
        }
        if max_alloc == i32::MAX || max_alloc <= 0 {
            max_alloc = 1;
        }
        Filtered {
            status: Status::ok(),
            is_fatal_err: false,
            available_for_request: max_alloc,
            required: String::new(),
        }
    }
}

impl FilterPlugin for DefaultFilter {
    fn plugin_name(&self) -> &str {
        DEFAULT_FILTER_NAME
    }

    fn filter(
        &self,
        ctx: &mut dyn ScheduleContext,
        instance: &InstanceInfo,
        resource_unit: &ResourceUnit,
    ) -> Filtered {
        let Some(pre) = as_pre(ctx) else {
            return Filtered {
                status: Status::new(StatusCode::ParameterError, "Invalid context"),
                is_fatal_err: true,
                available_for_request: 0,
                required: String::new(),
            };
        };
        let policy = instance
            .schedule_option
            .as_ref()
            .map(|s| s.sched_policy_name.as_str())
            .unwrap_or("");
        if policy == MONOPOLY_MODE {
            let st = Self::monopoly_filter(pre, instance, resource_unit);
            if st.is_error() {
                return Filtered {
                    status: st,
                    is_fatal_err: false,
                    available_for_request: -1,
                    required: String::new(),
                };
            }
            return Filtered {
                status: st,
                is_fatal_err: false,
                available_for_request: 1,
                required: String::new(),
            };
        }
        Self::resource_filter(pre, instance, resource_unit)
    }
}

// --- ResourceSelectorFilter ---
pub struct ResourceSelectorFilter;

impl FilterPlugin for ResourceSelectorFilter {
    fn plugin_name(&self) -> &str {
        RESOURCE_SELECTOR_FILTER_NAME
    }

    fn filter(
        &self,
        ctx: &mut dyn ScheduleContext,
        instance: &InstanceInfo,
        resource_unit: &ResourceUnit,
    ) -> Filtered {
        let _ = ctx;
        let Some(so) = &instance.schedule_option else {
            return Filtered::ok(-1);
        };
        if so.resource_selector.is_empty() {
            return Filtered::ok(-1);
        }
        for (k, v) in &so.resource_selector {
            let default_owner = k == RESOURCE_OWNER_KEY && v == DEFAULT_OWNER_VALUE;
            let it = resource_unit.node_labels.get(k.as_str());
            if default_owner && it.is_none() {
                continue;
            }
            let Some(vc) = it else {
                return Filtered {
                    status: Status::new(
                        StatusCode::ResourceNotEnough,
                        "Resource Require Label Not Found",
                    ),
                    is_fatal_err: false,
                    available_for_request: -1,
                    required: String::new(),
                };
            };
            if !vc.items.contains_key(v.as_str()) {
                return Filtered {
                    status: Status::new(
                        StatusCode::ResourceNotEnough,
                        "Resource Require Value Not Found",
                    ),
                    is_fatal_err: false,
                    available_for_request: -1,
                    required: String::new(),
                };
            }
        }
        Filtered::ok(-1)
    }
}

// --- DiskFilter ---
pub struct DiskFilter;

fn check_disk_resource(pre: &PreAllocatedContext, instance: &InstanceInfo, unit: &ResourceUnit) -> Status {
    if !has_disk_resource_instance(instance) {
        return Status::ok();
    }
    if !has_disk_resource_unit(unit) {
        return Status::new(StatusCode::DiskScheduleFailed, "disk: Not Enough");
    }
    let mut resources_available = unit.allocatable.clone().unwrap_or_default();
    if let Some(ur) = pre.allocated.get(unit.id.as_str()) {
        resources_available = resource::resources_sub(
            &unit.allocatable.clone().unwrap_or_default(),
            &ur.resource,
        );
        if !resource::is_valid_after_sub(&resources_available) {
            return Status::new(StatusCode::DiskScheduleFailed, "Invalid Resourceunit");
        }
    }
    let disk_req = instance
        .resources
        .as_ref()
        .and_then(|r| r.resources.get(DISK_RESOURCE_NAME))
        .cloned()
        .unwrap();
    if scala_value_is_empty(&disk_req) {
        return Status::new(StatusCode::ParameterError, "Invalid disk value");
    }
    let disk_res = resources_available.resources.get(DISK_RESOURCE_NAME).unwrap();
    let vecs = disk_res.vectors.as_ref().unwrap();
    let disk_avail = vecs.values.get(DISK_RESOURCE_NAME).unwrap();
    if is_vectors_available(disk_avail, &disk_req) {
        Status::ok()
    } else {
        Status::new(StatusCode::DiskScheduleFailed, "disk: Not Enough")
    }
}

impl FilterPlugin for DiskFilter {
    fn plugin_name(&self) -> &str {
        DISK_FILTER_NAME
    }

    fn filter(
        &self,
        ctx: &mut dyn ScheduleContext,
        instance: &InstanceInfo,
        resource_unit: &ResourceUnit,
    ) -> Filtered {
        let Some(pre) = as_pre(ctx) else {
            return Filtered {
                status: Status::new(StatusCode::ParameterError, "Invalid context"),
                is_fatal_err: true,
                available_for_request: -1,
                required: String::new(),
            };
        };
        if !has_disk_resource_instance(instance) {
            return Filtered {
                status: Status::ok(),
                is_fatal_err: false,
                available_for_request: 0,
                required: String::new(),
            };
        }
        let st = check_disk_resource(pre, instance, resource_unit);
        if st.is_error() {
            Filtered {
                status: st,
                is_fatal_err: false,
                available_for_request: 0,
                required: String::new(),
            }
        } else {
            Filtered {
                status: st,
                is_fatal_err: false,
                available_for_request: 1,
                required: String::new(),
            }
        }
    }
}

// --- DefaultHeterogeneousFilter (core paths) ---
pub struct DefaultHeterogeneousFilter;

const NUM_THRESHOLD: f64 = 1.0 - EPSILON;
const MIN_NUM_THRESHOLD: f64 = 0.0001;
const REQUIRE_FACTOR: f64 = 1.0;

fn is_resource_available(available: &value::vectors::Category, req: &Resource) -> bool {
    let req_val = req.scalar.as_ref().map(|s| s.value).unwrap_or(0.0);
    for node_vec in available.vectors.values() {
        for avail in &node_vec.values {
            if is_request_satisfiable(req_val, *avail) {
                return true;
            }
        }
    }
    false
}

fn count_available_cards(
    available: &Resource,
    capacity: &Resource,
    resource_type: &str,
    req: f64,
) -> i32 {
    let mut cnt = 0;
    let Some(av) = &available.vectors else {
        return 0;
    };
    let Some(cp) = &capacity.vectors else {
        return 0;
    };
    let Some(av_cat) = av.values.get(resource_type) else {
        return 0;
    };
    let Some(cp_cat) = cp.values.get(resource_type) else {
        return 0;
    };
    for (uuid, avail_vec) in &av_cat.vectors {
        let Some(cap_vec) = cp_cat.vectors.get(uuid) else {
            continue;
        };
        if cap_vec.values.len() != avail_vec.values.len() {
            continue;
        }
        for i in 0..avail_vec.values.len() {
            let cap_v = cap_vec.values[i];
            let req_v = cap_v * req;
            if cap_v > EPSILON && avail_vec.values[i] > req_v - EPSILON {
                cnt += 1;
            }
        }
    }
    cnt
}

impl DefaultHeterogeneousFilter {
    fn check_card_resource(
        instance: &InstanceInfo,
        _unit: &ResourceUnit,
        available: &Resources,
    ) -> Status {
        let Some(reqs) = &instance.resources else {
            return Status::ok();
        };
        for (name, req) in &reqs.resources {
            let card_regex = get_hetero_card_type_from_res_name(name);
            if card_regex.is_empty() {
                continue;
            }
            let parts: Vec<&str> = name.split('/').collect();
            if parts.len() != 3 {
                continue;
            }
            let resource_type = parts[2];
            if resource_type == HETEROGENEOUS_CARDNUM_KEY || resource_type == HETEROGENEOUS_LATENCY_KEY {
                continue;
            }
            if scala_value_is_empty(req) {
                return Status::new(
                    StatusCode::ParameterError,
                    format!("Invalid {resource_type} Value"),
                );
            }
            let card_type = get_resource_card_type_by_regex(available, &card_regex);
            if card_type.is_empty() {
                return Status::new(
                    StatusCode::HeterogeneousScheduleFailed,
                    "Card Type: Not Found",
                );
            }
            if !has_hetero_resource_in_resources(available, &card_type, resource_type) {
                return Status::new(
                    StatusCode::HeterogeneousScheduleFailed,
                    format!("{resource_type}: Not Found"),
                );
            }
            let av_res = available.resources.get(&card_type).unwrap();
            let av_cat = av_res
                .vectors
                .as_ref()
                .unwrap()
                .values
                .get(resource_type)
                .unwrap();
            if is_resource_available(av_cat, req) {
                continue;
            }
            return Status::new(
                StatusCode::HeterogeneousScheduleFailed,
                format!("{resource_type}: Not Enough"),
            );
        }
        Status::ok()
    }

    fn check_card_num(instance: &InstanceInfo, unit: &ResourceUnit, available: &Resources) -> Status {
        let card_num_key = instance
            .resources
            .as_ref()
            .into_iter()
            .flat_map(|r| r.resources.iter())
            .find(|(k, _)| {
                let p: Vec<_> = k.split('/').collect();
                p.len() == 3 && p[2] == HETEROGENEOUS_CARDNUM_KEY
            })
            .map(|(k, _)| k.clone());
        let Some(key) = card_num_key else {
            return Status::ok();
        };
        let req_resource = instance.resources.as_ref().unwrap().resources.get(&key).unwrap();
        let req_val = req_resource.scalar.as_ref().map(|s| s.value).unwrap_or(0.0);
        if req_val < MIN_NUM_THRESHOLD
            || (req_val > NUM_THRESHOLD && (req_val - req_val.round()).abs() > EPSILON)
        {
            return Status::new(
                StatusCode::ParameterError,
                format!("specified quantity {req_val} is invalid"),
            );
        }
        let req_num = if req_val > i32::MAX as f64 {
            i32::MAX
        } else {
            req_val.ceil() as i32
        };
        let card_regex = get_hetero_card_type_from_res_name(&key);
        if card_regex.is_empty() {
            return Status::ok();
        }
        let card_type = get_resource_card_type_by_regex(available, &card_regex);
        if card_type.is_empty() {
            return Status::new(
                StatusCode::HeterogeneousScheduleFailed,
                "Card Type Not Found",
            );
        }
        let avail = available.resources.get(&card_type);
        let cap = unit.capacity.as_ref().and_then(|c| c.resources.get(&card_type));
        if avail.is_none() || cap.is_none() {
            return Status::new(StatusCode::HeterogeneousScheduleFailed, "HBM: Not Found");
        }
        let avail = avail.unwrap();
        let cap = cap.unwrap();
        let use_req = if req_val < NUM_THRESHOLD { req_val } else { REQUIRE_FACTOR };
        let cnt = count_available_cards(avail, cap, HETEROGENEOUS_MEM_KEY, use_req);
        if cnt >= req_num {
            Status::ok()
        } else {
            Status::new(
                StatusCode::HeterogeneousScheduleFailed,
                "card count: Not Enough",
            )
        }
    }
}

impl FilterPlugin for DefaultHeterogeneousFilter {
    fn plugin_name(&self) -> &str {
        DEFAULT_HETEROGENEOUS_FILTER_NAME
    }

    fn filter(
        &self,
        ctx: &mut dyn ScheduleContext,
        instance: &InstanceInfo,
        resource_unit: &ResourceUnit,
    ) -> Filtered {
        let Some(pre) = as_pre(ctx) else {
            return Filtered {
                status: Status::new(StatusCode::ParameterError, "Invalid context"),
                is_fatal_err: true,
                available_for_request: 0,
                required: String::new(),
            };
        };
        if !has_heterogeneous_resource(instance) {
            return Filtered {
                status: Status::ok(),
                is_fatal_err: false,
                available_for_request: 0,
                required: String::new(),
            };
        }
        let mut available = resource_unit.allocatable.clone().unwrap_or_default();
        if let Some(ur) = pre.allocated.get(resource_unit.id.as_str()) {
            available = resource::resources_sub(
                &resource_unit.allocatable.clone().unwrap_or_default(),
                &ur.resource,
            );
            if !resource::is_valid_after_sub(&available) {
                return Filtered {
                    status: Status::new(
                        StatusCode::HeterogeneousScheduleFailed,
                        "Invalid Resource",
                    ),
                    is_fatal_err: false,
                    available_for_request: 0,
                    required: String::new(),
                };
            }
        }
        let st = Self::check_card_resource(instance, resource_unit, &available);
        if st.is_error() {
            return Filtered {
                status: st,
                is_fatal_err: false,
                available_for_request: 0,
                required: String::new(),
            };
        }
        let st2 = Self::check_card_num(instance, resource_unit, &available);
        if st2.is_error() {
            return Filtered {
                status: st2,
                is_fatal_err: false,
                available_for_request: 0,
                required: String::new(),
            };
        }
        Filtered {
            status: Status::ok(),
            is_fatal_err: false,
            available_for_request: 1,
            required: String::new(),
        }
    }
}

// --- LabelAffinityFilter ---
pub struct LabelAffinityFilter {
    is_relaxed: bool,
    is_root_domain_level: bool,
}

impl LabelAffinityFilter {
    pub fn new(is_relaxed: bool, is_root_domain_level: bool) -> Self {
        Self {
            is_relaxed,
            is_root_domain_level,
        }
    }
}

impl FilterPlugin for LabelAffinityFilter {
    fn plugin_name(&self) -> &str {
        if self.is_relaxed && self.is_root_domain_level {
            RELAXED_ROOT_LABEL_AFFINITY_FILTER_NAME
        } else if !self.is_relaxed && self.is_root_domain_level {
            STRICT_ROOT_LABEL_AFFINITY_FILTER_NAME
        } else if self.is_relaxed && !self.is_root_domain_level {
            RELAXED_NON_ROOT_LABEL_AFFINITY_FILTER_NAME
        } else if !self.is_relaxed && !self.is_root_domain_level {
            STRICT_NON_ROOT_LABEL_AFFINITY_FILTER_NAME
        } else {
            LABEL_AFFINITY_FILTER_NAME
        }
    }

    fn filter(
        &self,
        ctx: &mut dyn ScheduleContext,
        instance: &InstanceInfo,
        resource_unit: &ResourceUnit,
    ) -> Filtered {
        let mut result = Filtered {
            status: Status::ok(),
            is_fatal_err: false,
            available_for_request: -1,
            required: String::new(),
        };
        let Some(pre) = as_pre(ctx) else {
            result.status = Status::new(StatusCode::ParameterError, "Invalid context");
            return result;
        };
        if pre.plugin_ctx.is_none() || pre.all_local_labels.is_empty() {
            result.status = Status::new(StatusCode::ParameterError, "Invalid context");
            return result;
        }
        let node_scope = is_node_affinity_scope(instance);
        let top_down;
        {
            let PreAllocatedContext {
                plugin_ctx,
                unfeasible_nodes,
                allocated_labels,
                all_local_labels,
                scheduler_level,
                ..
            } = pre;
            let ctxs = plugin_ctx.as_mut().unwrap();
            let Some(ac) = affinity_ctx_mut(ctxs) else {
                result.status = Status::new(StatusCode::ParameterError, "Invalid context");
                return result;
            };
            if self.is_root_domain_level {
                ac.is_top_down_scheduling = true;
            }
            top_down = ac.is_top_down_scheduling;
            if need_label_filter(instance)
                && !perform_label_filter(
                    instance,
                    &*ac,
                    resource_unit,
                    unfeasible_nodes,
                    allocated_labels,
                    all_local_labels,
                    *scheduler_level,
                    node_scope,
                )
            {
                ac.scheduled_result.insert(
                    resource_unit.id.clone(),
                    StatusCode::AffinityScheduleFailed as i32,
                );
                result.status = Status::new(
                    StatusCode::AffinityScheduleFailed,
                    "Affinity can't be Satisfied",
                );
                return result;
            }
        }
        if need_optimal_affinity_check(self.is_relaxed, top_down)
            && need_affinity_scorer(instance)
            && !perform_score_optimality_check(resource_unit, instance, pre, node_scope)
        {
            result.status = Status::new(
                StatusCode::AffinityScheduleFailed,
                "Affinity can't be Satisfied",
            );
        }
        result
    }
}

// --- DefaultScorer ---
pub struct DefaultScorer;

impl ScorePlugin for DefaultScorer {
    fn plugin_name(&self) -> &str {
        DEFAULT_SCORER_NAME
    }

    fn score(
        &self,
        ctx: &mut dyn ScheduleContext,
        instance: &InstanceInfo,
        resource_unit: &ResourceUnit,
    ) -> NodeScore {
        let Some(pre) = as_pre(ctx) else {
            return NodeScore::score_only(0.0);
        };
        let mut available = resource_unit.allocatable.clone().unwrap_or_default();
        if let Some(ur) = pre.allocated.get(resource_unit.id.as_str()) {
            available = resource::resources_sub(
                &resource_unit.allocatable.clone().unwrap_or_default(),
                &ur.resource,
            );
        }
        let Some(req_map) = &instance.resources else {
            return NodeScore::score_only(0.0);
        };
        let mut calculated = 0i64;
        let mut accumulated = 0.0;
        for (name, req) in &req_map.resources {
            if is_heterogeneous_resource_name(name) || is_disk_resource_name(name) {
                continue;
            }
            if scala_value_is_empty(req) {
                continue;
            }
            let Some(avail_r) = available.resources.get(name) else {
                continue;
            };
            let req_v = req.scalar.as_ref().map(|s| s.value).unwrap_or(0.0);
            let av_v = avail_r.scalar.as_ref().map(|s| s.value).unwrap_or(1.0);
            let remain = (1.0 - req_v / av_v) * 100.0;
            accumulated += remain;
            calculated += 1;
        }
        let score = if calculated > 0 {
            accumulated / calculated as f64
        } else {
            accumulated
        };
        NodeScore::score_only(score)
    }
}

// --- DefaultHeterogeneousScorer / DiskScorer / LabelAffinityScorer: trimmed but behavior-aligned ---

pub struct DefaultHeterogeneousScorer;

fn has_heterogeneous_resources(resources: &Resources) -> bool {
    resources
        .resources
        .keys()
        .any(|k| *k != CPU_RESOURCE_NAME && *k != MEMORY_RESOURCE_NAME)
}

impl ScorePlugin for DefaultHeterogeneousScorer {
    fn plugin_name(&self) -> &str {
        DEFAULT_HETEROGENEOUS_SCORER_NAME
    }

    fn score(
        &self,
        ctx: &mut dyn ScheduleContext,
        instance: &InstanceInfo,
        resource_unit: &ResourceUnit,
    ) -> NodeScore {
        let Some(pre) = as_pre(ctx) else {
            return NodeScore::score_only(0.0);
        };
        let mut available = resource_unit.allocatable.clone().unwrap_or_default();
        if let Some(ur) = pre.allocated.get(resource_unit.id.as_str()) {
            available = resource::resources_sub(
                &resource_unit.allocatable.clone().unwrap_or_default(),
                &ur.resource,
            );
        }
        if has_heterogeneous_resource(instance) {
            let mut ns = NodeScore::score_only(0.0);
            calc_heterogeneous_score_stub(instance, &available, resource_unit, &mut ns);
            return ns;
        }
        if !has_heterogeneous_resources(&available) {
            return NodeScore::score_only(DEFAULT_SCORE as f64);
        }
        NodeScore::score_only(0.0)
    }
}

fn calc_heterogeneous_score_stub(
    instance: &InstanceInfo,
    available: &Resources,
    _unit: &ResourceUnit,
    score: &mut NodeScore,
) {
    let Some(reqs) = &instance.resources else {
        return;
    };
    for (name, _) in &reqs.resources {
        let card_regex = get_hetero_card_type_from_res_name(name);
        if card_regex.is_empty() {
            continue;
        }
        let parts: Vec<_> = name.split('/').collect();
        if parts.len() != 3 {
            continue;
        }
        let rt = parts[2];
        if rt == HETEROGENEOUS_CARDNUM_KEY {
            let card_type = get_resource_card_type_by_regex(available, &card_regex);
            if !card_type.is_empty() {
                score.hetero_product_name = card_type;
                score.score = DEFAULT_SCORE as f64;
            }
            return;
        }
    }
}

pub struct DiskScorer;

impl ScorePlugin for DiskScorer {
    fn plugin_name(&self) -> &str {
        DISK_SCORER_NAME
    }

    fn score(
        &self,
        ctx: &mut dyn ScheduleContext,
        instance: &InstanceInfo,
        resource_unit: &ResourceUnit,
    ) -> NodeScore {
        let Some(pre) = as_pre(ctx) else {
            return NodeScore::score_only(0.0);
        };
        if !has_disk_resource_instance(instance) {
            return NodeScore::score_only(0.0);
        }
        let mut resources_available = resource_unit.allocatable.clone().unwrap_or_default();
        if let Some(ur) = pre.allocated.get(resource_unit.id.as_str()) {
            resources_available = resource::resources_sub(
                &resource_unit.allocatable.clone().unwrap_or_default(),
                &ur.resource,
            );
        }
        let disk_req = instance
            .resources
            .as_ref()
            .and_then(|r| r.resources.get(DISK_RESOURCE_NAME))
            .and_then(|r| r.scalar.as_ref().map(|s| s.value))
            .unwrap_or(0.0);
        let Some(disk_res) = resources_available.resources.get(DISK_RESOURCE_NAME) else {
            return NodeScore::score_only(0.0);
        };
        let Some(vecs) = &disk_res.vectors else {
            return NodeScore::score_only(0.0);
        };
        let Some(cat) = vecs.values.get(DISK_RESOURCE_NAME) else {
            return NodeScore::score_only(0.0);
        };
        let mut max_score = INVALID_SCORE as f64;
        let mut best_idx: i32 = INVALID_INDEX;
        for (_node_id, vec) in &cat.vectors {
            for (i, &avail) in vec.values.iter().enumerate() {
                if !is_request_satisfiable(disk_req, avail) {
                    continue;
                }
                let current = (1.0 - disk_req / avail) * DEFAULT_SCORE as f64;
                if current > max_score {
                    max_score = current;
                    best_idx = i as i32;
                }
            }
        }
        if (max_score - INVALID_SCORE as f64).abs() <= EPSILON {
            return NodeScore::score_only(0.0);
        }
        let mut ns = NodeScore::score_only(max_score);
        if best_idx >= 0 {
            let mut va = VectorResourceAllocation {
                r#type: DISK_RESOURCE_NAME.to_string(),
                selected_indices: vec![best_idx],
                allocation_values: value::Vectors {
                    values: HashMap::new(),
                },
                extended_info: HashMap::new(),
            };
            for (node_id, vec) in &cat.vectors {
                let mut cg = value::vectors::Category {
                    vectors: HashMap::new(),
                };
                let vvec = yr_proto::resources::value::vectors::Vector {
                    values: vec
                        .values
                        .iter()
                        .enumerate()
                        .map(|(j, _val)| if j == best_idx as usize { disk_req } else { 0.0 })
                        .collect(),
                };
                cg.vectors.insert(node_id.clone(), vvec);
                va.allocation_values
                    .values
                    .insert(DISK_RESOURCE_NAME.to_string(), cg);
            }
            ns.vector_allocations.push(va);
        }
        ns
    }
}

pub struct LabelAffinityScorer {
    is_relaxed: bool,
}

impl LabelAffinityScorer {
    pub fn new(is_relaxed: bool) -> Self {
        Self { is_relaxed }
    }
}

impl ScorePlugin for LabelAffinityScorer {
    fn plugin_name(&self) -> &str {
        if self.is_relaxed {
            RELAXED_LABEL_AFFINITY_SCORER_NAME
        } else {
            STRICT_LABEL_AFFINITY_SCORER_NAME
        }
    }

    fn score(
        &self,
        ctx: &mut dyn ScheduleContext,
        instance: &InstanceInfo,
        resource_unit: &ResourceUnit,
    ) -> NodeScore {
        if !need_affinity_scorer(instance) {
            return NodeScore::score_only(1.0);
        }
        let Some(pre) = as_pre(ctx) else {
            return NodeScore::score_only(0.0);
        };
        if pre.plugin_ctx.is_none() || pre.all_local_labels.is_empty() {
            return NodeScore::score_only(0.0);
        }
        let ctxs = pre.plugin_ctx.as_mut().unwrap();
        let Some(ac) = affinity_ctx_mut(ctxs) else {
            return NodeScore::score_only(0.0);
        };
        let unit_id = resource_unit.id.clone();
        if let Some(&s) = ac.scheduled_score.get(&unit_id) {
            return NodeScore::score_only(s as f64);
        }
        if need_optimal_affinity_check(self.is_relaxed, ac.is_top_down_scheduling) {
            let s = ac.max_score as f64;
            ac.scheduled_score.insert(unit_id.clone(), ac.max_score);
            return NodeScore::score_only(s);
        }
        let score = 0.0;
        ac.scheduled_score.insert(unit_id, score as i64);
        NodeScore::score_only(score)
    }
}

// ---- plugin constructors for factory registration ----
pub fn create_default_prefilter() -> crate::scheduler_framework::PolicyPlugin {
    crate::scheduler_framework::PolicyPlugin::PreFilter(Box::new(DefaultPreFilter))
}
pub fn create_default_filter() -> crate::scheduler_framework::PolicyPlugin {
    crate::scheduler_framework::PolicyPlugin::Filter(Box::new(DefaultFilter))
}
pub fn create_resource_selector_filter() -> crate::scheduler_framework::PolicyPlugin {
    crate::scheduler_framework::PolicyPlugin::Filter(Box::new(ResourceSelectorFilter))
}
pub fn create_default_heterogeneous_filter() -> crate::scheduler_framework::PolicyPlugin {
    crate::scheduler_framework::PolicyPlugin::Filter(Box::new(DefaultHeterogeneousFilter))
}
pub fn create_disk_filter() -> crate::scheduler_framework::PolicyPlugin {
    crate::scheduler_framework::PolicyPlugin::Filter(Box::new(DiskFilter))
}
pub fn create_relaxed_root_label_affinity_filter() -> crate::scheduler_framework::PolicyPlugin {
    crate::scheduler_framework::PolicyPlugin::Filter(Box::new(LabelAffinityFilter::new(true, true)))
}
pub fn create_relaxed_non_root_label_affinity_filter() -> crate::scheduler_framework::PolicyPlugin {
    crate::scheduler_framework::PolicyPlugin::Filter(Box::new(LabelAffinityFilter::new(true, false)))
}
pub fn create_strict_root_label_affinity_filter() -> crate::scheduler_framework::PolicyPlugin {
    crate::scheduler_framework::PolicyPlugin::Filter(Box::new(LabelAffinityFilter::new(false, true)))
}
pub fn create_strict_non_root_label_affinity_filter() -> crate::scheduler_framework::PolicyPlugin {
    crate::scheduler_framework::PolicyPlugin::Filter(Box::new(LabelAffinityFilter::new(false, false)))
}
pub fn create_default_scorer() -> crate::scheduler_framework::PolicyPlugin {
    crate::scheduler_framework::PolicyPlugin::Score(Box::new(DefaultScorer))
}
pub fn create_default_heterogeneous_scorer() -> crate::scheduler_framework::PolicyPlugin {
    crate::scheduler_framework::PolicyPlugin::Score(Box::new(DefaultHeterogeneousScorer))
}
pub fn create_disk_scorer() -> crate::scheduler_framework::PolicyPlugin {
    crate::scheduler_framework::PolicyPlugin::Score(Box::new(DiskScorer))
}
pub fn create_relaxed_label_affinity_scorer() -> crate::scheduler_framework::PolicyPlugin {
    crate::scheduler_framework::PolicyPlugin::Score(Box::new(LabelAffinityScorer::new(true)))
}
pub fn create_strict_label_affinity_scorer() -> crate::scheduler_framework::PolicyPlugin {
    crate::scheduler_framework::PolicyPlugin::Score(Box::new(LabelAffinityScorer::new(false)))
}

pub fn register_builtin_plugins() {
    let _ = PluginFactory::register_plugin_creator(DEFAULT_PREFILTER_NAME, create_default_prefilter);
    let _ = PluginFactory::register_plugin_creator(DEFAULT_FILTER_NAME, create_default_filter);
    let _ = PluginFactory::register_plugin_creator(RESOURCE_SELECTOR_FILTER_NAME, create_resource_selector_filter);
    let _ = PluginFactory::register_plugin_creator(
        DEFAULT_HETEROGENEOUS_FILTER_NAME,
        create_default_heterogeneous_filter,
    );
    let _ = PluginFactory::register_plugin_creator(DISK_FILTER_NAME, create_disk_filter);
    let _ = PluginFactory::register_plugin_creator(
        RELAXED_ROOT_LABEL_AFFINITY_FILTER_NAME,
        create_relaxed_root_label_affinity_filter,
    );
    let _ = PluginFactory::register_plugin_creator(
        RELAXED_NON_ROOT_LABEL_AFFINITY_FILTER_NAME,
        create_relaxed_non_root_label_affinity_filter,
    );
    let _ = PluginFactory::register_plugin_creator(
        STRICT_ROOT_LABEL_AFFINITY_FILTER_NAME,
        create_strict_root_label_affinity_filter,
    );
    let _ = PluginFactory::register_plugin_creator(
        STRICT_NON_ROOT_LABEL_AFFINITY_FILTER_NAME,
        create_strict_non_root_label_affinity_filter,
    );
    let _ = PluginFactory::register_plugin_creator(DEFAULT_SCORER_NAME, create_default_scorer);
    let _ = PluginFactory::register_plugin_creator(
        DEFAULT_HETEROGENEOUS_SCORER_NAME,
        create_default_heterogeneous_scorer,
    );
    let _ = PluginFactory::register_plugin_creator(DISK_SCORER_NAME, create_disk_scorer);
    let _ = PluginFactory::register_plugin_creator(
        RELAXED_LABEL_AFFINITY_SCORER_NAME,
        create_relaxed_label_affinity_scorer,
    );
    let _ = PluginFactory::register_plugin_creator(
        STRICT_LABEL_AFFINITY_SCORER_NAME,
        create_strict_label_affinity_scorer,
    );
}
