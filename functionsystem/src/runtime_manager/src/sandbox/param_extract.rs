//! Extract sandbox start inputs from `RuntimeInstanceInfo` — the Rust analogue of
//! the C++ SandboxExecutor's gathering of `SandboxStartParams` (CommandBuilder +
//! deployOptions parsing). Pure, unit-testable.

use std::collections::HashMap;

use serde::Deserialize;
use yr_proto::messages::RuntimeInstanceInfo;

use super::executor_select::{select_start_path, StartPath};
use super::request_builder::{
    parse_forward_ports, PortForward, RootfsSpec, SandboxStartParams,
};

/// Container config carried in the internal `StartInstanceRequest.config_json`
/// (the proxy serializes this when the instance is CONTAINER-mode). This is the
/// runtime_manager-boundary contract — see docs/analysis/173.
#[derive(Debug, Clone, Deserialize, Default)]
pub struct SandboxConfig {
    /// Marker: presence of this object in config_json => CONTAINER backend.
    #[serde(default)]
    pub sandbox: bool,
    /// Custom rootfs/container image (image url, local path, or s3 — see rootfs_type).
    #[serde(default)]
    pub image: String,
    /// "image" (default) | "local" | "s3".
    #[serde(default)]
    pub rootfs_type: String,
    /// Port forwards as "PORT" or "PROTOCOL:PORT" (e.g. "8080", "tcp:9090").
    #[serde(default)]
    pub ports: Vec<String>,
    #[serde(default)]
    pub command: Vec<String>,
    #[serde(default)]
    pub cwd: String,
    #[serde(default)]
    pub runtime_envs: HashMap<String, String>,
    #[serde(default)]
    pub user_envs: HashMap<String, String>,
    /// network mode ("sandbox"|"host"|"none"); empty => sandbox.
    #[serde(default)]
    pub network: String,
    #[serde(default)]
    pub warmup: bool,
    #[serde(default)]
    pub checkpoint_id: String,
    #[serde(default)]
    pub storage: String,
}

/// Parse the container config from `config_json`. Returns None when the string is
/// empty, not JSON, or lacks the `"sandbox": true` marker (i.e. a process-mode req).
pub fn parse_sandbox_config(config_json: &str) -> Option<SandboxConfig> {
    if config_json.trim().is_empty() {
        return None;
    }
    let cfg: SandboxConfig = serde_json::from_str(config_json).ok()?;
    cfg.sandbox.then_some(cfg)
}

fn parse_port_spec(spec: &str) -> Option<PortForward> {
    let s = spec.trim();
    let (proto, port) = match s.split_once(':') {
        Some((p, n)) => (p.trim().to_ascii_lowercase(), n.trim()),
        None => ("tcp".to_string(), s),
    };
    let port: u32 = port.parse().ok()?;
    (port >= 1 && port <= 65535).then_some(PortForward {
        container_port: port,
        protocol: if proto.is_empty() { "tcp".into() } else { proto },
    })
}

/// Build the start inputs from a CONTAINER `SandboxConfig` + the internal request's
/// instance_id / resources / trace_id.
pub fn extract_from_config(
    cfg: &SandboxConfig,
    runtime_id: &str,
    trace_id: &str,
    resources: HashMap<String, f64>,
) -> ExtractedStart {
    let path = if cfg.warmup {
        StartPath::WarmUp
    } else if !cfg.checkpoint_id.trim().is_empty() {
        StartPath::Restore
    } else {
        StartPath::Normal
    };
    let rootfs = if cfg.image.trim().is_empty() {
        None
    } else {
        Some(match cfg.rootfs_type.as_str() {
            "local" => RootfsSpec::Local(cfg.image.clone()),
            _ => RootfsSpec::Image(cfg.image.clone()),
        })
    };
    let forwards = cfg.ports.iter().filter_map(|p| parse_port_spec(p)).collect();
    let params = SandboxStartParams {
        runtime_id: runtime_id.to_string(),
        command: cfg.command.clone(),
        cwd: cfg.cwd.clone(),
        runtime_envs: cfg.runtime_envs.clone(),
        user_envs: cfg.user_envs.clone(),
        resources,
        rootfs,
        rootfs_readonly: false,
        rootfs_config: None,
        mounts: Vec::new(),
        network: cfg.network.clone(),
        port_mappings: Vec::new(),
        ckpt_dir: String::new(),
        trace_id: trace_id.to_string(),
        stdout: String::new(),
        stderr: String::new(),
        extra_config: String::new(),
    };
    ExtractedStart {
        params,
        forwards,
        path,
        checkpoint_id: cfg.checkpoint_id.clone(),
        storage_url: cfg.storage.clone(),
    }
}

/// Everything needed to dispatch one CONTAINER StartInstance.
pub struct ExtractedStart {
    pub params: SandboxStartParams,
    pub forwards: Vec<PortForward>,
    pub path: StartPath,
    pub checkpoint_id: String,
    pub storage_url: String,
}

fn split_ws(s: &str) -> Vec<String> {
    s.split_whitespace().map(|x| x.to_string()).collect()
}

/// Build the start inputs from the instance info. Best-effort field mapping;
/// missing sub-messages yield empty defaults.
pub fn extract_start(info: &RuntimeInstanceInfo) -> ExtractedStart {
    let path = select_start_path(info);

    // Command from BootstrapConfig (entrypoint + cmd, space-split — launcher contract).
    let mut command = Vec::new();
    let mut cwd = String::new();
    if let Some(b) = &info.bootstrap_config {
        command.extend(split_ws(&b.entrypoint));
        command.extend(split_ws(&b.cmd));
        cwd = b.root.clone();
    }

    // Envs + resources from RuntimeConfig.
    let mut runtime_envs = HashMap::new();
    let mut user_envs = HashMap::new();
    let mut resources = HashMap::new();
    if let Some(rc) = &info.runtime_config {
        runtime_envs = rc.posix_envs.clone();
        user_envs = rc.user_envs.clone();
        if let Some(res) = &rc.resources {
            for (k, r) in &res.resources {
                if let Some(s) = &r.scalar {
                    resources.insert(k.clone(), s.value);
                }
            }
        }
    }

    // Rootfs from ContainerRuntimeConfig (already a runtime.v1.RootfsConfig).
    let rootfs_config = info.container.as_ref().and_then(|c| c.rootfs_config.clone());

    // Port forwards from deployOptions["network"] (JSON). network mode is left empty
    // so build_start_request defaults it to "sandbox".
    let forwards = info
        .deployment_config
        .as_ref()
        .and_then(|d| d.deploy_options.get("network"))
        .map(|j| parse_forward_ports(j))
        .unwrap_or_default();

    let (checkpoint_id, storage_url) = info
        .snapshot_info
        .as_ref()
        .map(|s| (s.checkpoint_id.clone(), s.storage.clone()))
        .unwrap_or_default();

    let params = SandboxStartParams {
        runtime_id: info.runtime_id.clone(),
        command,
        cwd,
        runtime_envs,
        user_envs,
        resources,
        rootfs: None,
        rootfs_readonly: false,
        rootfs_config,
        mounts: Vec::new(),
        network: String::new(),
        port_mappings: Vec::new(),
        ckpt_dir: String::new(),
        trace_id: info.trace_id.clone(),
        stdout: String::new(),
        stderr: String::new(),
        extra_config: String::new(),
    };

    ExtractedStart {
        params,
        forwards,
        path,
        checkpoint_id,
        storage_url,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use yr_proto::messages::{
        BootstrapConfig, ContainerRuntimeConfig, DeploymentConfig, RuntimeConfig,
        RuntimeInstanceInfo,
    };
    use yr_proto::resources::{value::Scalar, Resource, Resources, SnapshotInfo};
    use yr_proto::runtime::v1::{rootfs_config, RootfsConfig, RootfsSrcType};

    #[test]
    fn extracts_command_cwd_envs_resources_rootfs_and_forwards() {
        let mut deploy_options = HashMap::new();
        deploy_options.insert(
            "network".to_string(),
            r#"{"portForwardings":[{"port":8080,"protocol":"tcp"}]}"#.to_string(),
        );
        let mut posix = HashMap::new();
        posix.insert("PX".into(), "1".into());
        let mut resmap = std::collections::HashMap::new();
        resmap.insert(
            "CPU".to_string(),
            Resource {
                name: "CPU".into(),
                scalar: Some(Scalar { value: 2000.0, limit: 0.0 }),
                ..Default::default()
            },
        );
        let info = RuntimeInstanceInfo {
            runtime_id: "r1".into(),
            trace_id: "t1".into(),
            bootstrap_config: Some(BootstrapConfig {
                entrypoint: "/runtime cpp".into(),
                cmd: "--flag x".into(),
                root: "/work".into(),
                ..Default::default()
            }),
            runtime_config: Some(RuntimeConfig {
                posix_envs: posix,
                resources: Some(Resources { resources: resmap }),
                ..Default::default()
            }),
            container: Some(ContainerRuntimeConfig {
                rootfs_config: Some(RootfsConfig {
                    readonly: false,
                    r#type: RootfsSrcType::Image as i32,
                    source: Some(rootfs_config::Source::ImageUrl("img:v1".into())),
                }),
                ..Default::default()
            }),
            deployment_config: Some(DeploymentConfig {
                deploy_options,
                ..Default::default()
            }),
            ..Default::default()
        };

        let e = extract_start(&info);
        assert_eq!(e.path, StartPath::Normal);
        assert_eq!(e.params.runtime_id, "r1");
        assert_eq!(e.params.command, vec!["/runtime", "cpp", "--flag", "x"]);
        assert_eq!(e.params.cwd, "/work");
        assert_eq!(e.params.runtime_envs.get("PX"), Some(&"1".to_string()));
        assert_eq!(e.params.resources.get("CPU"), Some(&2000.0));
        assert!(e.params.rootfs_config.is_some());
        assert_eq!(e.forwards, vec![PortForward { container_port: 8080, protocol: "tcp".into() }]);
        assert_eq!(e.params.trace_id, "t1");
    }

    #[test]
    fn extracts_restore_checkpoint() {
        let info = RuntimeInstanceInfo {
            runtime_id: "r2".into(),
            snapshot_info: Some(SnapshotInfo {
                checkpoint_id: "ckpt-9".into(),
                storage: "s3://b/o".into(),
                ..Default::default()
            }),
            ..Default::default()
        };
        let e = extract_start(&info);
        assert_eq!(e.path, StartPath::Restore);
        assert_eq!(e.checkpoint_id, "ckpt-9");
        assert_eq!(e.storage_url, "s3://b/o");
    }

    #[test]
    fn parse_sandbox_config_requires_marker() {
        assert!(parse_sandbox_config("").is_none());
        assert!(parse_sandbox_config("not json").is_none());
        // valid json but no sandbox marker => process-mode (None)
        assert!(parse_sandbox_config(r#"{"image":"x"}"#).is_none());
        assert!(parse_sandbox_config(r#"{"sandbox":false,"image":"x"}"#).is_none());
        // marker present => Some
        assert!(parse_sandbox_config(r#"{"sandbox":true}"#).is_some());
    }

    #[test]
    fn extract_from_config_maps_image_ports_path() {
        let cfg = parse_sandbox_config(
            r#"{"sandbox":true,"image":"aio-yr-runtime:latest","ports":["8080","tcp:9090"],
                "command":["/runtime"],"cwd":"/w","network":"host"}"#,
        )
        .unwrap();
        let mut res = HashMap::new();
        res.insert("cpu".to_string(), 2000.0);
        let e = extract_from_config(&cfg, "r1", "t1", res);
        assert_eq!(e.path, StartPath::Normal);
        assert_eq!(e.params.runtime_id, "r1");
        assert_eq!(e.params.command, vec!["/runtime"]);
        assert_eq!(e.params.network, "host");
        assert!(matches!(&e.params.rootfs, Some(RootfsSpec::Image(u)) if u == "aio-yr-runtime:latest"));
        assert_eq!(
            e.forwards,
            vec![
                PortForward { container_port: 8080, protocol: "tcp".into() },
                PortForward { container_port: 9090, protocol: "tcp".into() },
            ]
        );
    }

    #[test]
    fn extract_from_config_warmup_and_restore_paths() {
        let warm = parse_sandbox_config(r#"{"sandbox":true,"warmup":true}"#).unwrap();
        assert_eq!(extract_from_config(&warm, "r", "t", HashMap::new()).path, StartPath::WarmUp);
        let restore = parse_sandbox_config(r#"{"sandbox":true,"checkpoint_id":"c1","storage":"s3://b/o"}"#).unwrap();
        let e = extract_from_config(&restore, "r", "t", HashMap::new());
        assert_eq!(e.path, StartPath::Restore);
        assert_eq!(e.checkpoint_id, "c1");
        assert_eq!(e.storage_url, "s3://b/o");
    }

    #[test]
    fn empty_info_is_normal_with_defaults() {
        let e = extract_start(&RuntimeInstanceInfo::default());
        assert_eq!(e.path, StartPath::Normal);
        assert!(e.params.command.is_empty());
        assert!(e.forwards.is_empty());
        assert!(e.params.rootfs_config.is_none());
    }
}
