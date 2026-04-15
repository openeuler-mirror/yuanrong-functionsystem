//! Periodic pull queue (port of `resource_poller.h`).

use dashmap::DashMap;
use parking_lot::Mutex;
use std::collections::VecDeque;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::time::{SystemTime, UNIX_EPOCH};

#[derive(Debug)]
pub struct ResourcePollInfo {
    pub id: String,
    pub latest_pulled_time_ms: i64,
}

impl ResourcePollInfo {
    pub fn new(id: impl Into<String>, latest_pulled_time_ms: i64) -> Self {
        Self {
            id: id.into(),
            latest_pulled_time_ms,
        }
    }
}

/// Coalesces pull requests with optional concurrency cap and cycle interval.
pub struct ResourcePoller {
    send_pull_resource: Arc<dyn Fn(String) + Send + Sync>,
    delegate_reset: Arc<dyn Fn(String) + Send + Sync>,
    defer_trigger_pull: Arc<dyn Fn(u64) + Send + Sync>,
    underlayers: DashMap<String, Arc<ResourcePollInfo>>,
    pulling: DashMap<String, ()>,
    to_poll: Mutex<VecDeque<Arc<ResourcePollInfo>>>,
    max_concurrency_pull: u32,
    stopped: Mutex<bool>,
}

impl std::fmt::Debug for ResourcePoller {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("ResourcePoller")
            .field("max_concurrency_pull", &self.max_concurrency_pull)
            .finish_non_exhaustive()
    }
}

static PULL_RESOURCE_CYCLE_MS: AtomicU64 = AtomicU64::new(500);

impl ResourcePoller {
    pub fn set_interval_ms(pull_resource_cycle_ms: u64) {
        PULL_RESOURCE_CYCLE_MS.store(pull_resource_cycle_ms, Ordering::SeqCst);
    }

    pub fn interval_ms() -> u64 {
        PULL_RESOURCE_CYCLE_MS.load(Ordering::SeqCst)
    }

    pub fn new(
        send_pull_resource: impl Fn(String) + Send + Sync + 'static,
        delegate_reset: impl Fn(String) + Send + Sync + 'static,
        defer_trigger_pull: impl Fn(u64) + Send + Sync + 'static,
        max_concurrency_pull: u32,
    ) -> Self {
        let cap = if max_concurrency_pull == 0 {
            super::MAX_CONCURRENCY_PULL
        } else {
            max_concurrency_pull
        };
        Self {
            send_pull_resource: Arc::new(send_pull_resource),
            delegate_reset: Arc::new(delegate_reset),
            defer_trigger_pull: Arc::new(defer_trigger_pull),
            underlayers: DashMap::new(),
            pulling: DashMap::new(),
            to_poll: Mutex::new(VecDeque::new()),
            max_concurrency_pull: cap,
            stopped: Mutex::new(false),
        }
    }

    pub fn stop(&self) {
        *self.stopped.lock() = true;
        self.to_poll.lock().clear();
    }

    pub fn add(&self, id: impl Into<String>) {
        if *self.stopped.lock() {
            return;
        }
        let id = id.into();
        // Eligible for immediate pull (matches typical "next pull" initialization).
        let info = Arc::new(ResourcePollInfo::new(id.clone(), 0));
        self.underlayers.insert(id.clone(), Arc::clone(&info));
        self.to_poll.lock().push_back(info);
    }

    pub fn del(&self, id: &str) {
        self.underlayers.remove(id);
        self.pulling.remove(id);
        let mut q = self.to_poll.lock();
        q.retain(|e| e.id != id);
    }

    pub fn reset(&self, id: &str) {
        (self.delegate_reset)(id.to_string());
        self.pulling.remove(id);
    }

    pub fn try_pull_resource(&self) {
        if *self.stopped.lock() {
            return;
        }
        let now = now_ms();
        let cycle = Self::interval_ms() as i64;
        let mut launched = 0u32;
        let mut q = self.to_poll.lock();
        let mut pending = VecDeque::new();
        while let Some(info) = q.pop_front() {
            if now - info.latest_pulled_time_ms < cycle {
                pending.push_back(Arc::clone(&info));
                continue;
            }
            if self.pulling.contains_key(&info.id) {
                pending.push_back(info);
                continue;
            }
            if launched >= self.max_concurrency_pull {
                pending.push_back(info);
                continue;
            }
            self.pulling.insert(info.id.clone(), ());
            let id = info.id.clone();
            if let Some(slot) = self.underlayers.get(&id) {
                // Update timestamp optimistically
                let new_info = Arc::new(ResourcePollInfo::new(id.clone(), now));
                drop(slot);
                self.underlayers.insert(id.clone(), Arc::clone(&new_info));
            }
            (self.send_pull_resource)(id);
            launched += 1;
        }
        q.extend(pending);
        if launched > 0 {
            (self.defer_trigger_pull)(Self::interval_ms());
        }
    }
}

fn now_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}
