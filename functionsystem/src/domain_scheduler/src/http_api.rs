use std::sync::Arc;

use axum::extract::State;
use axum::http::{HeaderMap, StatusCode};
use axum::response::IntoResponse;
use axum::routing::get;
use axum::{Json, Router};
use serde_json::json;

use crate::state::DomainSchedulerState;

#[derive(Clone)]
pub struct HttpState {
    pub inner: Arc<DomainSchedulerState>,
}

pub fn build_router(state: Arc<DomainSchedulerState>) -> Router {
    let s = HttpState { inner: state };
    Router::new()
        .route("/healthy", get(healthy))
        .route("/resources", get(resources))
        .route("/scheduling_queue", get(scheduling_queue))
        .with_state(s)
}

fn healthy_probe_header<'a>(headers: &'a HeaderMap, name: &str) -> Option<&'a str> {
    headers
        .get(name)
        .and_then(|v| v.to_str().ok())
        .map(str::trim)
        .filter(|s| !s.is_empty())
}

async fn healthy(State(st): State<HttpState>, headers: HeaderMap) -> impl IntoResponse {
    let node_hdr = healthy_probe_header(&headers, "node-id");
    let pid_hdr = healthy_probe_header(&headers, "pid");

    if node_hdr.is_none() && pid_hdr.is_none() {
        return (StatusCode::OK, "").into_response();
    }

    let expected_node = st.inner.config.node_id.as_str();
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

async fn resources(State(st): State<HttpState>) -> Json<serde_json::Value> {
    let mut body = st.inner.resource_view.domain_summary();
    if let Some(obj) = body.as_object_mut() {
        obj.insert("locals".into(), json!(st.inner.nodes.list_nodes_summary()));
    }
    Json(body)
}

async fn scheduling_queue(State(st): State<HttpState>) -> Json<serde_json::Value> {
    Json(json!({
        "pending_count": st.inner.scheduler.pending_len(),
        "pending": st.inner.scheduler.pending_snapshot_json(),
    }))
}
