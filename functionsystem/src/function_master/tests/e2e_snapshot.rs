//! E2E: instance snapshots — terminal transitions, HTTP query APIs, manual restore.

mod common;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use serde_json::{json, Value};
use tower::ServiceExt;
use yr_master::http::build_router;

use common::test_master_state;

#[tokio::test]
async fn snapshot_created_on_exited_and_queryable_via_http() {
    let state = test_master_state();
    let app = build_router(state.clone(), None);

    state.instances.upsert_instance(
        "/pre/snap-e2e-1",
        json!({
            "id": "snap-e2e-1",
            "function_name": "echo",
            "tenant": "ten",
            "state": "EXITED",
            "created_at_ms": 100,
            "updated_at_ms": 200,
            "node_id": "n1",
            "function_proxy_id": "p1",
        }),
    );

    let resp = app
        .clone()
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/query-snapshot")
                .header("Type", "application/json")
                .body(Body::from("snap-e2e-1"))
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: Value = serde_json::from_slice(&body).unwrap();
    assert_eq!(v["instanceID"], "snap-e2e-1");
    assert_eq!(v["functionName"], "echo");

    let list = app
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/list-snapshots")
                .header("Type", "application/json")
                .body(Body::from("echo"))
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(list.status(), StatusCode::OK);
    let list_body = axum::body::to_bytes(list.into_body(), usize::MAX)
        .await
        .unwrap();
    let arr: Vec<Value> = serde_json::from_slice(&list_body).unwrap();
    assert!(arr.iter().any(|row| row["instanceID"] == "snap-e2e-1"));
}

#[tokio::test]
async fn snapshot_manual_restore_after_drop() {
    let state = test_master_state();
    let app = build_router(state.clone(), None);

    state.instances.upsert_instance(
        "/pre/snap-e2e-2",
        json!({
            "id": "snap-e2e-2",
            "function_name": "f2",
            "tenant": "t2",
            "state": "FAILED",
            "created_at_ms": 1,
            "updated_at_ms": 2,
            "node_id": "n2",
            "function_proxy_id": "px",
        }),
    );

    let snap = state.snapshots.get("snap-e2e-2").expect("snap exists");
    state.snapshots.remove("snap-e2e-2");
    state.instances.remove_instance_key("/pre/snap-e2e-2");
    assert!(state.snapshots.get("snap-e2e-2").is_none());

    state.snapshots.create_snapshot(snap);

    let resp = app
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/query-snapshot")
                .header("Type", "application/json")
                .body(Body::from("snap-e2e-2"))
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
}
