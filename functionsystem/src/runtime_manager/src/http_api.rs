use axum::extract::State;
use axum::http::{HeaderMap, StatusCode};
use axum::response::IntoResponse;
use axum::{routing::get, Router};
use std::net::SocketAddr;
use std::sync::{Arc, OnceLock};
use std::time::Instant;
use tokio::net::TcpListener;
use tokio::sync::broadcast;
use tower_http::trace::TraceLayer;
use tracing::info;

use crate::config::Config;
use crate::metrics::prometheus_text;

static PROCESS_START: OnceLock<Instant> = OnceLock::new();

/// Call once at process start so `/healthy` startup grace is meaningful.
pub fn mark_process_start() {
    let _ = PROCESS_START.set(Instant::now());
}

/// Shared state for [`router`] / [`serve`] (HTTP health + metrics).
#[derive(Clone)]
pub struct HttpState {
    pub cfg: Arc<Config>,
}

/// Manager-level probes (HTTP GET + TCP connect + startup grace).
pub async fn manager_deep_health(cfg: &Config) -> (bool, String) {
    let grace = std::time::Duration::from_secs(cfg.manager_startup_probe_secs.max(1));
    if let Some(t0) = PROCESS_START.get() {
        if t0.elapsed() < grace {
            return (true, "startup_grace".into());
        }
    }
    let mut ok = true;
    let mut parts = Vec::new();

    let u = cfg.manager_health_http_url.trim();
    if !u.is_empty() {
        let client = match reqwest::Client::builder()
            .timeout(std::time::Duration::from_secs(3))
            .build()
        {
            Ok(c) => c,
            Err(e) => {
                return (false, format!("http_client:{e}"));
            }
        };
        let pid = std::process::id().to_string();
        match client
            .get(u)
            .header("Node-ID", cfg.node_id.as_str())
            .header("PID", pid.as_str())
            .send()
            .await
        {
            Ok(r) if r.status().is_success() => parts.push("http:ok".into()),
            Ok(r) => {
                ok = false;
                parts.push(format!("http:bad_status:{}", r.status()));
            }
            Err(e) => {
                ok = false;
                parts.push(format!("http:{e}"));
            }
        }
    }

    let tcp = cfg.manager_health_tcp.trim();
    if !tcp.is_empty() {
        let conn = async {
            let s = tcp.rsplit_once(':')?;
            let port: u16 = s.1.parse().ok()?;
            tokio::net::TcpStream::connect(format!("{}:{port}", s.0)).await.ok()?;
            Some(())
        };
        match tokio::time::timeout(std::time::Duration::from_secs(2), conn).await {
            Ok(Some(())) => parts.push("tcp:ok".into()),
            _ => {
                ok = false;
                parts.push("tcp:fail".into());
            }
        }
    }

    if parts.is_empty() {
        parts.push("no_extra_probes".into());
    }
    (ok, parts.join(";"))
}

async fn healthy(State(st): State<HttpState>, headers: HeaderMap) -> impl IntoResponse {
    let expected_node = st.cfg.node_id.as_str();
    let node_ok = headers
        .get("node-id")
        .and_then(|v| v.to_str().ok())
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .is_some_and(|v| v == expected_node);
    if !node_ok {
        return (StatusCode::BAD_REQUEST, "error nodeID").into_response();
    }
    let pid = std::process::id();
    let pid_ok = headers
        .get("pid")
        .and_then(|v| v.to_str().ok())
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .and_then(|s| s.parse::<u32>().ok())
        == Some(pid);
    if !pid_ok {
        return (StatusCode::BAD_REQUEST, "error PID").into_response();
    }
    let (deep_ok, detail) = manager_deep_health(&st.cfg).await;
    if !deep_ok {
        return (StatusCode::SERVICE_UNAVAILABLE, detail).into_response();
    }
    (StatusCode::OK, "").into_response()
}

async fn health_deep(State(st): State<HttpState>) -> impl IntoResponse {
    let (ok, detail) = manager_deep_health(&st.cfg).await;
    let code = if ok {
        StatusCode::OK
    } else {
        StatusCode::SERVICE_UNAVAILABLE
    };
    (code, detail).into_response()
}

async fn metrics_text() -> axum::response::Response {
    axum::response::Response::builder()
        .status(StatusCode::OK)
        .header(
            axum::http::header::CONTENT_TYPE,
            "text/plain; version=0.0.4",
        )
        .body(prometheus_text().into())
        .unwrap()
}

/// Axum router for `GET /healthy`, `GET /metrics`, `GET /healthz/deep` (used by the binary and tests).
pub fn router(cfg: Arc<Config>) -> Router<()> {
    let st = HttpState { cfg };
    Router::new()
        .route("/healthy", get(healthy))
        .route("/healthz/deep", get(health_deep))
        .route("/metrics", get(metrics_text))
        .layer(TraceLayer::new_for_http())
        .with_state(st)
}

/// Serves `GET /healthy`, `GET /metrics`, `GET /healthz/deep`.
pub async fn serve(
    addr: SocketAddr,
    mut shutdown: broadcast::Receiver<()>,
    cfg: Arc<Config>,
) -> anyhow::Result<()> {
    let app = router(cfg);
    let listener = TcpListener::bind(addr).await?;
    info!(%addr, "HTTP health server listening");
    axum::serve(listener, app)
        .with_graceful_shutdown(async move {
            let _ = shutdown.recv().await;
        })
        .await?;
    Ok(())
}
