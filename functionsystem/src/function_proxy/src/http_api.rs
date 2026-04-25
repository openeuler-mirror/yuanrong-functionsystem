use crate::resource_view::ResourceView;
use axum::extract::State;
use axum::http::{HeaderMap, StatusCode};
use axum::response::{IntoResponse, Response};
use axum::routing::get;
use axum::{Json, Router};
use serde_json::Value;
use std::sync::Arc;

#[derive(Clone)]
pub struct HttpState {
    pub resource_view: Arc<ResourceView>,
    pub node_id: String,
}

pub fn router(state: HttpState) -> Router {
    Router::new()
        .route("/healthy", get(healthy))
        .route("/local-scheduler/healthy", get(healthy))
        .route("/resources", get(resources))
        .with_state(state)
}

fn healthy_probe_header<'a>(headers: &'a HeaderMap, name: &str) -> Option<&'a str> {
    headers
        .get(name)
        .and_then(|v| v.to_str().ok())
        .map(str::trim)
        .filter(|s| !s.is_empty())
}

async fn healthy(State(st): State<HttpState>, headers: HeaderMap) -> Response {
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
    let pid_ok = pid_hdr.and_then(|s| s.parse::<u32>().ok()) == Some(pid);
    if !pid_ok {
        return (StatusCode::BAD_REQUEST, "error PID").into_response();
    }
    (StatusCode::OK, "").into_response()
}

async fn resources(State(st): State<HttpState>) -> Json<Value> {
    let s = st.resource_view.snapshot_json();
    serde_json::from_str(&s).unwrap_or(Value::Null).into()
}
