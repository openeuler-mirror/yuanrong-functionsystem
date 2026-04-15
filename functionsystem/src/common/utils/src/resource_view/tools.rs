//! Scalar / set / vector resource arithmetic (port of `resource_tool.h` operators).

use super::ResourceMaps;
use std::cmp::Ordering;

/// Float tolerance aligned with C++ `IsRequestSatisfiable` / `EPSINON`-style checks.
pub(crate) const EPSILON: f64 = 1e-6;

/// Compare heterogeneous devices by `device_id` (C++ `HeteroDeviceCompare`).
#[derive(Debug, Clone, Copy, Default)]
pub struct HeteroDeviceCompare;

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct HeteroDeviceInfo {
    pub device_id: String,
    pub product: String,
}

impl HeteroDeviceCompare {
    pub fn cmp(&self, a: &HeteroDeviceInfo, b: &HeteroDeviceInfo) -> Ordering {
        a.device_id.cmp(&b.device_id)
    }
}

pub struct ScalarResourceTool;

impl ScalarResourceTool {
    pub fn add_to(target: &ResourceMaps, delta: &ResourceMaps) {
        for e in delta.scalar.iter() {
            let (dv, dl) = *e.value();
            target
                .scalar
                .entry(e.key().clone())
                .and_modify(|(v, l)| {
                    *v += dv;
                    *l += dl;
                })
                .or_insert((dv, dl));
        }
    }

    pub fn sub_from(target: &ResourceMaps, delta: &ResourceMaps) {
        for e in delta.scalar.iter() {
            let (dv, dl) = *e.value();
            if let Some(mut cur) = target.scalar.get_mut(e.key()) {
                cur.0 -= dv;
                cur.1 -= dl;
            }
        }
    }

    /// Every scalar in `need` is <= allocatable value (limits ignored for feasibility).
    pub fn allocatable_covers(alloc: &ResourceMaps, need: &ResourceMaps) -> bool {
        for e in need.scalar.iter() {
            let (req, _) = *e.value();
            let Some(av) = alloc.scalar.get(e.key()) else {
                return false;
            };
            if req > av.0 + EPSILON {
                return false;
            }
        }
        true
    }
}

pub struct SetResourceTool;

impl SetResourceTool {
    pub fn union_into(target: &ResourceMaps, more: &ResourceMaps) {
        for e in more.sets.iter() {
            target
                .sets
                .entry(e.key().clone())
                .and_modify(|s| {
                    s.extend(e.value().iter().cloned());
                })
                .or_insert_with(|| e.value().clone());
        }
    }

    pub fn subtract_from(target: &ResourceMaps, sub: &ResourceMaps) {
        for e in sub.sets.iter() {
            if let Some(mut cur) = target.sets.get_mut(e.key()) {
                for x in e.value().iter() {
                    cur.remove(x);
                }
            }
        }
    }

    /// True if every key in `need.sets` is a subset of `alloc.sets`.
    pub fn is_subset(alloc: &ResourceMaps, need: &ResourceMaps) -> bool {
        for e in need.sets.iter() {
            let Some(have) = alloc.sets.get(e.key()) else {
                return false;
            };
            for x in e.value().iter() {
                if !have.contains(x) {
                    return false;
                }
            }
        }
        true
    }
}

pub struct VectorsResourceTool;

impl VectorsResourceTool {
    pub fn merge_into(target: &ResourceMaps, more: &ResourceMaps) {
        for e in more.vectors.iter() {
            target
                .vectors
                .entry(e.key().clone())
                .and_modify(|v| {
                    v.extend(e.value().iter().cloned());
                })
                .or_insert_with(|| e.value().clone());
        }
    }

    /// Remove first occurrences of each id in `sub` from `target` per key.
    pub fn subtract_from(target: &ResourceMaps, sub: &ResourceMaps) {
        for e in sub.vectors.iter() {
            if let Some(mut cur) = target.vectors.get_mut(e.key()) {
                for id in e.value().iter() {
                    if let Some(pos) = cur.iter().position(|x| x == id) {
                        cur.remove(pos);
                    }
                }
            }
        }
    }

    pub fn allocatable_covers(alloc: &ResourceMaps, need: &ResourceMaps) -> bool {
        for e in need.vectors.iter() {
            let Some(have) = alloc.vectors.get(e.key()) else {
                return false;
            };
            let mut pool = have.clone();
            for id in e.value().iter() {
                let Some(pos) = pool.iter().position(|x| x == id) else {
                    return false;
                };
                pool.remove(pos);
            }
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn scalar_covers() {
        let a = ResourceMaps::default();
        a.scalar.insert("x".into(), (10.0, 10.0));
        let n = ResourceMaps::default();
        n.scalar.insert("x".into(), (9.0, 0.0));
        assert!(ScalarResourceTool::allocatable_covers(&a, &n));
    }
}
