//! Port of `tenant_cooldown_manager.h` using cancel callbacks instead of LiteBus timers.

use std::collections::HashMap;

struct Entry {
    generation: u64,
    cancel: Option<Box<dyn FnOnce() + Send>>,
}

/// Thread-unsafe per-tenant cooldown table (drive from a single async task / actor thread).
pub struct TenantCooldownManager {
    entries: HashMap<String, Entry>,
}

impl TenantCooldownManager {
    pub fn new() -> Self {
        Self {
            entries: HashMap::new(),
        }
    }

    /// Cancels any previous timer, bumps generation, then stores the cancel closure returned by `schedule_timer`.
    pub fn apply<F>(&mut self, tenant_id: &str, mut schedule_timer: F)
    where
        F: FnMut(u64) -> Box<dyn FnOnce() + Send>,
    {
        if tenant_id.is_empty() {
            return;
        }
        let e = self.entries.entry(tenant_id.to_string()).or_insert(Entry {
            generation: 0,
            cancel: None,
        });
        if let Some(c) = e.cancel.take() {
            c();
        }
        e.generation += 1;
        let g = e.generation;
        e.cancel = Some(schedule_timer(g));
    }

    pub fn is_blocked(&self, tenant_id: &str) -> bool {
        self.entries.contains_key(tenant_id)
    }

    /// Only clears when `generation` still matches (stale callbacks are ignored).
    pub fn on_expired(&mut self, tenant_id: &str, generation: u64) -> bool {
        let Some(e) = self.entries.get(tenant_id) else {
            return false;
        };
        if e.generation != generation {
            return false;
        }
        self.entries.remove(tenant_id);
        true
    }

    pub fn cancel_all(&mut self) {
        for (_, mut e) in self.entries.drain() {
            if let Some(c) = e.cancel.take() {
                c();
            }
        }
    }
}

impl Default for TenantCooldownManager {
    fn default() -> Self {
        Self::new()
    }
}

impl Drop for TenantCooldownManager {
    fn drop(&mut self) {
        self.cancel_all();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stale_generation_is_ignored() {
        let mut m = TenantCooldownManager::new();
        m.apply("t", |gen| {
            assert_eq!(gen, 1);
            Box::new(|| {})
        });
        assert!(m.is_blocked("t"));
        m.apply("t", |gen| {
            assert_eq!(gen, 2);
            Box::new(|| {})
        });
        assert!(!m.on_expired("t", 1));
        assert!(m.is_blocked("t"));
        assert!(m.on_expired("t", 2));
        assert!(!m.is_blocked("t"));
    }

    #[test]
    fn cancel_invoked_on_reapply() {
        let mut m = TenantCooldownManager::new();
        let cnt = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
        let c2 = cnt.clone();
        m.apply("t", move |_| {
            let c2 = c2.clone();
            Box::new(move || {
                c2.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
            })
        });
        m.apply("t", |_| Box::new(|| {}));
        assert_eq!(cnt.load(std::sync::atomic::Ordering::SeqCst), 1);
    }
}
