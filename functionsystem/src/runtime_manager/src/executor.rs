use crate::config::Config;
use crate::container::{self, CgroupIsolate};
use crate::instance_health;
use crate::log_manager::InstanceLogPaths;
use crate::state::InstanceHealthSpec;
use crate::state::RunningProcess;
use crate::venv;
use crate::volume::{self, BindMount};
use anyhow::{anyhow, Context};
use nix::unistd::{setpgid, Pid};
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
    if is_python_runtime(runtime_type) {
        return python_interpreter(runtime_type).or_else(|| Some("python3".to_string()));
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

fn is_python_runtime(runtime_type: &str) -> bool {
    runtime_type.to_ascii_lowercase().contains("python")
}

fn python_interpreter(runtime_type: &str) -> Option<String> {
    let lower = runtime_type.to_ascii_lowercase();
    if lower.contains("3.11") {
        Some("python3.11".to_string())
    } else if lower.contains("3.10") {
        Some("python3.10".to_string())
    } else if lower.contains("3.9") {
        Some("python3.9".to_string())
    } else if lower.contains("3.8") {
        Some("python3.8".to_string())
    } else if lower.contains("3.7") {
        Some("python3.7".to_string())
    } else if lower.contains("3.6") {
        Some("python3.6".to_string())
    } else if lower.contains("python3") {
        Some("python3".to_string())
    } else {
        None
    }
}

fn python_server_path(paths: &[String]) -> Option<PathBuf> {
    paths
        .iter()
        .map(PathBuf::from)
        .find(|p| {
            p.file_name()
                .and_then(|n| n.to_str())
                .is_some_and(|n| n == "yr_runtime_main.py")
        })
        .or_else(|| {
            paths.iter().find_map(|p| {
                let path = Path::new(p);
                let service = path
                    .ancestors()
                    .find(|a| a.file_name().and_then(|n| n.to_str()) == Some("service"))?;
                Some(service.join("python/yr/main/yr_runtime_main.py"))
            })
        })
}

fn job_id_for(req: &StartInstanceRequest) -> String {
    req.env_vars
        .get("YR_JOB_ID")
        .filter(|v| v.starts_with("job-"))
        .cloned()
        .unwrap_or_else(|| format!("job-{}", &req.instance_id.get(..8).unwrap_or("ffffffff")))
}

fn python_path_for(cfg: &Config, req: &StartInstanceRequest) -> String {
    let mut parts = Vec::new();
    if !req.code_path.trim().is_empty() {
        parts.push(req.code_path.clone());
    }
    if !cfg.python_dependency_path.trim().is_empty() && cfg.python_dependency_path.trim() != "/" {
        parts.push(cfg.python_dependency_path.clone());
    }
    if let Ok(existing) = std::env::var("PYTHONPATH") {
        parts.push(existing);
    }
    parts.join(":")
}

fn expand_env_value(value: &str, vars: &HashMap<String, String>) -> String {
    let mut out = value.to_string();
    for (k, v) in vars {
        let pat = format!("${{{}}}", k);
        if out.contains(&pat) {
            out = out.replace(&pat, v);
        }
    }
    out
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
    let (stdout, stderr) =
        logs.open_append_rotated(cfg.log_rotate_max_bytes, cfg.log_rotate_keep)?;

    let health_spec: InstanceHealthSpec = instance_health::parse_from_config_json(
        &req.config_json,
        Duration::from_secs(cfg.manager_startup_probe_secs.max(1)),
    );

    let listen_addr = format!("{}:{}", cfg.host.trim(), port);

    let mut cmd = Command::new(&exe);
    if is_python_runtime(&req.runtime_type) {
        let server = python_server_path(paths)
            .ok_or_else(|| anyhow!("python runtime server path not found"))?;
        cmd.arg("-u")
            .arg(server)
            .arg("--rt_server_address")
            .arg(&listen_addr)
            .arg("--deploy_dir")
            .arg(&req.code_path)
            .arg("--runtime_id")
            .arg(runtime_id)
            .arg("--job_id")
            .arg(job_id_for(req))
            .arg("--log_level")
            .arg(&cfg.runtime_log_level);
        let python_path = python_path_for(cfg, req);
        if !python_path.is_empty() {
            cmd.env("PYTHONPATH", python_path);
        }
    } else if req.runtime_type.contains("cpp")
        || Path::new(&exe).file_name().and_then(|n| n.to_str()) == Some("runtime")
    {
        // C++ ST and legacy tooling locate worker processes by `cppruntime`.
        // The packaged executable is `cpp/bin/runtime`, but C++ function-agent
        // starts it with the legacy process name. Keep that black-box contract.
        cmd.arg0("cppruntime");
        cmd.arg(&req.instance_id);
    } else {
        cmd.arg(&req.instance_id);
    }
    cmd.current_dir(&workdir)
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
        .env(
            "YR_JOB_ID",
            format!("job-{}", &req.instance_id.get(..8).unwrap_or("ffffffff")),
        )
        .env("PYTHONUNBUFFERED", "1")
        .env("YR_BARE_MENTAL", "1");

    let mut expand_vars = HashMap::new();
    if let Ok(ld) = std::env::var("LD_LIBRARY_PATH") {
        expand_vars.insert("LD_LIBRARY_PATH".to_string(), ld.clone());
        cmd.env("LD_LIBRARY_PATH", ld);
    }
    if let Ok(pp) = std::env::var("PYTHONPATH") {
        expand_vars.insert("PYTHONPATH".to_string(), pp.clone());
        cmd.env("PYTHONPATH", pp);
    }
    if let Ok(path) = std::env::var("PATH") {
        expand_vars.insert("PATH".to_string(), path.clone());
        cmd.env("PATH", path);
    }
    for (k, v) in &req.env_vars {
        let mut vars = expand_vars.clone();
        for (other_k, other_v) in &req.env_vars {
            if other_k != k {
                vars.insert(other_k.clone(), other_v.clone());
            }
        }
        let expanded = expand_env_value(v, &vars);
        if k == "LD_LIBRARY_PATH" && (expanded.contains("depend") || v.contains("${")) {
            info!(
                instance_id = %req.instance_id,
                original = %v,
                expanded = %expanded,
                "runtime_manager applying LD_LIBRARY_PATH"
            );
        }
        cmd.env(k, expanded);
    }
    for (k, v) in java_env {
        cmd.env(k, expand_env_value(&v, &expand_vars));
    }
    if let Some(instance_work_dir) = req.env_vars.get("INSTANCE_WORK_DIR") {
        if !instance_work_dir.trim().is_empty() {
            let p = Path::new(instance_work_dir);
            std::fs::create_dir_all(p)
                .with_context(|| format!("create INSTANCE_WORK_DIR {}", p.display()))?;
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                let _ = std::fs::set_permissions(p, std::fs::Permissions::from_mode(0o770));
            }
        }
    }

    let res = req.resources.clone();
    let isolate = cfg.isolate_namespaces;
    unsafe {
        cmd.pre_exec(move || {
            let _ = container::maybe_unshare_namespaces(isolate);
            // New process group so SIGTERM/SIGKILL can target the whole runtime tree (negative pid).
            let _ = setpgid(Pid::from_raw(0), Pid::from_raw(0));
            apply_rlimits_libc(&res);
            Ok(())
        });
    }

    let child = cmd
        .spawn()
        .with_context(|| format!("spawn runtime {exe:?}"))?;
    let pid = child.id() as i32;

    if cfg.runtime_child_oom_score_adj != 0 {
        container::adjust_oom_score_adj(pid, cfg.runtime_child_oom_score_adj);
    }

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

fn kill_process_group(pid: i32, sig: libc::c_int) {
    if pid <= 0 {
        return;
    }
    unsafe {
        let _ = libc::kill(-pid, sig);
    }
}

fn term_timeout(cfg: &Config) -> Duration {
    let s = cfg.kill_process_timeout_seconds;
    if s > 0 {
        Duration::from_secs(u64::from(s))
    } else {
        Duration::from_secs(15)
    }
}

/// SIGTERM to the runtime process group, poll `/proc`, then SIGKILL. Cleans cgroup + bind mounts.
pub fn stop_runtime_process(
    cfg: &Config,
    proc: &RunningProcess,
    force: bool,
) -> anyhow::Result<()> {
    volume::unmount_all(&proc.bind_mount_points);
    if let Some(ref p) = proc.cgroup_path {
        container::remove_cgroup_dir(p);
    }

    let pid = proc.pid;
    if force {
        kill_process_group(pid, libc::SIGKILL);
        return Ok(());
    }
    kill_process_group(pid, libc::SIGTERM);
    let deadline = Instant::now() + term_timeout(cfg);
    while Instant::now() < deadline {
        if !proc_alive(pid) {
            return Ok(());
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    warn!(pid, "SIGTERM timeout; sending SIGKILL to process group");
    kill_process_group(pid, libc::SIGKILL);
    Ok(())
}

fn proc_alive(pid: i32) -> bool {
    Path::new(&format!("/proc/{pid}")).exists()
}
