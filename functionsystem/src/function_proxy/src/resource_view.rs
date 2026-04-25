use parking_lot::Mutex;
use serde::Serialize;
use std::collections::HashMap;
use std::sync::Arc;

#[derive(Debug, Clone, Default, Serialize)]
pub struct ResourceVector {
    pub cpu: f64,
    pub memory: f64,
    pub npu: f64,
}

impl ResourceVector {
    pub fn from_required(map: &HashMap<String, f64>) -> Self {
        Self {
            cpu: *map.get("cpu").unwrap_or(&0.0),
            memory: *map.get("memory").unwrap_or(&0.0),
            npu: *map.get("npu").or_else(|| map.get("ascend")).unwrap_or(&0.0),
        }
    }

    pub fn add_assign(&mut self, o: &ResourceVector) {
        self.cpu += o.cpu;
        self.memory += o.memory;
        self.npu += o.npu;
    }

    pub fn sub_assign(&mut self, o: &ResourceVector) {
        self.cpu -= o.cpu;
        self.memory -= o.memory;
        self.npu -= o.npu;
    }
}

/// Tracks node capacity, committed usage, and soft reservations for in-flight schedules.
#[derive(Debug)]
pub struct ResourceView {
    capacity: ResourceVector,
    used: Mutex<ResourceVector>,
    pending: Mutex<ResourceVector>,
}

impl ResourceView {
    pub fn new(capacity: ResourceVector) -> Arc<Self> {
        Arc::new(Self {
            capacity,
            used: Mutex::new(ResourceVector::default()),
            pending: Mutex::new(ResourceVector::default()),
        })
    }

    fn fits(&self, used: &ResourceVector, pending: &ResourceVector, req: &ResourceVector) -> bool {
        used.cpu + pending.cpu + req.cpu <= self.capacity.cpu
            && used.memory + pending.memory + req.memory <= self.capacity.memory
            && used.npu + pending.npu + req.npu <= self.capacity.npu
    }

    /// Pre-deduct capacity for a pending schedule (returns false if it does not fit).
    pub fn reserve_pending(&self, req: &HashMap<String, f64>) -> bool {
        let v = ResourceVector::from_required(req);
        let used = self.used.lock();
        let mut pending = self.pending.lock();
        if !self.fits(&used, &pending, &v) {
            return false;
        }
        pending.add_assign(&v);
        true
    }

    pub fn release_pending(&self, req: &HashMap<String, f64>) {
        let v = ResourceVector::from_required(req);
        let mut pending = self.pending.lock();
        pending.sub_assign(&v);
    }

    /// Move reservation into committed usage (after instance is placed).
    pub fn commit_pending_to_used(&self, req: &HashMap<String, f64>) {
        let v = ResourceVector::from_required(req);
        let mut used = self.used.lock();
        let mut pending = self.pending.lock();
        pending.sub_assign(&v);
        used.add_assign(&v);
    }

    /// Release committed usage (instance stopped).
    pub fn release_used(&self, req: &HashMap<String, f64>) {
        let v = ResourceVector::from_required(req);
        let mut used = self.used.lock();
        used.sub_assign(&v);
    }

    /// Add committed usage without a prior pending reservation (MetaStore recovery / rehydrate).
    pub fn adopt_used(&self, req: &HashMap<String, f64>) {
        let v = ResourceVector::from_required(req);
        let mut used = self.used.lock();
        used.add_assign(&v);
    }

    pub fn snapshot_json(&self) -> String {
        let used = self.used.lock().clone();
        let pending = self.pending.lock().clone();
        let snap = serde_json::json!({
            "capacity": self.capacity,
            "used": used,
            "pending": pending,
        });
        snap.to_string()
    }

    pub(crate) fn used_snapshot(&self) -> ResourceVector {
        self.used.lock().clone()
    }

    pub(crate) fn pending_snapshot(&self) -> ResourceVector {
        self.pending.lock().clone()
    }

    pub(crate) fn capacity_snapshot(&self) -> ResourceVector {
        self.capacity.clone()
    }

    #[cfg(test)]
    pub fn used_cpu(&self) -> f64 {
        self.used.lock().cpu
    }
}
