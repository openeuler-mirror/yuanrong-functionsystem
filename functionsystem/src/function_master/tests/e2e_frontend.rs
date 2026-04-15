//! Scenario 11 — Go frontend compatibility: prefixed HTTP paths, JSON + protobuf bodies.

mod common;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use prost::Message;
use tower::ServiceExt;
use yr_master::http::build_router;
use yr_proto::common::ErrorCode;
use yr_proto::messages::QueryInstancesInfoResponse;
use yr_proto::resources::InstanceInfo;

use common::test_master_state;

#[tokio::test]
async fn e2e_frontend_global_scheduler_queryinstances_protobuf_matches_root() {
    let state = test_master_state();
    state.instances.upsert_instance(
        "/instances/gf1",
        serde_json::json!({
            "id": "gf1",
            "tenant": "t-go",
            "requestID": "rid-gf",
        }),
    );
    let app = build_router(state, None);

    let a = app
        .clone()
        .oneshot(
            Request::builder()
                .uri("/queryinstances")
                .header("Type", "protobuf")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    let b = app
        .oneshot(
            Request::builder()
                .uri("/global-scheduler/queryinstances")
                .header("Type", "protobuf")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(a.status(), StatusCode::OK);
    assert_eq!(b.status(), StatusCode::OK);
    let ba = axum::body::to_bytes(a.into_body(), usize::MAX)
        .await
        .unwrap();
    let bb = axum::body::to_bytes(b.into_body(), usize::MAX)
        .await
        .unwrap();
    assert_eq!(ba, bb);
    let decoded = QueryInstancesInfoResponse::decode(ba.as_ref()).unwrap();
    assert_eq!(decoded.code, ErrorCode::ErrNone as i32);
    assert_eq!(decoded.instance_infos.len(), 1);
    assert_eq!(decoded.instance_infos[0].instance_id, "gf1");
}

#[tokio::test]
async fn e2e_frontend_instance_manager_resources_protobuf_decodes() {
    let state = test_master_state();
    state.instances.upsert_instance(
        "/instances/rf1",
        serde_json::json!({"id": "rf1", "tenant": "t"}),
    );
    let app = build_router(state, None);

    let root = app
        .clone()
        .oneshot(
            Request::builder()
                .uri("/resources")
                .header("Type", "protobuf")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    let prefixed = app
        .oneshot(
            Request::builder()
                .uri("/instance-manager/resources")
                .header("Type", "protobuf")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(root.status(), prefixed.status());
    let br = axum::body::to_bytes(root.into_body(), usize::MAX)
        .await
        .unwrap();
    let bp = axum::body::to_bytes(prefixed.into_body(), usize::MAX)
        .await
        .unwrap();
    assert_eq!(br, bp);
    let decoded = yr_proto::messages::ResourceInfo::decode(br.as_ref()).unwrap();
    let ru = decoded.resource.expect("resource unit");
    assert!(ru.instances.contains_key("rf1"));
    let inst: &InstanceInfo = ru.instances.get("rf1").unwrap();
    assert_eq!(inst.instance_id, "rf1");
}

#[tokio::test]
async fn e2e_frontend_prefixed_named_ins_json_matches_root() {
    let state = test_master_state();
    let app = build_router(state, None);
    let a = app
        .clone()
        .oneshot(
            Request::builder()
                .uri("/named-ins")
                .body(Body::from("req-fe"))
                .unwrap(),
        )
        .await
        .unwrap();
    let b = app
        .oneshot(
            Request::builder()
                .uri("/instance-manager/named-ins")
                .body(Body::from("req-fe"))
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(a.status(), b.status());
    let ba = axum::body::to_bytes(a.into_body(), usize::MAX)
        .await
        .unwrap();
    let bb = axum::body::to_bytes(b.into_body(), usize::MAX)
        .await
        .unwrap();
    assert_eq!(ba, bb);
}
