//! Per-instance stdout/stderr paths, size-based rotation before append, optional line forwarding.

use anyhow::Context;
use std::fs::{self, OpenOptions};
use std::path::{Path, PathBuf};
use tracing::debug;

/// Rotate `path` to `path.N` when over `max_bytes`, keeping up to `keep` numbered backups.
pub fn rotate_if_needed(path: &Path, max_bytes: u64, keep: u32) -> anyhow::Result<()> {
    let meta = match fs::metadata(path) {
        Ok(m) => m,
        Err(_) => return Ok(()),
    };
    if meta.len() <= max_bytes {
        return Ok(());
    }
    let base = path.display().to_string();
    for i in (1..=keep).rev() {
        let from = if i == 1 {
            base.clone()
        } else {
            format!("{base}.{}", i - 1)
        };
        let to = format!("{base}.{i}");
        if Path::new(&from).exists() {
            let _ = fs::rename(&from, &to);
        }
    }
    Ok(())
}

pub struct InstanceLogPaths {
    pub stdout_path: PathBuf,
    pub stderr_path: PathBuf,
}

impl InstanceLogPaths {
    pub fn new(log_dir: &Path, runtime_id: &str) -> Self {
        Self {
            stdout_path: log_dir.join(format!("{runtime_id}.stdout.log")),
            stderr_path: log_dir.join(format!("{runtime_id}.stderr.log")),
        }
    }

    pub fn open_append_rotated(
        &self,
        max_bytes: u64,
        keep: u32,
    ) -> anyhow::Result<(std::fs::File, std::fs::File)> {
        rotate_if_needed(&self.stdout_path, max_bytes, keep)?;
        rotate_if_needed(&self.stderr_path, max_bytes, keep)?;
        let stdout = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&self.stdout_path)
            .with_context(|| format!("open {}", self.stdout_path.display()))?;
        let stderr = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&self.stderr_path)
            .with_context(|| format!("open {}", self.stderr_path.display()))?;
        Ok((stdout, stderr))
    }
}

/// Best-effort forward of a log line to tracing (structured log aggregation).
pub fn forward_line(stream: &str, line: &[u8]) {
    if line.is_empty() {
        return;
    }
    let s = String::from_utf8_lossy(line);
    debug!(target: "yr_rm_instance_log", stream, line = %s.trim_end());
}
