//! E2E: resource group scheduling surface and instance group queries.

mod common;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use serde_json::{json, Value};
use tower::ServiceExt;
use yr_master::http::build_router;
use yr_proto::internal::{GroupScheduleRequest, ScheduleRequest};

use common::test_master_state;

#[tokio::test]
async fn group_schedule_rejects_empty_group_id() {
    let state = test_master_state();
    let r = state
        .do_group_schedule(GroupScheduleRequest {
            group_id: String::new(),
            requests: vec![ScheduleRequest {
                request_id: "r1".into(),
                ..Default::default()
            }],
            ..Default::default()
        })
        .await;
    assert!(!r.success);
    assert!(r.message.contains("group_id") || r.message.contains("required"));
}

#[tokio::test]
async fn group_schedule_rejects_empty_requests() {
    let state = test_master_state();
    let r = state
        .do_group_schedule(GroupScheduleRequest {
            group_id: "g-empty".into(),
            requests: vec![],
            ..Default::default()
        })
        .await;
    assert!(!r.success);
    assert!(r.message.contains("empty"));
}

#[tokio::test]
async fn group_schedule_accepts_batch_when_topology_has_root() {
    let state = test_master_state();
    state
        .topology
        .register_local("node-g".into(), "10.0.0.2:1".into(), "{}".into(), "{}".into())
        .await;
    state.rebuild_domain_routes();

    let r = state
        .do_group_schedule(GroupScheduleRequest {
            group_id: "g-batch".into(),
            requests: vec![
                ScheduleRequest {
                    request_id: "sub-a".into(),
                    ..Default::default()
                },
                ScheduleRequest {
                    request_id: "sub-b".into(),
                    ..Default::default()
                },
            ],
            ..Default::default()
        })
        .await;
    assert_eq!(r.group_id, "g-batch");
}

#[tokio::test]
async fn query_group_instances_http_lists_members() {
    let state = test_master_state();
    let app = build_router(state.clone(), None);

    state.instances.upsert_instance(
        "/i/a",
        json!({"id": "a", "tenant": "t", "group_id": "G-E2E"}),
    );
    state.instances.upsert_instance(
        "/i/b",
        json!({"id": "b", "tenant": "t", "groupID": "G-E2E"}),
    );
    state
        .instances
        .upsert_instance("/i/c", json!({"id": "c", "tenant": "t", "group_id": "other"}));

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/query-group-instances?group_id=G-E2E")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: Value = serde_json::from_slice(&body).unwrap();
    let inst = v["instances"].as_array().expect("instances array");
    let ids: Vec<_> = inst
        .iter()
        .filter_map(|x| x["id"].as_str())
        .collect();
    assert!(ids.contains(&"a"));
    assert!(ids.contains(&"b"));
    assert!(!ids.contains(&"c"));
}

#[tokio::test]
async fn rgroup_query_returns_stable_json_shape() {
    let state = test_master_state();
    let app = build_router(state, None);
    let resp = app
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/rgroup")
                .header("Type", "application/json")
                .body(Body::from(r#"{"requestID":"rid-e2e"}"#))
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: Value = serde_json::from_slice(&body).unwrap();
    assert_eq!(v["requestID"], "rid-e2e");
    assert!(v["groups"].is_array());
    assert_eq!(v["count"], 0);
}

#[tokio::test]
async fn group_scale_down_via_instance_removal_updates_query() {
    let state = test_master_state();
    let app = build_router(state.clone(), None);

    state.instances.upsert_instance(
        "/i/x1",
        json!({"id": "x1", "group_id": "G-SCALE"}),
    );
    state.instances.upsert_instance(
        "/i/x2",
        json!({"id": "x2", "group_id": "G-SCALE"}),
    );

    let resp1 = app
        .clone()
        .oneshot(
            Request::builder()
                .uri("/query-group-instances?group_id=G-SCALE")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    let v1: Value = serde_json::from_slice(
        &axum::body::to_bytes(resp1.into_body(), usize::MAX)
            .await
            .unwrap(),
    )
    .unwrap();
    assert_eq!(v1["instances"].as_array().unwrap().len(), 2);

    state.instances.remove_instance_key("/i/x2");

    let resp2 = app
        .oneshot(
            Request::builder()
                .uri("/query-group-instances?group_id=G-SCALE")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    let v2: Value = serde_json::from_slice(
        &axum::body::to_bytes(resp2.into_body(), usize::MAX)
            .await
            .unwrap(),
    )
    .unwrap();
    let ids: Vec<_> = v2["instances"]
        .as_array()
        .unwrap()
        .iter()
        .filter_map(|x| x["id"].as_str())
        .collect();
    assert_eq!(ids, vec!["x1"]);
}
