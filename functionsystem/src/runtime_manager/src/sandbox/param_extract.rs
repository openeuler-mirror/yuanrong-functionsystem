//! Extract sandbox start inputs from `RuntimeInstanceInfo` — the Rust analogue of
//! the C++ SandboxExecutor's gathering of `SandboxStartParams` (CommandBuilder +
//! deployOptions parsing). Pure, unit-testable.

use std::collections::HashMap;

use yr_proto::messages::RuntimeInstanceInfo;

use super::executor_select::{select_start_path, StartPath};
use super::request_builder::{parse_forward_ports, PortForward, SandboxStartParams};

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
    fn empty_info_is_normal_with_defaults() {
        let e = extract_start(&RuntimeInstanceInfo::default());
        assert_eq!(e.path, StartPath::Normal);
        assert!(e.params.command.is_empty());
        assert!(e.forwards.is_empty());
        assert!(e.params.rootfs_config.is_none());
    }
}
