//! C++ busproxy invoke memory-admission compatibility.

use crate::config::Config;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};

pub const DEFAULT_LOW_MEMORY_THRESHOLD: f64 = 0.6;
pub const DEFAULT_HIGH_MEMORY_THRESHOLD: f64 = 0.8;
pub const DEFAULT_MESSAGE_SIZE_THRESHOLD: u64 = 20 * 1024;
pub const MAXIMUM_MESSAGE_SIZE_THRESHOLD: u64 = 50 * 1024;
pub const MSG_ESTIMATED_FACTOR: usize = 2;

#[derive(Debug, Clone)]
pub struct InvokeMemoryConfig {
    pub enable: bool,
    pub low_memory_threshold: f64,
    pub high_memory_threshold: f64,
    pub message_size_threshold: u64,
}

impl Default for InvokeMemoryConfig {
    fn default() -> Self {
        Self {
            enable: false,
            low_memory_threshold: DEFAULT_LOW_MEMORY_THRESHOLD,
            high_memory_threshold: DEFAULT_HIGH_MEMORY_THRESHOLD,
            message_size_threshold: DEFAULT_MESSAGE_SIZE_THRESHOLD,
        }
    }
}

impl InvokeMemoryConfig {
    pub fn from_proxy_config(config: &Config) -> Self {
        let mut out = Self {
            enable: config.invoke_limitation_enable,
            ..Self::default()
        };
        if !out.enable {
            return out;
        }
        if config.low_memory_threshold > 0.0
            && config.high_memory_threshold < 1.0
            && config.low_memory_threshold < config.high_memory_threshold
        {
            out.low_memory_threshold = config.low_memory_threshold;
            out.high_memory_threshold = config.high_memory_threshold;
        }
        if config.message_size_threshold > 0
            && config.message_size_threshold < MAXIMUM_MESSAGE_SIZE_THRESHOLD
        {
            out.message_size_threshold = config.message_size_threshold;
        }
        out
    }
}

#[derive(Debug, Default)]
pub struct InvokeMemoryMonitor {
    config: InvokeMemoryConfig,
    estimated_usage: AtomicU64,
    instance_usage: Mutex<HashMap<String, u64>>,
    request_size: Mutex<HashMap<String, u64>>,
}

impl InvokeMemoryMonitor {
    pub fn new(config: InvokeMemoryConfig) -> Self {
        Self {
            config,
            estimated_usage: AtomicU64::new(0),
            instance_usage: Mutex::new(HashMap::new()),
            request_size: Mutex::new(HashMap::new()),
        }
    }

    pub fn from_proxy_config(config: &Config) -> Self {
        Self::new(InvokeMemoryConfig::from_proxy_config(config))
    }

    pub fn is_enabled(&self) -> bool {
        self.config.enable
    }

    pub fn estimated_usage(&self) -> u64 {
        self.estimated_usage.load(Ordering::Acquire)
    }

    pub fn allow(&self, instance_id: &str, request_id: &str, msg_size: u64) -> bool {
        if !self.is_enabled() {
            return true;
        }
        let Some((limit_usage, current_usage)) = current_memory_usage() else {
            return true;
        };
        self.allow_with_usage(
            instance_id,
            request_id,
            msg_size,
            limit_usage,
            current_usage,
        )
    }

    pub fn allow_with_usage(
        &self,
        instance_id: &str,
        request_id: &str,
        msg_size: u64,
        limit_usage: u64,
        current_usage: u64,
    ) -> bool {
        if !self.is_enabled() {
            return true;
        }
        let estimate_usage = self.estimated_usage();
        let high_threshold = (limit_usage as f64 * self.config.high_memory_threshold) as u64;
        let low_threshold = (limit_usage as f64 * self.config.low_memory_threshold) as u64;

        if current_usage.checked_add(msg_size).is_none() {
            return false;
        }
        if current_usage.saturating_add(msg_size) > high_threshold {
            return false;
        }
        if msg_size <= self.config.message_size_threshold {
            return true;
        }
        if current_usage <= low_threshold && estimate_usage <= low_threshold {
            self.allocate(instance_id, request_id, msg_size);
            return true;
        }
        let instance_usage = self.instance_usage(instance_id);
        let average_usage = self.average_usage(estimate_usage);
        if instance_usage == 0 {
            self.allocate(instance_id, request_id, msg_size);
            return true;
        }
        if instance_usage <= average_usage {
            self.allocate(instance_id, request_id, msg_size);
            return true;
        }
        false
    }

    pub fn release(&self, instance_id: &str, request_id: &str) {
        let Some(msg_size) = self.request_size.lock().remove(request_id) else {
            return;
        };
        self.estimated_usage
            .fetch_update(Ordering::AcqRel, Ordering::Acquire, |old| {
                Some(old.saturating_sub(msg_size))
            })
            .ok();
        let mut usage = self.instance_usage.lock();
        match usage.get_mut(instance_id) {
            Some(v) if *v > msg_size => *v -= msg_size,
            Some(_) => {
                usage.remove(instance_id);
            }
            None => {}
        }
    }

    fn allocate(&self, instance_id: &str, request_id: &str, msg_size: u64) {
        self.estimated_usage
            .fetch_update(Ordering::AcqRel, Ordering::Acquire, |old| {
                Some(old.saturating_add(msg_size))
            })
            .ok();
        self.instance_usage
            .lock()
            .entry(instance_id.to_string())
            .and_modify(|v| *v = v.saturating_add(msg_size))
            .or_insert(msg_size);
        self.request_size
            .lock()
            .insert(request_id.to_string(), msg_size);
    }

    fn instance_usage(&self, instance_id: &str) -> u64 {
        self.instance_usage
            .lock()
            .get(instance_id)
            .copied()
            .unwrap_or(0)
    }

    fn average_usage(&self, estimate_usage: u64) -> u64 {
        estimate_usage / (self.instance_usage.lock().len() as u64 + 1)
    }
}

fn current_memory_usage() -> Option<(u64, u64)> {
    if let (Some(current), Some(limit)) = (
        read_u64("/sys/fs/cgroup/memory.current"),
        read_cgroup_limit(),
    ) {
        return Some((limit, current));
    }
    let meminfo = std::fs::read_to_string("/proc/meminfo").ok()?;
    let mut total = None;
    let mut available = None;
    for line in meminfo.lines() {
        if let Some(rest) = line.strip_prefix("MemTotal:") {
            total = parse_meminfo_kb(rest);
        } else if let Some(rest) = line.strip_prefix("MemAvailable:") {
            available = parse_meminfo_kb(rest);
        }
    }
    let total = total?;
    let current = total.saturating_sub(available.unwrap_or(0));
    Some((total, current))
}

fn read_cgroup_limit() -> Option<u64> {
    let raw = std::fs::read_to_string("/sys/fs/cgroup/memory.max")
        .ok()?
        .trim()
        .to_string();
    if raw == "max" {
        return None;
    }
    raw.parse().ok()
}

fn read_u64(path: &str) -> Option<u64> {
    std::fs::read_to_string(path).ok()?.trim().parse().ok()
}

fn parse_meminfo_kb(rest: &str) -> Option<u64> {
    rest.split_whitespace()
        .next()?
        .parse::<u64>()
        .ok()?
        .checked_mul(1024)
}
