use std::sync::Arc;

use axum::extract::State;
use axum::http::{HeaderMap, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::routing::get;
use axum::{Json, Router};
use serde_json::Value;
use tower_http::trace::TraceLayer;

use crate::node_manager::NodeManager;
use crate::registration::SchedulerLink;
use crate::rm_client::RuntimeManagerClient;

#[derive(Clone)]
pub struct HealthState {
    pub rm: Arc<RuntimeManagerClient>,
    pub scheduler: Arc<SchedulerLink>,
    pub node_id: String,
    pub node: Arc<NodeManager>,
}

pub fn router(state: HealthState) -> Router {
    Router::new()
        .route("/healthy", get(healthy))
        .route("/function-agent/healthy", get(healthy))
        .route("/readiness", get(readiness))
        .layer(TraceLayer::new_for_http())
        .with_state(state)
}

fn healthy_probe_header<'a>(headers: &'a HeaderMap, name: &str) -> Option<&'a str> {
    headers
        .get(name)
        .and_then(|v| v.to_str().ok())
        .map(str::trim)
        .filter(|s| !s.is_empty())
}

async fn healthy(State(st): State<HealthState>, headers: HeaderMap) -> Response {
    let node_hdr = healthy_probe_header(&headers, "node-id");
    let pid_hdr = healthy_probe_header(&headers, "pid");

    if node_hdr.is_none() && pid_hdr.is_none() {
        return (StatusCode::OK, "").into_response();
    }

    let expected_node = st.node_id.as_str();
    let node_ok = node_hdr.is_some_and(|v| v == expected_node);
    if !node_ok {
        return (StatusCode::BAD_REQUEST, "error nodeID").into_response();
    }
    let pid = std::process::id();
    let pid_ok = pid_hdr
        .and_then(|s| s.parse::<u32>().ok())
        == Some(pid);
    if !pid_ok {
        return (StatusCode::BAD_REQUEST, "error PID").into_response();
    }
    (StatusCode::OK, "").into_response()
}

async fn readiness(
    axum::extract::State(state): axum::extract::State<HealthState>,
) -> (StatusCode, Json<Value>) {
    let rm_ok = state.rm.readiness_probe().await;
    let sched_ok = state.scheduler.heartbeat_ping().await.is_ok();
    let node_ok = state.node.ready();
    let ok = rm_ok && sched_ok && node_ok;
    let status = if ok {
        StatusCode::OK
    } else {
        StatusCode::SERVICE_UNAVAILABLE
    };
    (
        status,
        Json(serde_json::json!({
            "ready": ok,
            "runtime_manager": rm_ok,
            "scheduler": sched_ok,
            "node_ready_flag": node_ok,
        })),
    )
}
