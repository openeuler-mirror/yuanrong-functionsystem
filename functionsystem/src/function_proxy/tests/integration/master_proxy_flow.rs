//! Master HTTP API ↔ proxy registration contract (in-memory master, no network).

use axum::body::Body;
use axum::http::{Request, StatusCode};
use http_body_util::BodyExt;
use prost::Message;
use serde_json::json;
use tower::ServiceExt;
use yr_master::http::build_router;
use yr_proto::internal::RegisterRequest;
use yr_proto::messages::QueryInstancesInfoResponse;
use yr_proto::messages::ResourceInfo;

use super::test_master_state;

#[test]
fn register_request_proto_matches_proxy_registration_shape() {
    let req = RegisterRequest {
        node_id: "proxy-1".into(),
        address: "http://127.0.0.1:9000".into(),
        resource_json: r#"{"cpu":{"scalar":{"value":8.0}}}"#.into(),
        agent_info_json: r#"{"agents":[]}"#.into(),
        resource_unit: None,
    };
    let v = req.encode_to_vec();
    let dec = RegisterRequest::decode(v.as_slice()).unwrap();
    assert_eq!(dec.node_id, "proxy-1");
    assert!(dec.address.starts_with("http://"));
}

#[tokio::test]
async fn agent_registration_updates_topology_and_queryagents() {
    let state = test_master_state();
    state
        .topology
        .register_local(
            "agent-a".into(),
            "10.0.0.1:1".into(),
            r#"{"cpu":1}"#.into(),
            None,
            "{}".into(),
        )
        .await;

    let app = build_router(state, None);
    let resp = app
        .oneshot(
            Request::builder()
                .uri("/queryagents")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    let body = resp.into_body().collect().await.unwrap().to_bytes();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
    assert!(v.is_array() || v.is_object(), "queryagents returns JSON");
}

#[tokio::test]
async fn resources_protobuf_includes_resource_unit_and_instances_map() {
    let state = test_master_state();
    state.instances.upsert_instance(
        "/instance/i1",
        json!({
            "id": "i1",
            "requestID": "rid-1",
            "function": "f",
            "tenant": "t1",
        }),
    );

    let app = build_router(state, None);
    let resp = app
        .oneshot(
            Request::builder()
                .uri("/resources")
                .header("Type", "protobuf")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(resp.status(), StatusCode::OK);
    let body = resp.into_body().collect().await.unwrap().to_bytes();
    let info = ResourceInfo::decode(body.as_ref()).expect("ResourceInfo protobuf");
    let unit = info.resource.expect("resource unit");
    assert_eq!(unit.id, "test-cluster");
    assert!(unit.instances.contains_key("i1"));
}

#[tokio::test]
async fn evict_agent_removes_leaf_and_queryagentcount_drops() {
    let state = test_master_state();
    state
        .topology
        .register_local(
            "to-evict".into(),
            "h:1".into(),
            "{}".into(),
            None,
            "{}".into(),
        )
        .await;

    let app = build_router(state.clone(), None);

    let count_before = app
        .clone()
        .oneshot(
            Request::builder()
                .uri("/queryagentcount")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    let n0 = String::from_utf8(
        count_before
            .into_body()
            .collect()
            .await
            .unwrap()
            .to_bytes()
            .to_vec(),
    )
    .unwrap();
    assert_eq!(n0, "1");

    let evict = app
        .clone()
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/evictagent")
                .header("content-type", "application/json")
                .body(Body::from(
                    json!({"agentid": "to-evict", "timeoutsec": 0, "reason": "test"}).to_string(),
                ))
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(evict.status(), StatusCode::OK);

    let count_after = app
        .oneshot(
            Request::builder()
                .uri("/queryagentcount")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    let n1 = String::from_utf8(
        count_after
            .into_body()
            .collect()
            .await
            .unwrap()
            .to_bytes()
            .to_vec(),
    )
    .unwrap();
    assert_eq!(n1, "0");
}

#[tokio::test]
async fn scheduling_queue_json_and_protobuf_formats() {
    let state = test_master_state();
    state.instances.upsert_instance(
        "/instance/qi",
        json!({
            "id": "qi",
            "requestID": "sched-req-1",
            "function": "g",
        }),
    );
    {
        let mut q = state.scheduling_queue.lock();
        q.push_back("sched-req-1".into());
    }

    let app = build_router(state, None);

    let j = app
        .clone()
        .oneshot(
            Request::builder()
                .uri("/scheduling_queue")
                .header("Type", "json")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(j.status(), StatusCode::OK);
    let jb = j.into_body().collect().await.unwrap().to_bytes();
    let jq: serde_json::Value = serde_json::from_slice(&jb).unwrap();
    assert_eq!(jq["len"], 1);
    assert_eq!(jq["queue"].as_array().unwrap().len(), 1);

    let p = app
        .oneshot(
            Request::builder()
                .uri("/scheduling_queue")
                .header("Type", "protobuf")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(p.status(), StatusCode::OK);
    let pb = p.into_body().collect().await.unwrap().to_bytes();
    let qi = QueryInstancesInfoResponse::decode(pb.as_ref()).unwrap();
    assert_eq!(qi.instance_infos.len(), 1);
    assert_eq!(qi.instance_infos[0].request_id, "sched-req-1");
}
