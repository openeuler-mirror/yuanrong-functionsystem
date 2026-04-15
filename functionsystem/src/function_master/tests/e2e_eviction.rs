//! Scenario 10 — agent eviction via HTTP and topology consistency.

mod common;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use serde_json::json;
use tower::ServiceExt;
use yr_master::http::build_router;

use common::test_master_state;

#[tokio::test]
async fn e2e_evict_http_removes_agent_from_topology() {
    let state = test_master_state();
    state
        .topology
        .register_local(
            "agent-evict-1".into(),
            "10.8.0.1:1".into(),
            "{}".into(),
            "{}".into(),
        )
        .await;
    let app = build_router(state.clone(), None);

    let resp = app
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/evictagent")
                .header("content-type", "application/json")
                .body(Body::from(
                    r#"{"agentid":"agent-evict-1","timeoutsec":5}"#,
                ))
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
    assert_eq!(v["code"], 0);
    assert_eq!(state.topology.agent_count(), 0);
}

#[tokio::test]
async fn e2e_evict_instances_on_node_remain_until_explicit_lifecycle() {
    let state = test_master_state();
    state
        .topology
        .register_local("agent-with-ins".into(), "10.8.1.1:1".into(), "{}".into(), "{}".into())
        .await;
    state.instances.upsert_instance(
        "/instances/e2e-ev-1",
        json!({
            "id": "e2e-ev-1",
            "tenant": "tenant-z",
            "node_id": "agent-with-ins",
            "function_proxy_id": "agent-with-ins",
        }),
    );
    assert!(state.topology.evict("agent-with-ins").await);
    assert_eq!(state.topology.agent_count(), 0);
    assert_eq!(state.instances.count(), 1);
    let (rows, n) = state.instances.query_by_tenant("tenant-z", None);
    assert_eq!(n, 1);
    assert_eq!(rows[0]["id"], "e2e-ev-1");
}

#[tokio::test]
async fn e2e_evict_queryagentcount_drops_after_eviction() {
    let state = test_master_state();
    state
        .topology
        .register_local("c1".into(), "h1".into(), "{}".into(), "{}".into())
        .await;
    state
        .topology
        .register_local("c2".into(), "h2".into(), "{}".into(), "{}".into())
        .await;
    let app = build_router(state.clone(), None);
    let before = app
        .clone()
        .oneshot(
            Request::builder()
                .uri("/queryagentcount")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    let btxt = String::from_utf8(
        axum::body::to_bytes(before.into_body(), usize::MAX)
            .await
            .unwrap()
            .to_vec(),
    )
    .unwrap();
    assert_eq!(btxt, "2");

    assert!(state.topology.evict("c1").await);
    let app = build_router(state, None);
    let after = app
        .oneshot(
            Request::builder()
                .uri("/queryagentcount")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    let atxt = String::from_utf8(
        axum::body::to_bytes(after.into_body(), usize::MAX)
            .await
            .unwrap()
            .to_vec(),
    )
    .unwrap();
    assert_eq!(atxt, "1");
}
