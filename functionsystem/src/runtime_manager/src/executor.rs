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

const CPP_PROGRAM_NAME: &str = "cppruntime";
const GO_PROGRAM_NAME: &str = "goruntime";

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

fn runtime_grpc_address(cfg: &Config, port: u16) -> String {
    let host = if cfg.proxy_ip.trim().is_empty() {
        cfg.host.trim()
    } else {
        cfg.proxy_ip.trim()
    };
    format!("{host}:{port}")
}

fn runtime_connect_address(cfg: &Config, req: &StartInstanceRequest, port: u16) -> String {
    req.env_vars
        .get("POSIX_LISTEN_ADDR")
        .map(|v| v.trim())
        .filter(|v| !v.is_empty() && *v != "0")
        .map(ToOwned::to_owned)
        .unwrap_or_else(|| runtime_grpc_address(cfg, port))
}

fn job_id_env_for(req: &StartInstanceRequest) -> String {
    req.env_vars
        .get("YR_JOB_ID")
        .filter(|v| !v.trim().is_empty())
        .cloned()
        .unwrap_or_else(|| format!("job-{}", &req.instance_id.get(..8).unwrap_or("ffffffff")))
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
    // C++ enforces runtime memory through metrics/OOM callbacks, not RLIMIT_AS.
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

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RuntimeCredential {
    pub uid: u32,
    pub gid: u32,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RuntimeLaunchSpec {
    pub executable: String,
    pub arg0: Option<String>,
    pub args: Vec<String>,
    pub current_dir: PathBuf,
    pub env: HashMap<String, String>,
    pub credential: Option<RuntimeCredential>,
}

fn base_ld_library_path(cfg: &Config, req: &StartInstanceRequest) -> String {
    let mut parts = Vec::new();
    let code_path = req.code_path.trim();
    if !code_path.is_empty() && code_path != "/" {
        parts.push(code_path.to_string());
        parts.push(format!("{code_path}/lib"));
    }
    if req.runtime_type.to_ascii_lowercase().contains("cpp") {
        parts.push(format!("{}/cpp/lib", cfg.runtime_dir.trim_end_matches('/')));
    }
    if !cfg.runtime_ld_library_path.trim().is_empty() {
        parts.push(cfg.runtime_ld_library_path.clone());
    }
    parts.join(":")
}

fn build_runtime_env(
    cfg: &Config,
    req: &StartInstanceRequest,
    runtime_id: &str,
    port: u16,
    workdir: &Path,
) -> HashMap<String, String> {
    let listen_addr = runtime_connect_address(cfg, req, port);
    let host_ip = cfg.host_ip.trim();
    let host_ip = if host_ip.is_empty() {
        cfg.host.trim()
    } else {
        host_ip
    };
    let mut env = HashMap::from([
        ("INSTANCE_ID".to_string(), req.instance_id.clone()),
        ("RUNTIME_ID".to_string(), runtime_id.to_string()),
        ("YR_RUNTIME_ID".to_string(), runtime_id.to_string()),
        ("FUNCTION_NAME".to_string(), req.function_name.clone()),
        ("TENANT_ID".to_string(), req.tenant_id.clone()),
        ("RUNTIME_PORT".to_string(), port.to_string()),
        ("RUNTIME_TYPE".to_string(), req.runtime_type.clone()),
        ("POSIX_LISTEN_ADDR".to_string(), listen_addr),
        ("POD_IP".to_string(), cfg.host.trim().to_string()),
        ("SNUSER_LIB_PATH".to_string(), cfg.snuser_lib_dir.clone()),
        (
            "DATASYSTEM_ADDR".to_string(),
            format!("{host_ip}:{}", cfg.data_system_port),
        ),
        (
            "YR_DS_ADDRESS".to_string(),
            format!("{host_ip}:{}", cfg.data_system_port),
        ),
        (
            "DRIVER_SERVER_PORT".to_string(),
            cfg.driver_server_port.clone(),
        ),
        ("HOME".to_string(), cfg.runtime_home_dir.clone()),
        ("HOST_IP".to_string(), host_ip.to_string()),
        ("FUNCTION_LIB_PATH".to_string(), req.code_path.clone()),
        ("YR_FUNCTION_LIB_PATH".to_string(), req.code_path.clone()),
        ("LAYER_LIB_PATH".to_string(), String::new()),
        (
            "LD_LIBRARY_PATH".to_string(),
            base_ld_library_path(cfg, req),
        ),
        (
            "PROXY_GRPC_SERVER_PORT".to_string(),
            cfg.proxy_grpc_server_port.clone(),
        ),
        (
            "YR_SERVER_ADDRESS".to_string(),
            format!(
                "{}:{}",
                cfg.proxy_ip.trim(),
                cfg.proxy_grpc_server_port.trim()
            ),
        ),
        ("NODE_ID".to_string(), cfg.node_id.clone()),
        ("YR_JOB_ID".to_string(), job_id_env_for(req)),
        ("PYTHONUNBUFFERED".to_string(), "1".to_string()),
        ("YR_BARE_MENTAL".to_string(), "1".to_string()),
    ]);

    let mut expand_vars = env.clone();
    for key in ["LD_LIBRARY_PATH", "PYTHONPATH", "PATH"] {
        if let Ok(value) = std::env::var(key) {
            expand_vars.entry(key.to_string()).or_insert(value);
        }
    }

    for (k, v) in &req.env_vars {
        if k == "UNZIPPED_WORKING_DIR" {
            continue;
        }
        let mut vars = expand_vars.clone();
        for (other_k, other_v) in &req.env_vars {
            if other_k != k {
                vars.insert(other_k.clone(), other_v.clone());
            }
        }
        let expanded = expand_env_value(v, &vars);
        if k == "LD_LIBRARY_PATH" {
            if v.contains("${LD_LIBRARY_PATH}") {
                env.insert(k.clone(), expanded);
            } else {
                let base = env.get(k).cloned().unwrap_or_default();
                env.insert(
                    k.clone(),
                    if base.is_empty() {
                        expanded
                    } else {
                        format!("{base}:{expanded}")
                    },
                );
            }
        } else {
            env.insert(k.clone(), expanded);
        }
    }

    env.insert("YR_LOG_LEVEL".to_string(), cfg.runtime_log_level.clone());
    env.insert("GLOG_log_dir".to_string(), cfg.runtime_logs_dir.clone());
    env.insert(
        "YR_MAX_LOG_SIZE_MB".to_string(),
        cfg.runtime_max_log_size.to_string(),
    );
    env.insert(
        "YR_MAX_LOG_FILE_NUM".to_string(),
        cfg.runtime_max_log_file_num.to_string(),
    );
    env.insert(
        "DS_CONNECT_TIMEOUT_SEC".to_string(),
        cfg.runtime_ds_connect_timeout.to_string(),
    );

    let mut python_path = String::new();
    if let Some(working_dir) = req.env_vars.get("UNZIPPED_WORKING_DIR") {
        if !working_dir.trim().is_empty() {
            python_path.push(':');
            python_path.push_str(working_dir);
        }
    }
    if let Some(existing) = env.get("PYTHONPATH").filter(|v| !v.is_empty()) {
        python_path.push(':');
        python_path.push_str(existing);
    } else {
        let fallback = python_path_for(cfg, req);
        if !fallback.is_empty() {
            python_path.push(':');
            python_path.push_str(&fallback);
        }
    }
    env.insert("PYTHONPATH".to_string(), python_path);

    if let Ok(path) = std::env::var("PATH") {
        let existing = env.get("PATH").cloned().unwrap_or_default();
        if cfg.enable_inherit_env {
            env.insert(
                "PATH".to_string(),
                if existing.is_empty() {
                    path
                } else {
                    format!("{existing}:{path}")
                },
            );
        } else {
            env.entry("PATH".to_string()).or_insert(path);
        }
    }
    for (k, v) in std::env::vars() {
        if k.starts_with("YR_") {
            env.entry(k).or_insert(v);
        } else if cfg.enable_inherit_env && k != "PATH" {
            env.entry(k).or_insert(v);
        }
    }
    if env.contains_key("YR_NOSET_ASCEND_RT_VISIBLE_DEVICES") {
        env.remove("ASCEND_RT_VISIBLE_DEVICES");
    }

    for (k, v) in venv::java_env_hints(workdir, &req.runtime_type) {
        env.insert(k, expand_env_value(&v, &expand_vars));
    }

    env
}

pub fn build_runtime_launch_spec(
    cfg: &Config,
    req: &StartInstanceRequest,
    paths: &[String],
    runtime_id: &str,
    port: u16,
) -> anyhow::Result<RuntimeLaunchSpec> {
    let executable = pick_runtime_executable(paths, &req.runtime_type)
        .ok_or_else(|| anyhow!("no runtime executable configured"))?;
    let current_dir = resolve_workdir(req);
    let listen_addr = runtime_connect_address(cfg, req, port);

    let mut arg0 = None;
    let args = if is_python_runtime(&req.runtime_type) {
        let server = python_server_path(paths)
            .ok_or_else(|| anyhow!("python runtime server path not found"))?;
        vec![
            "-u".to_string(),
            server.to_string_lossy().into_owned(),
            "--rt_server_address".to_string(),
            listen_addr,
            "--deploy_dir".to_string(),
            req.code_path.clone(),
            "--runtime_id".to_string(),
            runtime_id.to_string(),
            "--job_id".to_string(),
            job_id_for(req),
            "--log_level".to_string(),
            cfg.runtime_log_level.clone(),
        ]
    } else if req.runtime_type.to_ascii_lowercase().contains("cpp")
        || Path::new(&executable).file_name().and_then(|n| n.to_str()) == Some("runtime")
    {
        arg0 = Some(CPP_PROGRAM_NAME.to_string());
        vec![
            format!("-runtimeId={runtime_id}"),
            format!("-logLevel={}", cfg.runtime_log_level),
            format!("-jobId={}", job_id_for(req)),
            format!("-grpcAddress={listen_addr}"),
            format!(
                "-runtimeConfigPath={}/runtime.json",
                cfg.runtime_config_dir.trim_end_matches('/')
            ),
        ]
    } else if req.runtime_type.to_ascii_lowercase().contains("go")
        || Path::new(&executable).file_name().and_then(|n| n.to_str()) == Some("goruntime")
    {
        arg0 = Some(GO_PROGRAM_NAME.to_string());
        vec![
            format!("-runtimeId={runtime_id}"),
            format!("-instanceId={}", req.instance_id),
            format!("-logLevel={}", cfg.runtime_log_level),
            format!("-grpcAddress={listen_addr}"),
        ]
    } else {
        vec![req.instance_id.clone()]
    };

    let env = build_runtime_env(cfg, req, runtime_id, port, &current_dir);
    let credential = cfg.set_cmd_cred.then_some(RuntimeCredential {
        uid: cfg.runtime_uid.max(0) as u32,
        gid: cfg.runtime_gid.max(0) as u32,
    });
    Ok(RuntimeLaunchSpec {
        executable,
        arg0,
        args,
        current_dir,
        env,
        credential,
    })
}

/// Spawn a runtime child with env, logs, cgroups, optional bind mounts, venv hints.
pub fn start_runtime_process(
    cfg: &Config,
    req: &StartInstanceRequest,
    paths: &[String],
    runtime_id: &str,
    port: u16,
) -> anyhow::Result<RunningProcess> {
    cfg.ensure_log_dir()?;

    let workdir = resolve_workdir(req);
    let _ = venv::prepare_python_env(&workdir, &req.runtime_type);
    let spec = build_runtime_launch_spec(cfg, req, paths, runtime_id, port)?;

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

    let mut cmd = Command::new(&spec.executable);
    if let Some(arg0) = &spec.arg0 {
        cmd.arg0(arg0);
    }
    cmd.args(&spec.args)
        .current_dir(&spec.current_dir)
        .stdin(Stdio::null())
        .stdout(Stdio::from(stdout))
        .stderr(Stdio::from(stderr))
        .envs(&spec.env);

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
    let credential = spec.credential.clone();
    unsafe {
        cmd.pre_exec(move || {
            let _ = container::maybe_unshare_namespaces(isolate);
            // New process group so SIGTERM/SIGKILL can target the whole runtime tree (negative pid).
            let _ = setpgid(Pid::from_raw(0), Pid::from_raw(0));
            if let Some(cred) = &credential {
                if libc::setuid(cred.uid) != 0 {
                    return Err(std::io::Error::last_os_error());
                }
                if libc::setgid(cred.gid) != 0 {
                    return Err(std::io::Error::last_os_error());
                }
            }
            apply_rlimits_libc(&res);
            Ok(())
        });
    }

    let child = cmd
        .spawn()
        .with_context(|| format!("spawn runtime {:?}", spec.executable))?;
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
        exe = %spec.executable,
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
