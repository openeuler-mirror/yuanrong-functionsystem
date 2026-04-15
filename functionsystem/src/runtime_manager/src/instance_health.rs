//! HTTP/TCP/startup probes derived from instance `config_json`.

use crate::state::{InstanceHealthSpec, RuntimeManagerState};
use serde::Deserialize;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio::net::TcpStream;
use tokio::time::timeout;

#[derive(Debug, Deserialize, Default)]
struct HealthJson {
    http: Option<String>,
    tcp: Option<String>,
    #[serde(default)]
    startup_seconds: Option<u64>,
}

pub fn parse_from_config_json(config_json: &str, default_startup: Duration) -> InstanceHealthSpec {
    let Ok(v) = serde_json::from_str::<serde_json::Value>(config_json) else {
        return InstanceHealthSpec {
            startup_deadline: default_startup,
            ..Default::default()
        };
    };
    let h = v
        .get("health")
        .and_then(|x| serde_json::from_value::<HealthJson>(x.clone()).ok());
    let Some(h) = h else {
        return InstanceHealthSpec {
            startup_deadline: default_startup,
            ..Default::default()
        };
    };
    let (tcp_host, tcp_port) = match h.tcp.as_ref().and_then(|s| parse_host_port(s)) {
        Some((h, p)) => (Some(h), Some(p)),
        None => (None, None),
    };
    InstanceHealthSpec {
        http_url: h.http.filter(|u| !u.trim().is_empty()),
        tcp_host,
        tcp_port,
        startup_deadline: Duration::from_secs(h.startup_seconds.unwrap_or(30).max(1)),
    }
}

fn parse_host_port(s: &str) -> Option<(String, u16)> {
    let s = s.trim();
    let (host, port_s) = s.rsplit_once(':')?;
    let port: u16 = port_s.parse().ok()?;
    if host.is_empty() {
        return None;
    }
    Some((host.to_string(), port))
}

pub async fn probe_http(url: &str) -> bool {
    let client = match reqwest::Client::builder()
        .timeout(Duration::from_secs(3))
        .build()
    {
        Ok(c) => c,
        Err(_) => return false,
    };
    match client.get(url).send().await {
        Ok(r) => r.status().is_success(),
        Err(_) => false,
    }
}

pub async fn probe_tcp(host: &str, port: u16) -> bool {
    let addr = format!("{host}:{port}");
    match timeout(
        Duration::from_secs(2),
        TcpStream::connect(addr),
    )
    .await
    {
        Ok(Ok(_s)) => true,
        _ => false,
    }
}

/// Process alive + optional HTTP/TCP once past startup deadline.
pub async fn evaluate(
    pid: i32,
    spec: &InstanceHealthSpec,
    started_at: Instant,
    default_startup: Duration,
) -> &'static str {
    let proc_ok = std::path::Path::new(&format!("/proc/{pid}")).exists();
    if !proc_ok {
        return "down";
    }
    let grace = if spec.startup_deadline.is_zero() {
        default_startup
    } else {
        spec.startup_deadline
    };
    if started_at.elapsed() < grace {
        return "starting";
    }
    if let Some(url) = &spec.http_url {
        if !probe_http(url).await {
            return "unhealthy_http";
        }
    }
    if let (Some(h), Some(p)) = (&spec.tcp_host, spec.tcp_port) {
        if !probe_tcp(h, p).await {
            return "unhealthy_tcp";
        }
    }
    "healthy"
}

/// Background HTTP/TCP probes for each tracked runtime (from `config_json.health`).
pub async fn supervision_loop(state: Arc<RuntimeManagerState>) {
    let iv = Duration::from_millis(state.config.instance_health_interval_ms.max(300));
    let default_grace = Duration::from_secs(state.config.manager_startup_probe_secs.max(1));
    loop {
        tokio::time::sleep(iv).await;
        let rids = state.list_runtime_ids();
        for rid in rids {
            let Some(p) = state.get_by_runtime(&rid) else {
                continue;
            };
            let label = evaluate(
                p.pid,
                &p.health_spec,
                p.started_at,
                default_grace,
            )
            .await;
            state.apply_health_status(&rid, label);
        }
    }
}
