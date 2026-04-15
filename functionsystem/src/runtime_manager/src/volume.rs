//! Bind mounts for function code / data directories (Linux).

use anyhow::Context;
use std::fs;

#[derive(Debug, Clone)]
pub struct BindMount {
    pub src: std::path::PathBuf,
    pub dst: std::path::PathBuf,
    pub read_only: bool,
}

#[cfg(target_os = "linux")]
pub fn apply_bind_mounts(mounts: &[BindMount]) -> anyhow::Result<Vec<std::path::PathBuf>> {
    use nix::mount::{mount, MsFlags};
    let mut applied = Vec::new();
    for m in mounts {
        if !m.src.exists() {
            anyhow::bail!("bind mount source missing: {}", m.src.display());
        }
        if let Some(parent) = m.dst.parent() {
            fs::create_dir_all(parent).with_context(|| parent.display().to_string())?;
        }
        if m.dst.exists() {
            let _ = fs::remove_dir_all(&m.dst);
        }
        if m.src.is_dir() {
            fs::create_dir_all(&m.dst).with_context(|| m.dst.display().to_string())?;
        } else {
            if let Some(p) = m.dst.parent() {
                fs::create_dir_all(p)?;
            }
            fs::write(&m.dst, []).with_context(|| m.dst.display().to_string())?;
        }

        mount(
            Some(&m.src),
            &m.dst,
            None::<&str>,
            MsFlags::MS_BIND,
            None::<&str>,
        )
        .with_context(|| format!("bind mount {} -> {}", m.src.display(), m.dst.display()))?;

        if m.read_only {
            mount(
                Some(&m.dst),
                &m.dst,
                None::<&str>,
                MsFlags::MS_BIND | MsFlags::MS_REMOUNT | MsFlags::MS_RDONLY,
                None::<&str>,
            )
            .with_context(|| format!("remount ro {}", m.dst.display()))?;
        }
        applied.push(m.dst.clone());
    }
    Ok(applied)
}

#[cfg(target_os = "linux")]
pub fn unmount_all(points: &[std::path::PathBuf]) {
    for p in points.iter().rev() {
        let _ = nix::mount::umount(p.as_path());
    }
}

#[cfg(not(target_os = "linux"))]
pub fn apply_bind_mounts(_mounts: &[BindMount]) -> anyhow::Result<Vec<std::path::PathBuf>> {
    Ok(vec![])
}

#[cfg(not(target_os = "linux"))]
pub fn unmount_all(_points: &[std::path::PathBuf]) {}
