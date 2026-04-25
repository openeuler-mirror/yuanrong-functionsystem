//! Host-level disk/memory snapshot for scheduler reports (best-effort `/proc` + root `statvfs`).

use libc;
use serde_json::{json, Value};
use std::ffi::CString;
use std::fs;

/// Cheap node health block merged into `UpdateResources.resource_json` by the registration loop.
pub fn host_resource_snapshot() -> Value {
    let (mem_total, mem_avail, mem_ratio) = sample_mem_kb();
    let (disk_total, disk_avail, disk_ratio) = sample_disk_root();
    json!({
        "memory_total_kb": mem_total,
        "memory_available_kb": mem_avail,
        "memory_used_ratio": mem_ratio,
        "disk_total_bytes": disk_total,
        "disk_available_bytes": disk_avail,
        "disk_used_ratio": disk_ratio,
    })
}

fn sample_mem_kb() -> (u64, u64, f64) {
    let Ok(text) = fs::read_to_string("/proc/meminfo") else {
        return (0, 0, 0.0);
    };
    let mut total = 0u64;
    let mut avail = 0u64;
    for line in text.lines() {
        if line.starts_with("MemTotal:") {
            total = line
                .split_whitespace()
                .nth(1)
                .and_then(|s| s.parse().ok())
                .unwrap_or(0);
        } else if line.starts_with("MemAvailable:") {
            avail = line
                .split_whitespace()
                .nth(1)
                .and_then(|s| s.parse().ok())
                .unwrap_or(0);
        }
    }
    let ratio = if total > 0 {
        ((total.saturating_sub(avail)) as f64 / total as f64).clamp(0.0, 1.0)
    } else {
        0.0
    };
    (total, avail, ratio)
}

fn sample_disk_root() -> (u64, u64, f64) {
    let mut vfs: libc::statvfs = unsafe { std::mem::zeroed() };
    let root = CString::new("/").unwrap();
    let rc = unsafe { libc::statvfs(root.as_ptr(), &mut vfs) };
    if rc != 0 {
        return (0, 0, 0.0);
    }
    let frsize = vfs.f_frsize as u64;
    let blocks = vfs.f_blocks as u64;
    let bavail = vfs.f_bavail as u64;
    let total = blocks.saturating_mul(frsize);
    let avail = bavail.saturating_mul(frsize);
    let ratio = if total > 0 {
        ((total.saturating_sub(avail)) as f64 / total as f64).clamp(0.0, 1.0)
    } else {
        0.0
    };
    (total, avail, ratio)
}
