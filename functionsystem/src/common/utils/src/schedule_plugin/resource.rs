//! Resource helpers (scalar / vectors / labels) aligned with `resource_view/resource_tool.*`.

use super::constants::{
    HETERO_RESOURCE_FIELD_NUM, PRODUCT_INDEX, VENDOR_IDX,
};
use std::collections::HashMap;
use yr_proto::resources::value::{self, Type as ValueType};
use yr_proto::resources::{InstanceInfo, Resource, ResourceUnit, Resources};

pub const EPSILON: f64 = 1e-8;
pub const THOUSAND_INT: i64 = 1000;
pub const THOUSAND_DOUBLE: f64 = 1000.0;

pub const CPU_RESOURCE_NAME: &str = "CPU";
pub const MEMORY_RESOURCE_NAME: &str = "Memory";
pub const DISK_RESOURCE_NAME: &str = "disk";
pub const HETEROGENEOUS_MEM_KEY: &str = "HBM";
pub const HETEROGENEOUS_LATENCY_KEY: &str = "latency";
pub const HETEROGENEOUS_STREAM_KEY: &str = "stream";
pub const HETEROGENEOUS_CARDNUM_KEY: &str = "count";
pub const DISK_MOUNT_POINT: &str = "YR_DISK_MOUNT_POINT";
pub const HETEROGENEOUS_RESOURCE_REQUIRED_COUNT: f32 = 3.0;

#[inline]
pub fn to_long(value: f64) -> i64 {
    (value * THOUSAND_INT as f64).round() as i64
}

#[inline]
pub fn to_double(value: i64) -> f64 {
    let int_part = value / THOUSAND_INT;
    let dec_part = (value % THOUSAND_INT) as f64 / THOUSAND_DOUBLE;
    int_part as f64 + dec_part
}

pub fn scala_value_is_empty(res: &Resource) -> bool {
    res.scalar.as_ref().map_or(true, |s| s.value.abs() < EPSILON)
}

pub fn scala_value_sub(l: &Resource, r: &Resource) -> Resource {
    let mut out = l.clone();
    let lv = l.scalar.as_ref().map(|s| s.value).unwrap_or(0.0);
    let rv = r.scalar.as_ref().map(|s| s.value).unwrap_or(0.0);
    let nv = to_double(to_long(lv) - to_long(rv));
    out.scalar = Some(value::Scalar {
        value: nv,
        limit: l.scalar.as_ref().map(|s| s.limit).unwrap_or(0.0),
    });
    out
}

pub fn scala_value_less_eq(req: &Resource, avail: &Resource) -> bool {
    let a = req.scalar.as_ref().map(|s| s.value).unwrap_or(0.0);
    let b = avail.scalar.as_ref().map(|s| s.value).unwrap_or(0.0);
    a < b || (a - b).abs() <= EPSILON
}

pub fn resource_is_valid(res: &Resource) -> bool {
    if res.name.is_empty() {
        return false;
    }
    match res.r#type {
        x if x == ValueType::Scalar as i32 => res.scalar.is_some(),
        x if x == ValueType::Vectors as i32 => res.vectors.is_some(),
        _ => false,
    }
}

pub fn resources_is_valid(res: &Resources) -> bool {
    if res.resources.is_empty() {
        return false;
    }
    res.resources.values().all(resource_is_valid)
}

/// Instance resources must include valid scalar CPU and Memory (see C++ `CheckParam` / `IsValid`).
pub fn is_valid_instance_resources(res: &Resources) -> bool {
    let cpu_ok = res.resources.get(CPU_RESOURCE_NAME).map_or(false, |r| {
        r.r#type == ValueType::Scalar as i32 && r.scalar.is_some()
    });
    let mem_ok = res.resources.get(MEMORY_RESOURCE_NAME).map_or(false, |r| {
        r.r#type == ValueType::Scalar as i32 && r.scalar.is_some()
    });
    cpu_ok && mem_ok
}

pub fn vectors_sub(left: &Resource, right: &Resource) -> Resource {
    let mut result = left.clone();
    let Some(rv) = &right.vectors else {
        return result;
    };
    let Some(lv) = result.vectors.as_mut() else {
        return result;
    };
    for (cat_name, cat) in &rv.values {
        let Some(base_cat) = lv.values.get_mut(cat_name) else {
            continue;
        };
        for (uuid, vec) in &cat.vectors {
            let Some(base_vec) = base_cat.vectors.get_mut(uuid) else {
                continue;
            };
            if right.expired {
                base_cat.vectors.remove(uuid);
                continue;
            }
            for (i, rvv) in vec.values.iter().enumerate() {
                if let Some(bv) = base_vec.values.get_mut(i) {
                    *bv -= rvv;
                }
            }
        }
    }
    result
}

pub fn resources_sub(left: &Resources, right: &Resources) -> Resources {
    let mut sub = left.clone();
    for (k, rv) in &right.resources {
        if let Some(lv) = sub.resources.get_mut(k) {
            if lv.r#type == ValueType::Scalar as i32 && rv.r#type == ValueType::Scalar as i32 {
                *lv = scala_value_sub(lv, rv);
            } else if lv.r#type == ValueType::Vectors as i32 && rv.r#type == ValueType::Vectors as i32 {
                *lv = vectors_sub(lv, rv);
            }
        }
    }
    sub
}

pub fn is_valid_after_sub(available: &Resources) -> bool {
    resources_is_valid(available)
}

pub fn is_request_satisfiable(req: f64, available: f64) -> bool {
    req < available || (req - available).abs() <= EPSILON
}

pub fn is_vectors_available(available: &value::vectors::Category, req: &Resource) -> bool {
    let req_val = req.scalar.as_ref().map(|s| s.value).unwrap_or(0.0);
    for v in available.vectors.values() {
        for av in &v.values {
            if is_request_satisfiable(req_val, *av) {
                return true;
            }
        }
    }
    false
}

pub fn is_heterogeneous_resource_name(name: &str) -> bool {
    name.split('/').count() == HETERO_RESOURCE_FIELD_NUM
}

pub fn has_heterogeneous_resource(instance: &InstanceInfo) -> bool {
    let Some(res) = &instance.resources else {
        return false;
    };
    res.resources
        .keys()
        .any(|k| is_heterogeneous_resource_name(k))
}

pub fn is_disk_resource_name(name: &str) -> bool {
    name == DISK_RESOURCE_NAME
}

pub fn has_disk_resource_instance(instance: &InstanceInfo) -> bool {
    instance
        .resources
        .as_ref()
        .is_some_and(|r| r.resources.contains_key(DISK_RESOURCE_NAME))
}

pub fn has_disk_resource_unit(unit: &ResourceUnit) -> bool {
    let Some(alloc) = &unit.allocatable else {
        return false;
    };
    let Some(disk) = alloc.resources.get(DISK_RESOURCE_NAME) else {
        return false;
    };
    let Some(vecs) = &disk.vectors else {
        return false;
    };
    vecs.values.contains_key(DISK_RESOURCE_NAME)
}

pub fn split_resource_name(name: &str) -> Vec<&str> {
    name.split('/').collect()
}

pub fn get_hetero_card_type_from_res_name(resource_name: &str) -> String {
    let f: Vec<&str> = split_resource_name(resource_name);
    if f.len() != HETERO_RESOURCE_FIELD_NUM {
        return String::new();
    }
    format!("{}/{}", f[VENDOR_IDX], f[PRODUCT_INDEX])
}

pub fn get_resource_card_type_by_regex(resources: &Resources, card_type_regex: &str) -> String {
    if let Ok(re) = regex::Regex::new(card_type_regex) {
        for k in resources.resources.keys() {
            if re.is_match(k) {
                return k.clone();
            }
        }
    }
    String::new()
}

pub fn has_hetero_resource_in_resources(resources: &Resources, card_type: &str, resource_type: &str) -> bool {
    let Some(r) = resources.resources.get(card_type) else {
        return false;
    };
    let Some(v) = &r.vectors else {
        return false;
    };
    v.values.contains_key(resource_type)
}

pub fn merge_node_labels(
    a: &HashMap<String, value::Counter>,
    b: &HashMap<String, value::Counter>,
) -> HashMap<String, value::Counter> {
    let mut out = a.clone();
    for (k, vb) in b {
        let entry = out.entry(k.clone()).or_insert_with(|| value::Counter {
            items: HashMap::new(),
        });
        for (vk, vv) in &vb.items {
            *entry.items.entry(vk.clone()).or_insert(0) += vv;
        }
    }
    out
}
