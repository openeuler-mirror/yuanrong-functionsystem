//! cgroup v2 CPU/memory limits and optional Linux namespace isolation (nix).

use crate::config::Config;
use anyhow::Context;
use nix::sched::{unshare, CloneFlags};
use std::collections::HashMap;
use std::fs;
use std::io::Write;
use std::path::{Path, PathBuf};

/// Best-effort NVIDIA / Ascend NPU device presence (device-node scan).
pub fn detect_accelerators() -> AcceleratorSnapshot {
    let nvidia = count_dev_prefix(Path::new("/dev"), "nvidia");
    let davinci = count_dev_prefix(Path::new("/dev"), "davinci");
    AcceleratorSnapshot { nvidia, davinci }
}

#[derive(Debug, Clone, Default, serde::Serialize)]
pub struct AcceleratorSnapshot {
    pub nvidia: u32,
    pub davinci: u32,
}

fn count_dev_prefix(dir: &Path, prefix: &str) -> u32 {
    let Ok(rd) = std::fs::read_dir(dir) else {
        return 0;
    };
    rd.filter_map(Result::ok)
        .filter(|e| {
            e.file_name()
                .to_str()
                .is_some_and(|n| n.starts_with(prefix))
        })
        .count() as u32
}

pub fn remove_cgroup_dir(path: &Path) {
    let _ = fs::remove_dir(path);
}

/// Best-effort `/proc/<pid>/oom_score_adj` for runtime children (clamped −1000..1000).
pub fn adjust_oom_score_adj(pid: i32, adj: i32) {
    if pid <= 0 {
        return;
    }
    let clamped = adj.clamp(-1000, 1000);
    let path = format!("/proc/{pid}/oom_score_adj");
    if let Err(e) = fs::write(&path, format!("{clamped}\n")) {
        tracing::debug!(error = %e, %pid, "oom_score_adj write skipped");
    }
}

pub struct CgroupIsolate {
    pub cgroup_path: PathBuf,
}

impl CgroupIsolate {
    /// Create a cgroup under `cfg.cgroup_parent` and attach `pid` with optional CPU/memory caps.
    pub fn apply(
        cfg: &Config,
        runtime_id: &str,
        pid: i32,
        resources: &HashMap<String, f64>,
    ) -> anyhow::Result<Option<Self>> {
        let base = &cfg.cgroup_parent;
        if base.as_os_str().is_empty() || !base.exists() {
            return Ok(None);
        }
        let safe_id = runtime_id
            .chars()
            .map(|c| {
                if c.is_alphanumeric() || c == '-' {
                    c
                } else {
                    '_'
                }
            })
            .collect::<String>();
        let cg = base.join(&safe_id);
        fs::create_dir_all(&cg).with_context(|| format!("mkdir cgroup {}", cg.display()))?;

        if let Err(e) = enable_controllers_parent(cfg) {
            tracing::warn!(error = %e, "cgroup subtree_control (parent); continuing without full limits");
        }

        if cfg.cgroup_enable_cpu {
            if let Some(quota) = cpu_quota_nanos(resources.get("cpu").copied()) {
                let max = format!("{quota} 100000");
                write_ctrl(&cg, "cpu.max", &max)?;
            }
        }
        if cfg.cgroup_enable_memory {
            if let Some(bytes) = memory_max_bytes_for_resource(resources.get("memory").copied()) {
                write_ctrl(&cg, "memory.max", &bytes.to_string())?;
            }
        }

        let procs = cg.join("cgroup.procs");
        let mut f = fs::OpenOptions::new()
            .append(true)
            .open(&procs)
            .with_context(|| format!("open {}", procs.display()))?;
        writeln!(f, "{pid}")?;

        Ok(Some(Self { cgroup_path: cg }))
    }
}

fn enable_controllers_parent(cfg: &Config) -> std::io::Result<()> {
    let parent = cfg.cgroup_parent.parent().ok_or_else(|| {
        std::io::Error::new(
            std::io::ErrorKind::InvalidInput,
            "cgroup_parent has no parent",
        )
    })?;
    let sc = parent.join("cgroup.subtree_control");
    if !sc.exists() {
        return Ok(());
    }
    let mut line = String::new();
    if let Ok(s) = fs::read_to_string(&sc) {
        line = s;
    }
    let mut add = Vec::new();
    if cfg.cgroup_enable_cpu && !line.contains("+cpu") {
        add.push("+cpu");
    }
    if cfg.cgroup_enable_memory && !line.contains("+memory") {
        add.push("+memory");
    }
    for a in add {
        let mut f = fs::OpenOptions::new().append(true).open(&sc)?;
        writeln!(f, "{a}")?;
    }
    Ok(())
}

fn write_ctrl(cg: &Path, name: &str, val: &str) -> anyhow::Result<()> {
    let p = cg.join(name);
    fs::write(&p, val).with_context(|| format!("write {} = {}", p.display(), val))
}

/// Applied inside `pre_exec` when namespace isolation is enabled.
pub unsafe fn maybe_unshare_namespaces(isolate: bool) -> std::io::Result<()> {
    if !isolate {
        return Ok(());
    }
    unshare(CloneFlags::CLONE_NEWIPC | CloneFlags::CLONE_NEWUTS | CloneFlags::CLONE_NEWNS)
        .map_err(|e| std::io::Error::other(e.to_string()))
}

/// cgroup v2 `cpu.max` first field: quota in microseconds per 100ms period (100000 us).
fn cpu_quota_nanos(cpu_cores: Option<f64>) -> Option<u64> {
    let c = cpu_cores?;
    if c <= 0.0 {
        return None;
    }
    let us = (c * 100_000.0).round() as u64;
    Some(us.clamp(1000, 100_000 * 1024))
}

pub fn memory_max_bytes_for_resource(mem_mb: Option<f64>) -> Option<u64> {
    let mb = mem_mb?;
    if mb <= 0.0 {
        return None;
    }
    let bytes = (mb * 1024.0 * 1024.0).ceil() as u64;
    Some(bytes.max(4 * 1024 * 1024))
}
