//! Node labels and readiness (C++ function_agent node registration metadata).

use dashmap::DashMap;
use serde_json::{json, Value};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

#[derive(Debug)]
pub struct NodeManager {
    pub labels: DashMap<String, String>,
    ready: AtomicBool,
}

impl NodeManager {
    pub fn new_arc() -> Arc<Self> {
        Arc::new(Self {
            labels: DashMap::new(),
            ready: AtomicBool::new(false),
        })
    }

    pub fn set_label(&self, key: impl Into<String>, value: impl Into<String>) {
        self.labels.insert(key.into(), value.into());
    }

    pub fn remove_label(&self, key: &str) {
        self.labels.remove(key);
    }

    pub fn labels_json(&self) -> Value {
        let mut m = serde_json::Map::new();
        for e in self.labels.iter() {
            m.insert(e.key().clone(), json!(e.value().clone()));
        }
        json!(m)
    }

    pub fn set_ready(&self, v: bool) {
        self.ready.store(v, Ordering::SeqCst);
    }

    pub fn ready(&self) -> bool {
        self.ready.load(Ordering::SeqCst)
    }
}
