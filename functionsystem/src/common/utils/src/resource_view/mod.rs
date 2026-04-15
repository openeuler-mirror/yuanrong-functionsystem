//! Resource view types and managers ported from `functionsystem/src/common/resource_view/`.
//!
//! Uses [`DashMap`] for concurrent maps and [`Arc<RwLock<_>>`](parking_lot::RwLock) for shared units.

mod poller;
mod tools;

pub use poller::{ResourcePollInfo, ResourcePoller};
pub use tools::{
    HeteroDeviceCompare, HeteroDeviceInfo, ScalarResourceTool, SetResourceTool, VectorsResourceTool,
};

use crate::status::{Status, StatusCode};
use dashmap::DashMap;
use parking_lot::RwLock;
use serde::{Deserialize, Serialize};
use std::collections::{BTreeSet, HashMap, HashSet};
use std::sync::atomic::{AtomicI64, Ordering};
use std::sync::Arc;
use uuid::Uuid;

// --- Constants (resource_type.h) -------------------------------------------------

pub const CPU_RESOURCE_NAME: &str = "CPU";
pub const MEMORY_RESOURCE_NAME: &str = "Memory";
pub const DISK_RESOURCE_NAME: &str = "disk";
pub const DEFAULT_NPU_PRODUCT: &str = "310";
pub const DEFAULT_GPU_PRODUCT: &str = "cuda";
pub const GPU_RESOURCE_NAME: &str = "GPU";
pub const NPU_RESOURCE_NAME: &str = "NPU";
pub const INIT_LABELS_RESOURCE_NAME: &str = "InitLabels";

pub const MULTI_STREAM_DEFAULT_NUM: u32 = 100;
pub const HETEROGENEOUS_RESOURCE_REQUIRED_COUNT: u32 = 3;
pub const HETEROGENEOUS_MEM_KEY: &str = "HBM";
pub const HETEROGENEOUS_LATENCY_KEY: &str = "latency";
pub const HETEROGENEOUS_STREAM_KEY: &str = "stream";
pub const HETEROGENEOUS_CARDNUM_KEY: &str = "count";
pub const HEALTH_KEY: &str = "health";
pub const IDS_KEY: &str = "ids";
pub const USED_IDS_KEY: &str = "used_ids";
pub const DEV_CLUSTER_IPS_KEY: &str = "dev_cluster_ips";

pub const MAX_CONCURRENCY_PULL: u32 = 100;

// --- Enums ----------------------------------------------------------------------

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum UpdateType {
    UpdateActual,
    UpdateStatic,
    UpdateDynamic,
    UpdateUndefined,
}

#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum UnitStatus {
    Normal = 0,
    Evicting = 1,
    Recovering = 2,
    ToBeDeleted = 3,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum SchedulerLevel {
    Local,
    NonRootDomain,
    RootDomain,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize)]
pub enum ResourceType {
    Primary = 0,
    Virtual = 1,
}

// --- Core resource containers ---------------------------------------------------

/// Label / bucket counter (protobuf `Value::Counter`-shaped).
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct LabelCounter {
    pub monopoly_num: i64,
    pub shared_num: i64,
}

/// Scalar, set (labels), and vector (device lists) buckets for one resource view surface.
#[derive(Debug, Default)]
pub struct ResourceMaps {
    pub scalar: DashMap<String, (f64, f64)>,
    pub sets: DashMap<String, BTreeSet<String>>,
    pub vectors: DashMap<String, Vec<String>>,
}

impl Clone for ResourceMaps {
    fn clone(&self) -> Self {
        let out = Self::default();
        for e in self.scalar.iter() {
            out.scalar.insert(e.key().clone(), *e.value());
        }
        for e in self.sets.iter() {
            out.sets.insert(e.key().clone(), e.value().clone());
        }
        for e in self.vectors.iter() {
            out.vectors.insert(e.key().clone(), e.value().clone());
        }
        out
    }
}

impl ResourceMaps {
    pub fn is_empty(&self) -> bool {
        self.scalar.is_empty() && self.sets.is_empty() && self.vectors.is_empty()
    }
}

/// One schedulable unit (agent / scheduler) and its capacity views.
#[derive(Debug)]
pub struct ResourceUnit {
    pub id: String,
    pub revision: AtomicI64,
    pub view_init_time: RwLock<String>,
    pub owner_id: RwLock<String>,
    pub capacity: ResourceMaps,
    pub allocatable: ResourceMaps,
    pub actual_use: ResourceMaps,
    pub unit_status: RwLock<UnitStatus>,
    pub url: RwLock<Option<String>>,
}

impl ResourceUnit {
    pub fn new(id: impl Into<String>) -> Self {
        let id = id.into();
        Self {
            revision: AtomicI64::new(0),
            view_init_time: RwLock::new(Uuid::new_v4().to_string()),
            owner_id: RwLock::new(id.clone()),
            capacity: ResourceMaps::default(),
            allocatable: ResourceMaps::default(),
            actual_use: ResourceMaps::default(),
            unit_status: RwLock::new(UnitStatus::Normal),
            url: RwLock::new(None),
            id,
        }
    }

    pub fn bump_revision(&self) -> i64 {
        self.revision.fetch_add(1, Ordering::SeqCst) + 1
    }
}

/// Initialize a unit with zero CPU/Memory scalars (resource_tool.h `InitResource`).
pub fn init_resource_unit(id: impl Into<String>) -> Arc<ResourceUnit> {
    let id = id.into();
    let u = Arc::new(ResourceUnit::new(id.clone()));
    for m in [&u.capacity, &u.allocatable, &u.actual_use] {
        m.scalar
            .insert(CPU_RESOURCE_NAME.to_string(), (0.0, 0.0));
        m.scalar
            .insert(MEMORY_RESOURCE_NAME.to_string(), (0.0, 0.0));
    }
    u
}

#[derive(Debug, Clone)]
pub struct InstanceAllocatedInfo {
    pub instance_id: String,
    pub request_id: String,
    /// Resources to deduct from allocatable (typically CPU/Memory scalars + device vectors).
    pub resources: ResourceMaps,
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct LocalResourceViewInfo {
    pub local_revision_in_domain: u64,
    pub agent_ids: HashSet<String>,
    pub local_view_init_time: String,
}

#[derive(Debug)]
pub struct ResourceViewInfo {
    pub resource_unit: Arc<RwLock<Arc<ResourceUnit>>>,
    pub scheduler_level: SchedulerLevel,
    pub already_scheduled: DashMap<String, String>,
    /// Outer key: unit or label namespace; inner: label key -> counter.
    pub all_local_labels: DashMap<String, DashMap<String, LabelCounter>>,
}

impl ResourceViewInfo {
    pub fn new(scheduler_level: SchedulerLevel) -> Self {
        Self {
            resource_unit: Arc::new(RwLock::new(init_resource_unit("view-root"))),
            scheduler_level,
            already_scheduled: DashMap::new(),
            all_local_labels: DashMap::new(),
        }
    }
}

type UpdateHandler = Arc<dyn Fn() + Send + Sync>;

/// Parameters mirroring `ResourceViewActor::Param`.
#[derive(Debug, Clone)]
pub struct ResourceViewParam {
    pub is_local: bool,
    pub enable_tenant_affinity: bool,
    pub tenant_pod_reuse_time_window: i32,
}

impl Default for ResourceViewParam {
    fn default() -> Self {
        Self {
            is_local: false,
            enable_tenant_affinity: true,
            tenant_pod_reuse_time_window: 10,
        }
    }
}

/// Concurrent resource view: multiple units, aggregate allocatable, instance bookkeeping.
pub struct ResourceView {
    view_id: String,
    param: ResourceViewParam,
    units: DashMap<String, Arc<RwLock<Arc<ResourceUnit>>>>,
    /// Aggregated view (merged capacity/allocatable/actual from all units).
    aggregate: Arc<RwLock<Arc<ResourceUnit>>>,
    inst_to_unit: DashMap<String, String>,
    handlers: RwLock<Vec<UpdateHandler>>,
    local_infos: DashMap<String, LocalResourceViewInfo>,
}

impl std::fmt::Debug for ResourceView {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("ResourceView")
            .field("view_id", &self.view_id)
            .field("units", &self.units.len())
            .finish_non_exhaustive()
    }
}

impl ResourceView {
    pub fn new(view_id: impl Into<String>, param: ResourceViewParam) -> Self {
        let agg = init_resource_unit("aggregate");
        Self {
            view_id: view_id.into(),
            param,
            units: DashMap::new(),
            aggregate: Arc::new(RwLock::new(agg)),
            inst_to_unit: DashMap::new(),
            handlers: RwLock::new(Vec::new()),
            local_infos: DashMap::new(),
        }
    }

    pub fn view_id(&self) -> &str {
        &self.view_id
    }

    pub fn param(&self) -> &ResourceViewParam {
        &self.param
    }

    fn notify(&self) {
        for h in self.handlers.read().iter() {
            h();
        }
    }

    pub fn add_resource_update_handler(&self, handler: UpdateHandler) {
        self.handlers.write().push(handler);
    }

    /// Merge one unit into the aggregate maps (simple sum / union / concat semantics).
    fn merge_unit_into_aggregate(&self, unit: &ResourceUnit, sign: i32) {
        let agg = self.aggregate.read().clone();
        if sign > 0 {
            ScalarResourceTool::add_to(&agg.capacity, &unit.capacity);
            ScalarResourceTool::add_to(&agg.allocatable, &unit.allocatable);
            ScalarResourceTool::add_to(&agg.actual_use, &unit.actual_use);
            SetResourceTool::union_into(&agg.capacity, &unit.capacity);
            SetResourceTool::union_into(&agg.allocatable, &unit.allocatable);
            SetResourceTool::union_into(&agg.actual_use, &unit.actual_use);
            VectorsResourceTool::merge_into(&agg.capacity, &unit.capacity);
            VectorsResourceTool::merge_into(&agg.allocatable, &unit.allocatable);
            VectorsResourceTool::merge_into(&agg.actual_use, &unit.actual_use);
        } else {
            ScalarResourceTool::sub_from(&agg.capacity, &unit.capacity);
            ScalarResourceTool::sub_from(&agg.allocatable, &unit.allocatable);
            ScalarResourceTool::sub_from(&agg.actual_use, &unit.actual_use);
            SetResourceTool::subtract_from(&agg.capacity, &unit.capacity);
            SetResourceTool::subtract_from(&agg.allocatable, &unit.allocatable);
            SetResourceTool::subtract_from(&agg.actual_use, &unit.actual_use);
            VectorsResourceTool::subtract_from(&agg.capacity, &unit.capacity);
            VectorsResourceTool::subtract_from(&agg.allocatable, &unit.allocatable);
            VectorsResourceTool::subtract_from(&agg.actual_use, &unit.actual_use);
        }
        agg.bump_revision();
    }

    fn rebuild_aggregate(&self) {
        let fresh = init_resource_unit("aggregate");
        for e in self.units.iter() {
            let u = e.value().read().clone();
            ScalarResourceTool::add_to(&fresh.capacity, &u.capacity);
            ScalarResourceTool::add_to(&fresh.allocatable, &u.allocatable);
            ScalarResourceTool::add_to(&fresh.actual_use, &u.actual_use);
            SetResourceTool::union_into(&fresh.capacity, &u.capacity);
            SetResourceTool::union_into(&fresh.allocatable, &u.allocatable);
            SetResourceTool::union_into(&fresh.actual_use, &u.actual_use);
            VectorsResourceTool::merge_into(&fresh.capacity, &u.capacity);
            VectorsResourceTool::merge_into(&fresh.allocatable, &u.allocatable);
            VectorsResourceTool::merge_into(&fresh.actual_use, &u.actual_use);
        }
        *self.aggregate.write() = fresh;
    }

    pub fn add_resource_unit(&self, unit: Arc<ResourceUnit>) -> Status {
        let id = unit.id.clone();
        if self.units.contains_key(&id) {
            return Status::new(StatusCode::ErrInstanceDuplicated, "resource unit already exists");
        }
        self.merge_unit_into_aggregate(unit.as_ref(), 1);
        self.units
            .insert(id, Arc::new(RwLock::new(unit.clone())));
        self.notify();
        Status::ok()
    }

    pub fn add_resource_unit_with_url(&self, unit: Arc<ResourceUnit>, url: impl Into<String>) -> Status {
        *unit.url.write() = Some(url.into());
        self.add_resource_unit(unit)
    }

    pub fn delete_resource_unit(&self, unit_id: &str) -> Status {
        let Some((_, slot)) = self.units.remove(unit_id) else {
            return Status::new(StatusCode::ErrInstanceNotFound, "resource unit not found");
        };
        let u = slot.read().clone();
        self.merge_unit_into_aggregate(u.as_ref(), -1);
        self.notify();
        Status::ok()
    }

    pub fn delete_local_resource_view(&self, local_id: &str) -> Status {
        self.local_infos.remove(local_id);
        self.delete_resource_unit(local_id)
    }

    pub fn get_resource_unit(&self, unit_id: &str) -> Option<Arc<ResourceUnit>> {
        self.units
            .get(unit_id)
            .map(|e| e.value().read().clone())
    }

    pub fn update_resource_unit(&self, unit: Arc<ResourceUnit>, _kind: UpdateType) -> Status {
        let id = unit.id.clone();
        let Some(entry) = self.units.get(&id) else {
            return Status::new(StatusCode::ErrInstanceNotFound, "resource unit not found");
        };
        let old = entry.value().read().clone();
        self.merge_unit_into_aggregate(old.as_ref(), -1);
        *entry.value().write() = unit;
        let new_u = entry.value().read().clone();
        self.merge_unit_into_aggregate(new_u.as_ref(), 1);
        new_u.bump_revision();
        self.notify();
        Status::ok()
    }

    pub fn clear_resource_view(&self) {
        self.units.clear();
        self.inst_to_unit.clear();
        *self.aggregate.write() = init_resource_unit("aggregate");
        self.notify();
    }

    pub fn add_instances(&self, insts: &HashMap<String, InstanceAllocatedInfo>) -> Status {
        for (k, info) in insts {
            if k.is_empty() || info.instance_id.is_empty() {
                return Status::new(StatusCode::ErrParamInvalid, "empty instance key or id");
            }
        }
        for (_k, info) in insts {
            let Some(first) = self.units.iter().next() else {
                return Status::new(StatusCode::ErrResourceNotEnough, "no resource units");
            };
            let unit_id = first.key().clone();
            let entry = self.units.get(&unit_id).unwrap();
            let u = entry.value().read().clone();
            if !ScalarResourceTool::allocatable_covers(&u.allocatable, &info.resources) {
                return Status::new(StatusCode::ErrResourceNotEnough, "scalar resources");
            }
            if !SetResourceTool::is_subset(&u.allocatable, &info.resources) {
                return Status::new(StatusCode::ErrResourceNotEnough, "set resources");
            }
            if !VectorsResourceTool::allocatable_covers(&u.allocatable, &info.resources) {
                return Status::new(StatusCode::ErrResourceNotEnough, "vector resources");
            }
        }
        for (_k, info) in insts {
            let unit_id = self.units.iter().next().unwrap().key().clone();
            let entry = self.units.get(&unit_id).unwrap();
            let u = entry.value().read().clone();
            ScalarResourceTool::sub_from(&u.allocatable, &info.resources);
            SetResourceTool::subtract_from(&u.allocatable, &info.resources);
            VectorsResourceTool::subtract_from(&u.allocatable, &info.resources);
            u.bump_revision();
            self.inst_to_unit
                .insert(info.request_id.clone(), unit_id);
        }
        self.rebuild_aggregate();
        self.notify();
        Status::ok()
    }

    pub fn delete_instances(&self, inst_ids: &[String], _is_virtual: bool) -> Status {
        for rid in inst_ids {
            self.inst_to_unit.remove(rid);
        }
        self.notify();
        Status::ok()
    }

    pub fn get_resource_view_copy(&self) -> Arc<ResourceUnit> {
        let g = self.aggregate.read().clone();
        Arc::new(clone_resource_unit_shallow(&g))
    }

    pub fn get_resource_view(&self) -> Arc<ResourceUnit> {
        self.aggregate.read().clone()
    }

    pub fn get_resource_info(&self) -> ResourceViewInfo {
        let level = if self.param.is_local {
            SchedulerLevel::Local
        } else {
            SchedulerLevel::NonRootDomain
        };
        ResourceViewInfo {
            resource_unit: Arc::clone(&self.aggregate),
            scheduler_level: level,
            already_scheduled: DashMap::new(),
            all_local_labels: DashMap::new(),
        }
    }

    pub fn get_unit_by_inst_req_id(&self, req: &str) -> Option<String> {
        self.inst_to_unit.get(req).map(|e| e.value().clone())
    }

    pub fn update_unit_status(&self, unit_id: &str, status: UnitStatus) -> Status {
        let Some(entry) = self.units.get(unit_id) else {
            return Status::new(StatusCode::ErrInstanceNotFound, "unit not found");
        };
        *entry.value().read().unit_status.write() = status;
        Status::ok()
    }

    pub fn get_local_info_in_domain(&self, local_id: &str) -> Option<LocalResourceViewInfo> {
        self.local_infos.get(local_id).map(|e| e.value().clone())
    }

    pub fn set_local_info_in_domain(&self, local_id: impl Into<String>, info: LocalResourceViewInfo) {
        self.local_infos.insert(local_id.into(), info);
    }
}

fn clone_resource_unit_shallow(src: &Arc<ResourceUnit>) -> ResourceUnit {
    ResourceUnit {
        id: src.id.clone(),
        revision: AtomicI64::new(src.revision.load(Ordering::SeqCst)),
        view_init_time: RwLock::new(src.view_init_time.read().clone()),
        owner_id: RwLock::new(src.owner_id.read().clone()),
        capacity: src.capacity.clone(),
        allocatable: src.allocatable.clone(),
        actual_use: src.actual_use.clone(),
        unit_status: RwLock::new(*src.unit_status.read()),
        url: RwLock::new(src.url.read().clone()),
    }
}

/// Owns primary and virtual [`ResourceView`] instances.
#[derive(Debug)]
pub struct ResourceViewMgr {
    id: RwLock<String>,
    param: RwLock<ResourceViewParam>,
    primary: RwLock<Option<Arc<ResourceView>>>,
    virtual_view: RwLock<Option<Arc<ResourceView>>>,
}

impl Default for ResourceViewMgr {
    fn default() -> Self {
        Self::new()
    }
}

impl ResourceViewMgr {
    pub fn new() -> Self {
        Self {
            id: RwLock::new(String::new()),
            param: RwLock::new(ResourceViewParam::default()),
            primary: RwLock::new(None),
            virtual_view: RwLock::new(None),
        }
    }

    pub fn init(&self, id: impl Into<String>, param: ResourceViewParam) {
        let id = id.into();
        *self.id.write() = id.clone();
        *self.param.write() = param.clone();
        *self.primary.write() = Some(Arc::new(ResourceView::new(format!("{id}-primary"), param.clone())));
        *self.virtual_view.write() = Some(Arc::new(ResourceView::new(format!("{id}-virtual"), param)));
    }

    pub fn get_inf(&self, ty: ResourceType) -> Arc<ResourceView> {
        let p = self.primary.read();
        let v = self.virtual_view.read();
        match ty {
            ResourceType::Primary => p
                .as_ref()
                .expect("ResourceViewMgr::init must be called")
                .clone(),
            ResourceType::Virtual => v
                .as_ref()
                .expect("ResourceViewMgr::init must be called")
                .clone(),
        }
    }

    pub fn get_resources(&self) -> HashMap<ResourceType, Arc<ResourceUnit>> {
        let mut m = HashMap::new();
        if let Some(p) = self.primary.read().as_ref() {
            m.insert(ResourceType::Primary, p.get_resource_view());
        }
        if let Some(v) = self.virtual_view.read().as_ref() {
            m.insert(ResourceType::Virtual, v.get_resource_view());
        }
        m
    }

    pub fn trigger_try_pull(&self) {
        // Hook for poller integration in upper layers.
    }

    pub fn update_domain_url_for_local(&self, _addr: &str) {
        // Placeholder for RPC URL updates.
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn init_resource_unit_has_cpu_memory() {
        let u = init_resource_unit("a");
        assert!(u.capacity.scalar.contains_key(CPU_RESOURCE_NAME));
        assert!(u.allocatable.scalar.contains_key(MEMORY_RESOURCE_NAME));
    }

    #[test]
    fn scalar_tool_add_sub() {
        let a = ResourceMaps::default();
        a.scalar.insert("CPU".into(), (2.0, 4.0));
        let b = ResourceMaps::default();
        b.scalar.insert("CPU".into(), (1.0, 1.0));
        ScalarResourceTool::add_to(&a, &b);
        assert!((a.scalar.get("CPU").unwrap().0 - 3.0).abs() < 1e-9);
        ScalarResourceTool::sub_from(&a, &b);
        assert!((a.scalar.get("CPU").unwrap().0 - 2.0).abs() < 1e-9);
    }

    #[test]
    fn set_tool_union_subset() {
        let a = ResourceMaps::default();
        a.sets.insert("l".into(), BTreeSet::from(["x".into()]));
        let b = ResourceMaps::default();
        b.sets.insert("l".into(), BTreeSet::from(["y".into()]));
        SetResourceTool::union_into(&a, &b);
        assert_eq!(a.sets.get("l").unwrap().len(), 2);
        assert!(SetResourceTool::is_subset(&a, &b));
    }

    #[test]
    fn vectors_tool_subtract() {
        let a = ResourceMaps::default();
        a.vectors
            .insert("GPU".into(), vec!["0".into(), "1".into()]);
        let b = ResourceMaps::default();
        b.vectors.insert("GPU".into(), vec!["0".into()]);
        assert!(VectorsResourceTool::allocatable_covers(&a, &b));
        VectorsResourceTool::subtract_from(&a, &b);
        assert_eq!(a.vectors.get("GPU").unwrap().as_slice(), &["1".to_string()]);
    }

    #[test]
    fn resource_view_add_unit_and_aggregate() {
        let v = ResourceView::new("v", ResourceViewParam::default());
        let u = init_resource_unit("u1");
        u.allocatable
            .scalar
            .insert(CPU_RESOURCE_NAME.to_string(), (4.0, 4.0));
        assert!(v.add_resource_unit(u).is_ok());
        let agg = v.get_resource_view();
        let cpu = agg.allocatable.scalar.get(CPU_RESOURCE_NAME).unwrap();
        assert!((cpu.0 - 4.0).abs() < 1e-9);
    }

    #[test]
    fn resource_view_mgr_init_and_get() {
        let m = ResourceViewMgr::new();
        m.init("cluster", ResourceViewParam::default());
        let _p = m.get_inf(ResourceType::Primary);
        let _vu = m.get_inf(ResourceType::Virtual);
        assert_eq!(m.get_resources().len(), 2);
    }

    #[test]
    fn hetero_device_compare_orders_by_id() {
        let a = HeteroDeviceInfo {
            device_id: "b".into(),
            ..Default::default()
        };
        let b = HeteroDeviceInfo {
            device_id: "a".into(),
            ..Default::default()
        };
        assert_eq!(HeteroDeviceCompare.cmp(&a, &b), std::cmp::Ordering::Greater);
    }

    #[test]
    fn resource_poller_add_try_pull() {
        use std::sync::atomic::AtomicUsize;
        let n = Arc::new(AtomicUsize::new(0));
        let n2 = Arc::clone(&n);
        let poller = ResourcePoller::new(
            move |_id: String| {
                n2.fetch_add(1, Ordering::SeqCst);
            },
            |_id: String| {},
            |_ms: u64| {},
            10,
        );
        poller.add("u1");
        poller.try_pull_resource();
        assert!(n.load(Ordering::SeqCst) >= 1);
    }
}
