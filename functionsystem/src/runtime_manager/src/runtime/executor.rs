use crate::config::Config;
use crate::container::{self, CgroupIsolate};
use crate::instance_health;
use crate::state::InstanceHealthSpec;
use crate::log_manager::InstanceLogPaths;
use crate::state::RunningProcess;
use crate::venv;
use crate::volume::{self, BindMount};
use anyhow::{anyhow, Context};
use nix::sys::signal::{kill, Signal};
use nix::unistd::Pid;
use std::collections::HashMap;
use std::os::unix::process::CommandExt;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};
use tracing::{info, warn};
use yr_proto::internal::StartInstanceRequest;

/// Resolve executable path from `runtime_type` and configured path list.
pub fn pick_runtime_executable(paths: &[String], runtime_type: &str) -> Option<String> {
    if paths.is_empty() {
        return None;
    }
    if let Ok(idx) = runtime_type.parse::<usize>() {
        if let Some(p) = paths.get(idx) {
            return Some(p.clone());
        }
    }
    paths
        .iter()
        .find(|p| {
            Path::new(p)
                .file_name()
                .and_then(|n| n.to_str())
                .is_some_and(|n| n.contains(runtime_type))
                || p.contains(runtime_type)
        })
        .cloned()
        .or_else(|| paths.first().cloned())
}

unsafe fn apply_rlimits_libc(resources: &HashMap<String, f64>) {
    if let Some(mem) = resources.get("memory").copied() {
        if mem > 0.0 {
            let bytes = (mem * 1024.0 * 1024.0 * 1024.0).ceil() as libc::rlim_t;
            let lim = libc::rlimit {
                rlim_cur: bytes,
                rlim_max: bytes,
            };
            let _ = libc::setrlimit(libc::RLIMIT_AS, &lim);
        }
    }
    if let Some(cpu) = resources.get("cpu").copied() {
        if cpu > 0.0 {
            let sec = (cpu * 3600.0).ceil().max(60.0) as libc::rlim_t;
            let lim = libc::rlimit {
                rlim_cur: sec,
                rlim_max: sec,
            };
            let _ = libc::setrlimit(libc::RLIMIT_CPU, &lim);
        }
    }
}

fn resolve_workdir(req: &StartInstanceRequest) -> PathBuf {
    if req.code_path.trim().is_empty() {
        PathBuf::from(".")
    } else {
        PathBuf::from(&req.code_path)
    }
}

fn bind_mounts_for_instance(cfg: &Config, workdir: &Path) -> Vec<BindMount> {
    cfg.parse_bind_mounts()
        .into_iter()
        .map(|mut m| {
            if m.dst.is_relative() {
                m.dst = workdir.join(&m.dst);
            }
            m
        })
        .collect()
}

/// Spawn a runtime child with env, logs, cgroups, optional bind mounts, venv hints.
pub fn start_runtime_process(
    cfg: &Config,
    req: &StartInstanceRequest,
    paths: &[String],
    runtime_id: &str,
    port: u16,
) -> anyhow::Result<RunningProcess> {
    let exe = pick_runtime_executable(paths, &req.runtime_type)
        .ok_or_else(|| anyhow!("no runtime executable configured"))?;
    cfg.ensure_log_dir()?;

    let workdir = resolve_workdir(req);
    let _ = venv::prepare_python_env(&workdir, &req.runtime_type);
    let java_env = venv::java_env_hints(&workdir, &req.runtime_type);

    let mounts = bind_mounts_for_instance(cfg, &workdir);
    let mount_points = volume::apply_bind_mounts(&mounts).unwrap_or_else(|e| {
        tracing::warn!(error = %e, "bind mounts skipped");
        Vec::new()
    });

    let logs = InstanceLogPaths::new(&cfg.log_path, runtime_id);
    let (stdout, stderr) = logs.open_append_rotated(cfg.log_rotate_max_bytes, cfg.log_rotate_keep)?;

    let health_spec: InstanceHealthSpec =
        instance_health::parse_from_config_json(
            &req.config_json,
            Duration::from_secs(cfg.manager_startup_probe_secs.max(1)),
        );

    let listen_addr = format!("{}:{}", cfg.host.trim(), port);

    let mut cmd = Command::new(&exe);
    cmd.arg(&req.instance_id)
        .current_dir(&workdir)
        .stdin(Stdio::null())
        .stdout(Stdio::from(stdout))
        .stderr(Stdio::from(stderr))
        .env("INSTANCE_ID", &req.instance_id)
        .env("RUNTIME_ID", runtime_id)
        .env("YR_RUNTIME_ID", runtime_id)
        .env("FUNCTION_NAME", &req.function_name)
        .env("TENANT_ID", &req.tenant_id)
        .env("RUNTIME_PORT", port.to_string())
        .env("RUNTIME_TYPE", &req.runtime_type)
        .env("POSIX_LISTEN_ADDR", &listen_addr)
        .env("YR_JOB_ID", format!("job-{}", &req.instance_id.get(..8).unwrap_or("ffffffff")))
        .env("PYTHONUNBUFFERED", "1")
        .env("YR_BARE_MENTAL", "1");

    for (k, v) in &req.env_vars {
        cmd.env(k, v);
    }
    for (k, v) in java_env {
        cmd.env(k, v);
    }

    if let Ok(ld) = std::env::var("LD_LIBRARY_PATH") {
        cmd.env("LD_LIBRARY_PATH", &ld);
    }
    if let Ok(pp) = std::env::var("PYTHONPATH") {
        cmd.env("PYTHONPATH", &pp);
    }
    if let Ok(path) = std::env::var("PATH") {
        cmd.env("PATH", &path);
    }

    let res = req.resources.clone();
    let isolate = cfg.isolate_namespaces;
    unsafe {
        cmd.pre_exec(move || {
            let _ = container::maybe_unshare_namespaces(isolate);
            apply_rlimits_libc(&res);
            Ok(())
        });
    }

    let child = cmd.spawn().with_context(|| format!("spawn runtime {exe:?}"))?;
    let pid = child.id() as i32;

    let cgroup_path = match CgroupIsolate::apply(cfg, runtime_id, pid, &req.resources) {
        Ok(Some(cg)) => Some(cg.cgroup_path),
        Ok(None) => None,
        Err(e) => {
            tracing::warn!(error = %e, "cgroup attach failed");
            None
        }
    };

    std::mem::forget(child);

    info!(
        %runtime_id,
        pid,
        port,
        exe = %exe,
        "started runtime process"
    );

    Ok(RunningProcess {
        instance_id: req.instance_id.clone(),
        runtime_id: runtime_id.to_string(),
        pid,
        port,
        status: "running".into(),
        exit_code: None,
        error_message: String::new(),
        cgroup_path,
        bind_mount_points: mount_points,
        health_spec,
        started_at: Instant::now(),
        resources: req.resources.clone(),
    })
}

/// SIGTERM, poll `/proc`, then SIGKILL. Cleans cgroup + bind mounts.
pub fn stop_runtime_process(proc: &RunningProcess, force: bool) -> anyhow::Result<()> {
    volume::unmount_all(&proc.bind_mount_points);
    if let Some(ref p) = proc.cgroup_path {
        container::remove_cgroup_dir(p);
    }

    let p = Pid::from_raw(proc.pid);
    if force {
        let _ = kill(p, Signal::SIGKILL);
        return Ok(());
    }
    let _ = kill(p, Signal::SIGTERM);
    let deadline = Instant::now() + Duration::from_secs(15);
    while Instant::now() < deadline {
        if !proc_alive(proc.pid) {
            return Ok(());
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    warn!(pid = proc.pid, "SIGTERM timeout; sending SIGKILL");
    let _ = kill(p, Signal::SIGKILL);
    Ok(())
}

fn proc_alive(pid: i32) -> bool {
    Path::new(&format!("/proc/{pid}")).exists()
}
