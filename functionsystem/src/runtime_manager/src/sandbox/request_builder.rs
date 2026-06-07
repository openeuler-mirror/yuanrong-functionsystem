//! Build a `runtime.v1.StartRequest` from sandbox start params — port of C++
//! `SandboxRequestBuilder` + `SandboxExecutor::ParseForwardPorts`.
//!
//! Pure functions (no I/O) so they are fully unit-testable without containerd.

use std::collections::HashMap;

use serde_json::Value;
use yr_proto::runtime::v1::{
    rootfs_config, FunctionRuntime, Mount, RootfsConfig, RootfsSrcType, S3Config, StartRequest,
};

/// Resolve the effective rootfs for a build: an explicit proto `RootfsConfig`
/// (extracted from `RuntimeInstanceInfo.container.rootfsConfig`) takes precedence
/// over the higher-level [`RootfsSpec`].
fn resolve_rootfs(params: &SandboxStartParams) -> Option<RootfsConfig> {
    if let Some(rc) = &params.rootfs_config {
        return Some(rc.clone());
    }
    params.rootfs.as_ref().map(|r| r.to_config(params.rootfs_readonly))
}

/// One requested port-forward (C++ `PortForwardConfig`).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PortForward {
    pub container_port: u32,
    pub protocol: String,
}

/// Parse `deployOptions["network"]` JSON `{"portForwardings":[{"port":8080,"protocol":"tcp"}]}`.
/// Mirrors C++ `ParseForwardPorts`: ports must be 1..=65535; protocol defaults to "tcp",
/// lowercased; malformed entries are skipped; bad JSON yields an empty list.
pub fn parse_forward_ports(network_json: &str) -> Vec<PortForward> {
    if network_json.trim().is_empty() {
        return Vec::new();
    }
    let Ok(j) = serde_json::from_str::<Value>(network_json) else {
        return Vec::new();
    };
    let Some(arr) = j.get("portForwardings").and_then(|v| v.as_array()) else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for item in arr {
        let Some(p) = item.get("port").and_then(|v| v.as_i64()) else {
            continue;
        };
        if p <= 0 || p > 65535 {
            continue;
        }
        let protocol = item
            .get("protocol")
            .and_then(|v| v.as_str())
            .map(|s| s.to_ascii_lowercase())
            .unwrap_or_else(|| "tcp".to_string());
        out.push(PortForward {
            container_port: p as u32,
            protocol,
        });
    }
    out
}

/// Encode a single allocated mapping as `protocol:hostPort:containerPort`
/// (C++ `SandboxExecutor` mapping format, consumed by sandbox-shim + Traefik).
pub fn encode_port_mapping(protocol: &str, host_port: u16, container_port: u32) -> String {
    format!("{protocol}:{host_port}:{container_port}")
}

/// Rootfs source from `deployOptions["rootfs"]` (C++ supports s3 / image / local).
#[derive(Debug, Clone, PartialEq)]
pub enum RootfsSpec {
    Image(String),
    Local(String),
    S3(S3Config),
}

impl RootfsSpec {
    pub fn to_config(&self, readonly: bool) -> RootfsConfig {
        match self {
            RootfsSpec::Image(url) => RootfsConfig {
                readonly,
                r#type: RootfsSrcType::Image as i32,
                source: Some(rootfs_config::Source::ImageUrl(url.clone())),
            },
            RootfsSpec::Local(path) => RootfsConfig {
                readonly,
                r#type: RootfsSrcType::Local as i32,
                source: Some(rootfs_config::Source::Path(path.clone())),
            },
            RootfsSpec::S3(cfg) => RootfsConfig {
                readonly,
                r#type: RootfsSrcType::S3 as i32,
                source: Some(rootfs_config::Source::S3Config(cfg.clone())),
            },
        }
    }
}

/// All inputs needed to build a `StartRequest` (C++ `SandboxStartParams`).
#[derive(Debug, Clone, Default)]
pub struct SandboxStartParams {
    pub runtime_id: String,
    pub command: Vec<String>,
    pub cwd: String,
    pub runtime_envs: HashMap<String, String>,
    pub user_envs: HashMap<String, String>,
    pub resources: HashMap<String, f64>,
    pub rootfs: Option<RootfsSpec>,
    pub rootfs_readonly: bool,
    /// Explicit proto rootfs (from `container.rootfsConfig`); takes precedence over `rootfs`.
    pub rootfs_config: Option<RootfsConfig>,
    pub mounts: Vec<Mount>,
    /// network mode string ("sandbox" default / "host" / "none").
    pub network: String,
    /// Allocated `proto:host:container` mappings (see [`encode_port_mapping`]).
    pub port_mappings: Vec<String>,
    /// Non-empty for checkpoint-based (Restore) start.
    pub ckpt_dir: String,
    pub trace_id: String,
    pub stdout: String,
    pub stderr: String,
    pub extra_config: String,
}

/// Assemble the gRPC `StartRequest` (C++ `SandboxRequestBuilder::Build`).
pub fn build_start_request(params: &SandboxStartParams) -> StartRequest {
    let func_runtime = FunctionRuntime {
        id: params.runtime_id.clone(),
        sandbox: String::new(),
        rootfs: resolve_rootfs(params),
        make_seed: false,
        command: params.command.clone(),
        runtime_envs: params.runtime_envs.clone(),
        cwd: params.cwd.clone(),
        mounts: params.mounts.clone(),
    };
    StartRequest {
        func_runtime: Some(func_runtime),
        mounts: params.mounts.clone(),
        resources: params.resources.clone(),
        user_envs: params.user_envs.clone(),
        stdout: params.stdout.clone(),
        stderr: params.stderr.clone(),
        extra_config: params.extra_config.clone(),
        network: if params.network.is_empty() {
            "sandbox".to_string()
        } else {
            params.network.clone()
        },
        ckpt_dir: params.ckpt_dir.clone(),
        trace_id: params.trace_id.clone(),
        ports: params.port_mappings.clone(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_forward_ports_basic_and_defaults() {
        let v = parse_forward_ports(r#"{"portForwardings":[{"port":8080,"protocol":"TCP"},{"port":9090}]}"#);
        assert_eq!(
            v,
            vec![
                PortForward { container_port: 8080, protocol: "tcp".into() },
                PortForward { container_port: 9090, protocol: "tcp".into() },
            ]
        );
    }

    #[test]
    fn parse_forward_ports_skips_invalid_and_bad_json() {
        assert!(parse_forward_ports("").is_empty());
        assert!(parse_forward_ports("not json").is_empty());
        assert!(parse_forward_ports(r#"{"other":1}"#).is_empty());
        // out-of-range and non-integer ports skipped; valid one kept.
        let v = parse_forward_ports(
            r#"{"portForwardings":[{"port":0},{"port":70000},{"port":"x"},{"port":443,"protocol":"udp"}]}"#,
        );
        assert_eq!(v, vec![PortForward { container_port: 443, protocol: "udp".into() }]);
    }

    #[test]
    fn encode_mapping_format() {
        assert_eq!(encode_port_mapping("tcp", 40001, 8080), "tcp:40001:8080");
    }

    #[test]
    fn build_request_maps_fields_and_defaults_network() {
        let mut resources = HashMap::new();
        resources.insert("CPU".to_string(), 2000.0);
        let params = SandboxStartParams {
            runtime_id: "r1".into(),
            command: vec!["/runtime".into(), "cpp".into()],
            cwd: "/work".into(),
            resources: resources.clone(),
            rootfs: Some(RootfsSpec::Image("img:v1".into())),
            port_mappings: vec!["tcp:40001:8080".into()],
            ckpt_dir: String::new(),
            trace_id: "t1".into(),
            ..Default::default()
        };
        let req = build_start_request(&params);
        let fr = req.func_runtime.expect("func_runtime");
        assert_eq!(fr.id, "r1");
        assert_eq!(fr.command, vec!["/runtime", "cpp"]);
        assert_eq!(fr.cwd, "/work");
        let rootfs = fr.rootfs.expect("rootfs");
        assert_eq!(rootfs.r#type, RootfsSrcType::Image as i32);
        assert!(matches!(rootfs.source, Some(rootfs_config::Source::ImageUrl(u)) if u == "img:v1"));
        assert_eq!(req.resources.get("CPU"), Some(&2000.0));
        assert_eq!(req.ports, vec!["tcp:40001:8080"]);
        assert_eq!(req.trace_id, "t1");
        assert_eq!(req.network, "sandbox"); // defaulted
    }

    #[test]
    fn build_request_local_rootfs_and_explicit_network() {
        let params = SandboxStartParams {
            runtime_id: "r2".into(),
            rootfs: Some(RootfsSpec::Local("/rootfs".into())),
            network: "host".into(),
            ckpt_dir: "/ckpt/abc".into(),
            ..Default::default()
        };
        let req = build_start_request(&params);
        assert_eq!(req.network, "host");
        assert_eq!(req.ckpt_dir, "/ckpt/abc");
        let rootfs = req.func_runtime.unwrap().rootfs.unwrap();
        assert_eq!(rootfs.r#type, RootfsSrcType::Local as i32);
        assert!(matches!(rootfs.source, Some(rootfs_config::Source::Path(p)) if p == "/rootfs"));
    }
}
