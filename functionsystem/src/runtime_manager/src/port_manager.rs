use anyhow::{bail, Context};
use parking_lot::Mutex;
use std::collections::{HashMap, HashSet};

/// Allocates TCP ports from a fixed range and tracks ownership per `runtime_id`.
pub struct PortManager {
    base: u16,
    count: u32,
    free: Vec<u16>,
    by_runtime: HashMap<String, u16>,
}

impl PortManager {
    pub fn new(base: u16, count: u32) -> anyhow::Result<Self> {
        if count == 0 {
            bail!("port_count must be > 0");
        }
        let end = u64::from(base) + u64::from(count);
        if end > u64::from(u16::MAX) {
            bail!("port range overflows u16 (base={base}, count={count})");
        }
        let mut free: Vec<u16> = (0..count)
            .map(|i| {
                base.checked_add(i as u16).ok_or_else(|| {
                    anyhow::anyhow!("port range arithmetic overflow (base={base}, index={i})")
                })
            })
            .collect::<Result<_, _>>()?;
        free.reverse();
        Ok(Self {
            base,
            count,
            free,
            by_runtime: HashMap::new(),
        })
    }

    pub fn allocate(&mut self, runtime_id: &str) -> anyhow::Result<u16> {
        if self.by_runtime.contains_key(runtime_id) {
            bail!("runtime_id already has a port: {runtime_id}");
        }
        let p = self
            .free
            .pop()
            .with_context(|| format!("no free ports in pool [{}, {})", self.base, self.base as u32 + self.count))?;
        self.by_runtime.insert(runtime_id.to_string(), p);
        Ok(p)
    }

    pub fn release(&mut self, runtime_id: &str) -> Option<u16> {
        let p = self.by_runtime.remove(runtime_id)?;
        self.free.push(p);
        Some(p)
    }

    pub fn port_for(&self, runtime_id: &str) -> Option<u16> {
        self.by_runtime.get(runtime_id).copied()
    }
}

/// Thread-safe wrapper.
pub struct SharedPortManager {
    inner: Mutex<PortManager>,
}

impl SharedPortManager {
    pub fn new(base: u16, count: u32) -> anyhow::Result<Self> {
        Ok(Self {
            inner: Mutex::new(PortManager::new(base, count)?),
        })
    }

    pub fn allocate(&self, runtime_id: &str) -> anyhow::Result<u16> {
        self.inner.lock().allocate(runtime_id)
    }

    pub fn release(&self, runtime_id: &str) -> Option<u16> {
        self.inner.lock().release(runtime_id)
    }

    pub fn port_for(&self, runtime_id: &str) -> Option<u16> {
        self.inner.lock().port_for(runtime_id)
    }

    /// Snapshot of (runtime_id -> port) for metrics.
    pub fn snapshot_allocations(&self) -> HashMap<String, u16> {
        self.inner.lock().by_runtime.clone()
    }

    /// Distinct ports currently allocated (sanity).
    pub fn allocated_ports(&self) -> HashSet<u16> {
        self.inner
            .lock()
            .by_runtime
            .values()
            .copied()
            .collect()
    }
}
