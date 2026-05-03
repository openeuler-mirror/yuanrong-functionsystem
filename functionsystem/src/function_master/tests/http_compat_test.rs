//! HTTP API compatibility tests — validates 1:1 contract with C++ original.

mod common;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use axum08::body::Body as Body08;
use prost::Message;
use tower::ServiceExt;
use yr_master::http::{build_grpc_compat_router, build_router};
use yr_master::snapshot::InstanceSnapshot;
use yr_proto::common::ErrorCode;
use yr_proto::messages::QueryInstancesInfoResponse;
use yr_proto::resources::InstanceInfo;

use common::test_master_state;

use serde_json;

/// ST: master_http_queryagentcount_format
/// Original C++ returns a plain-text integer string, not JSON.
#[tokio::test]
async fn queryagentcount_returns_plain_text_number() {
    let state = test_master_state();
    let app = build_router(state.clone(), None);

    // Register two agents
    state
        .topology
        .register_local("a1".into(), "h1".into(), "{}".into(), None, "{}".into())
        .await;
    state
        .topology
        .register_local("a2".into(), "h2".into(), "{}".into(), None, "{}".into())
        .await;

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/queryagentcount")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);

    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let text = String::from_utf8(body.to_vec()).unwrap();

    // Must be a plain integer, not JSON like {"count":2}
    assert_eq!(text, "2", "should be plain text number, not JSON");
    assert!(
        text.parse::<u64>().is_ok(),
        "response must be parseable as integer"
    );
}

/// ST: master_http_queryagentcount_format — zero case
#[tokio::test]
async fn queryagentcount_returns_zero_when_empty() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/queryagentcount")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let text = String::from_utf8(body.to_vec()).unwrap();
    assert_eq!(text, "0");
}

/// ST: master_http_evictagent_request_shape
/// Original C++ accepts `{"agentid":"...","timeoutsec":N}` and returns `{"code":0,"message":"..."}`.
#[tokio::test]
async fn evictagent_accepts_original_field_names() {
    let state = test_master_state();
    state
        .topology
        .register_local(
            "agent-42".into(),
            "h:1".into(),
            "{}".into(),
            None,
            "{}".into(),
        )
        .await;

    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/evictagent")
                .header("content-type", "application/json")
                .body(Body::from(r#"{"agentid":"agent-42","timeoutsec":30}"#))
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);

    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();

    assert_eq!(v["code"], 0, "successful eviction should return code 0");
    assert!(
        v.get("message").is_some(),
        "response must contain 'message' field"
    );
}

/// ST: master_http_evictagent_request_shape — evict non-existent agent returns code 1
#[tokio::test]
async fn evictagent_returns_error_code_for_missing_agent() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/evictagent")
                .header("content-type", "application/json")
                .body(Body::from(r#"{"agentid":"nonexistent","timeoutsec":10}"#))
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);

    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();

    assert_eq!(
        v["code"], 1,
        "evicting non-existent agent should return code 1"
    );
}

/// ST: master_http_evictagent_request_shape — backward compat with `node_id` alias
#[tokio::test]
async fn evictagent_accepts_node_id_alias() {
    let state = test_master_state();
    state
        .topology
        .register_local(
            "agent-99".into(),
            "h:1".into(),
            "{}".into(),
            None,
            "{}".into(),
        )
        .await;

    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/evictagent")
                .header("content-type", "application/json")
                .body(Body::from(r#"{"node_id":"agent-99","reason":"test"}"#))
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);

    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
    assert_eq!(
        v["code"], 0,
        "node_id alias should work for backward compat"
    );
}

// --- ST: master_http_resources_type_header ---

/// Default (no Type header) returns JSON.
#[tokio::test]
async fn resources_default_returns_json() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/resources")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let ct = resp
        .headers()
        .get("content-type")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("");
    assert!(
        ct.contains("application/json"),
        "default should be JSON, got: {ct}"
    );
}

/// Type: json explicitly → JSON.
#[tokio::test]
async fn resources_type_json_returns_json() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/resources")
                .header("Type", "json")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let ct = resp
        .headers()
        .get("content-type")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("");
    assert!(ct.contains("application/json"));
}

/// Type: protobuf → binary body with protobuf content-type.
#[tokio::test]
async fn resources_type_protobuf_returns_binary() {
    let state = test_master_state();
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
    let ct = resp
        .headers()
        .get("content-type")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("");
    assert!(
        ct.contains("protobuf"),
        "protobuf type should return protobuf content-type, got: {ct}"
    );
}

/// /resources with unsupported Type value → 400 Bad Request (strict validation like C++).
#[tokio::test]
async fn resources_unsupported_type_returns_400() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/resources")
                .header("Type", "xml")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(
        resp.status(),
        StatusCode::BAD_REQUEST,
        "/resources must reject unsupported Type values"
    );
}

// --- ST: master_http_scheduling_queue_type_header ---

/// Default (no Type) → JSON.
#[tokio::test]
async fn scheduling_queue_default_returns_json() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/scheduling_queue")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let ct = resp
        .headers()
        .get("content-type")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("");
    assert!(
        ct.contains("application/json"),
        "default should be JSON, got: {ct}"
    );
}

/// Type: protobuf → binary protobuf (lenient: any non-json selects protobuf).
#[tokio::test]
async fn scheduling_queue_type_protobuf_returns_binary() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/scheduling_queue")
                .header("Type", "protobuf")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let ct = resp
        .headers()
        .get("content-type")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("");
    assert!(
        ct.contains("protobuf"),
        "protobuf type should return protobuf, got: {ct}"
    );
}

/// /scheduling_queue with unknown Type value → still protobuf (lenient, unlike /resources).
#[tokio::test]
async fn scheduling_queue_unknown_type_returns_protobuf_lenient() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/scheduling_queue")
                .header("Type", "xml")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(
        resp.status(),
        StatusCode::OK,
        "/scheduling_queue should accept any Type value (lenient)"
    );
    let ct = resp
        .headers()
        .get("content-type")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("");
    assert!(
        ct.contains("protobuf"),
        "unknown Type should fall through to protobuf, got: {ct}"
    );
}

// ============== Instance Manager HTTP endpoints ==============

/// ST: /named-ins returns JSON with requestID, instances array, and count.
#[tokio::test]
async fn named_ins_returns_json_with_request_id() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/named-ins")
                .body(Body::from("my-request-id"))
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
    assert_eq!(v["requestID"], "my-request-id");
    assert!(v["instances"].is_array());
    assert_eq!(v["count"], 0);
}

/// ST: /queryinstances returns JSON object of instances.
#[tokio::test]
async fn queryinstances_returns_json() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/queryinstances")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
    assert!(v.is_object());
}

/// ST: /query-debug-instances returns JSON with instances and count.
#[tokio::test]
async fn query_debug_instances_returns_json() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/query-debug-instances")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
    assert!(v["instances"].is_array());
    assert_eq!(v["count"], 0);
}

/// ST: /query-tenant-instances requires tenant_id parameter.
#[tokio::test]
async fn query_tenant_instances_requires_tenant_id() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/query-tenant-instances")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
}

/// ST: /query-tenant-instances with valid tenant_id returns JSON.
#[tokio::test]
async fn query_tenant_instances_returns_json() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/query-tenant-instances?tenant_id=t1")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
    assert_eq!(v["tenantID"], "t1");
    assert_eq!(v["count"], 0);
    assert!(v["instances"].is_array());
}

// ============== Snap Manager HTTP endpoints ==============

/// ST: /query-snapshot requires body (snapshot ID).
#[tokio::test]
async fn query_snapshot_requires_body() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/query-snapshot")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
}

/// ST: /query-snapshot with instance id returns stored snapshot JSON.
#[tokio::test]
async fn query_snapshot_returns_json() {
    let state = test_master_state();
    state.snapshots.create_snapshot(InstanceSnapshot {
        instance_id: "snap-001".into(),
        function_name: "my-fn".into(),
        tenant_id: "t1".into(),
        state: "FAILED".into(),
        create_time: 100,
        exit_time: 200,
        exit_reason: "boom".into(),
        resource_cpu: 0,
        resource_memory: 0,
        node_id: "node-a".into(),
        proxy_id: "proxy-a".into(),
        snapshot_id: "snap-001".into(),
    });
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/query-snapshot")
                .body(Body::from("snap-001"))
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
    assert_eq!(v["snapshotID"], "snap-001");
    assert_eq!(v["instanceID"], "snap-001");
    assert_eq!(v["functionName"], "my-fn");
    assert_eq!(v["tenantID"], "t1");
    assert_eq!(v["state"], "FAILED");
    assert_eq!(v["exitReason"], "boom");
}

#[tokio::test]
async fn query_snapshot_missing_returns_404() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/query-snapshot")
                .body(Body::from("no-such-instance"))
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::NOT_FOUND);
}

/// ST: /list-snapshots requires body (function ID).
#[tokio::test]
async fn list_snapshots_requires_function_id() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/list-snapshots")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
}

/// ST: /list-snapshots with valid function name returns matching snapshots.
#[tokio::test]
async fn list_snapshots_returns_matching_snapshots() {
    let state = test_master_state();
    state.instances.upsert_instance(
        "/instances/i-list-1",
        serde_json::json!({
            "id": "i-list-1",
            "function_name": "fn-001",
            "tenant": "tenant-a",
            "state": "EVICTED",
            "created_at_ms": 1,
            "updated_at_ms": 2,
        }),
    );
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/list-snapshots")
                .body(Body::from("fn-001"))
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
    assert!(v.is_array());
    assert_eq!(v.as_array().unwrap().len(), 1);
    assert_eq!(v[0]["instanceID"], "i-list-1");
    assert_eq!(v[0]["functionName"], "fn-001");
}

#[tokio::test]
async fn list_snapshots_json_body_filters_by_tenant() {
    let state = test_master_state();
    state.instances.upsert_instance(
        "/instances/a",
        serde_json::json!({
            "id": "a",
            "function_name": "f-shared",
            "tenant": "t1",
            "state": "EXITED",
            "created_at_ms": 1,
            "updated_at_ms": 1,
        }),
    );
    state.instances.upsert_instance(
        "/instances/b",
        serde_json::json!({
            "id": "b",
            "function_name": "f-shared",
            "tenant": "t2",
            "state": "EXITED",
            "created_at_ms": 1,
            "updated_at_ms": 2,
        }),
    );
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/list-snapshots?tenantID=t1")
                .body(Body::from(r#"{"function":"f-shared"}"#))
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
    let arr = v.as_array().unwrap();
    assert_eq!(arr.len(), 1);
    assert_eq!(arr[0]["instanceID"], "a");
}

// ============== Resource Group HTTP endpoint ==============

/// ST: /rgroup POST returns JSON with requestID and empty groups.
#[tokio::test]
async fn rgroup_post_returns_json() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/rgroup")
                .header("content-type", "application/json")
                .body(Body::from(r#"{"requestID":"rg-1","rGroupName":"pool"}"#))
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
    assert_eq!(v["requestID"], "rg-1");
    assert!(v["groups"].is_array());
    assert_eq!(v["count"], 0);
}

/// ST: /masterinfo returns the C++ JSON contract on both the dedicated HTTP router
/// and the combined global-scheduler listener.
#[tokio::test]
async fn masterinfo_returns_cpp_shape_on_root_and_prefixed_routes() {
    let state = test_master_state();
    state
        .topology
        .register_local(
            "agent-b".into(),
            "10.0.0.2:1000".into(),
            "{}".into(),
            None,
            "{}".into(),
        )
        .await;
    state
        .topology
        .register_local(
            "agent-a".into(),
            "10.0.0.1:1000".into(),
            "{}".into(),
            None,
            "{}".into(),
        )
        .await;
    let app = build_router(state.clone(), None);
    let grpc_app = build_grpc_compat_router(state);

    let http_root = app
        .clone()
        .oneshot(
            Request::builder()
                .uri("/masterinfo")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    let http_prefixed = app
        .clone()
        .oneshot(
            Request::builder()
                .uri("/global-scheduler/masterinfo")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    let grpc_root = grpc_app
        .clone()
        .oneshot(
            Request::builder()
                .uri("/masterinfo")
                .body(Body08::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    let grpc_prefixed = grpc_app
        .oneshot(
            Request::builder()
                .uri("/global-scheduler/masterinfo")
                .body(Body08::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(http_root.status(), StatusCode::OK);
    assert_eq!(http_prefixed.status(), StatusCode::OK);
    assert_eq!(grpc_root.status(), StatusCode::OK);
    assert_eq!(grpc_prefixed.status(), StatusCode::OK);

    let http_root_body = axum::body::to_bytes(http_root.into_body(), usize::MAX)
        .await
        .unwrap();
    let http_prefixed_body = axum::body::to_bytes(http_prefixed.into_body(), usize::MAX)
        .await
        .unwrap();
    let grpc_root_body = axum08::body::to_bytes(grpc_root.into_body(), usize::MAX)
        .await
        .unwrap();
    let grpc_prefixed_body = axum08::body::to_bytes(grpc_prefixed.into_body(), usize::MAX)
        .await
        .unwrap();
    let http_root_v: serde_json::Value = serde_json::from_slice(&http_root_body).unwrap();
    let http_prefixed_v: serde_json::Value = serde_json::from_slice(&http_prefixed_body).unwrap();
    let grpc_root_v: serde_json::Value = serde_json::from_slice(&grpc_root_body).unwrap();
    let grpc_prefixed_v: serde_json::Value = serde_json::from_slice(&grpc_prefixed_body).unwrap();

    assert_eq!(http_root_v, http_prefixed_v);
    assert_eq!(http_root_v, grpc_root_v);
    assert_eq!(http_root_v, grpc_prefixed_v);
    assert_eq!(http_root_v["master_address"], "0.0.0.0:8400");
    assert_eq!(http_root_v["meta_store_address"], "127.0.0.1:2389");
    assert!(http_root_v["schedule_topo"].get("leader").is_none());
    let members = http_root_v["schedule_topo"]["members"]
        .as_array()
        .expect("members array");
    let names: Vec<_> = members
        .iter()
        .filter_map(|row| row["name"].as_str())
        .collect();
    assert_eq!(names, vec!["agent-a", "agent-b"]);
}

#[tokio::test]
async fn masterinfo_rejects_non_json_type() {
    let state = test_master_state();
    let app = build_router(state.clone(), None);
    let grpc_app = build_grpc_compat_router(state);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/masterinfo")
                .header("Type", "protobuf")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::BAD_REQUEST);

    let resp = grpc_app
        .oneshot(
            Request::builder()
                .uri("/global-scheduler/masterinfo")
                .header("Type", "application/json")
                .body(Body08::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn masterinfo_empty_topology_keeps_members_array() {
    let state = test_master_state();
    let grpc_app = build_grpc_compat_router(state);

    let resp = grpc_app
        .oneshot(
            Request::builder()
                .uri("/global-scheduler/masterinfo")
                .body(Body08::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let body = axum08::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
    assert_eq!(v["schedule_topo"]["members"], serde_json::json!([]));
}

// --- Prefixed routes (global-scheduler / instance-manager) ---

#[tokio::test]
async fn global_scheduler_resources_matches_root_resources_json() {
    let state = test_master_state();
    let app = build_router(state.clone(), None);

    let root = app
        .clone()
        .oneshot(
            Request::builder()
                .uri("/resources")
                .header("Type", "json")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    let prefixed = app
        .oneshot(
            Request::builder()
                .uri("/global-scheduler/resources")
                .header("Type", "json")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(root.status(), StatusCode::OK);
    assert_eq!(prefixed.status(), StatusCode::OK);
    let b1 = axum::body::to_bytes(root.into_body(), usize::MAX)
        .await
        .unwrap();
    let b2 = axum::body::to_bytes(prefixed.into_body(), usize::MAX)
        .await
        .unwrap();
    assert_eq!(b1, b2);
}

#[tokio::test]
async fn global_scheduler_scheduling_queue_matches_legacy_path_json() {
    let state = test_master_state();
    let app = build_router(state, None);

    let a = app
        .clone()
        .oneshot(
            Request::builder()
                .uri("/scheduling_queue")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    let b = app
        .oneshot(
            Request::builder()
                .uri("/global-scheduler/scheduling_queue")
                .body(Body::empty())
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

#[tokio::test]
async fn instance_manager_queryinstances_matches_legacy_json() {
    let state = test_master_state();
    let app = build_router(state, None);

    let a = app
        .clone()
        .oneshot(
            Request::builder()
                .uri("/queryinstances")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    let b = app
        .oneshot(
            Request::builder()
                .uri("/instance-manager/queryinstances")
                .body(Body::empty())
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

// --- Protobuf wire validation ---

#[tokio::test]
async fn queryinstances_protobuf_decodes_to_query_instances_info_response() {
    let state = test_master_state();
    state.instances.upsert_instance(
        "/instances/i1",
        serde_json::json!({
            "id": "i1",
            "tenant": "t-a",
            "requestID": "rid-1",
            "function": "f",
        }),
    );
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/queryinstances")
                .header("Type", "protobuf")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let ct = resp
        .headers()
        .get("content-type")
        .and_then(|v| v.to_str().ok())
        .unwrap_or("");
    assert!(ct.contains("protobuf"));
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let decoded = QueryInstancesInfoResponse::decode(body.as_ref()).unwrap();
    assert_eq!(decoded.code, ErrorCode::ErrNone as i32);
    assert_eq!(decoded.instance_infos.len(), 1);
    assert_eq!(decoded.instance_infos[0].instance_id, "i1");
    assert_eq!(decoded.instance_infos[0].request_id, "rid-1");
}

#[tokio::test]
async fn named_ins_protobuf_decodes_instance_infos() {
    let state = test_master_state();
    state.instances.upsert_instance(
        "/instances/n1",
        serde_json::json!({
            "id": "n1",
            "designated_instance_id": "dn1",
            "tenant": "t1",
        }),
    );
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .method("GET")
                .uri("/named-ins")
                .header("content-type", "application/x-protobuf")
                .body(Body::from("req-pb"))
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let decoded = QueryInstancesInfoResponse::decode(body.as_ref()).unwrap();
    assert_eq!(decoded.request_id, "req-pb");
    assert_eq!(decoded.instance_infos.len(), 1);
}

#[tokio::test]
async fn query_debug_instances_protobuf_includes_instance_id() {
    let state = test_master_state();
    state.instances.upsert_instance(
        "/instances/d1",
        serde_json::json!({"id": "d1", "tenant": "tx"}),
    );
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/query-debug-instances")
                .header("Type", "protobuf")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let decoded = QueryInstancesInfoResponse::decode(body.as_ref()).unwrap();
    assert_eq!(decoded.instance_infos.len(), 1);
    assert_eq!(decoded.instance_infos[0].instance_id, "d1");
}

#[tokio::test]
async fn resources_protobuf_decodes_resource_info_with_instances() {
    let state = test_master_state();
    state.instances.upsert_instance(
        "/instances/r1",
        serde_json::json!({"id": "r1", "tenant": "t"}),
    );
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/global-scheduler/resources")
                .header("Type", "protobuf")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let decoded = yr_proto::messages::ResourceInfo::decode(body.as_ref()).unwrap();
    let ru = decoded.resource.expect("resource unit");
    assert!(ru.instances.contains_key("r1"));
    let inst: &InstanceInfo = ru.instances.get("r1").unwrap();
    assert_eq!(inst.instance_id, "r1");
}

// --- Error responses ---

#[tokio::test]
async fn healthy_rejects_wrong_node_id_header() {
    let state = test_master_state();
    let app = build_router(state, None);
    let pid = std::process::id();

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/healthy")
                .header("node-id", "wrong-node")
                .header("pid", pid.to_string())
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    assert_eq!(body.as_ref(), b"error nodeID");
}

#[tokio::test]
async fn healthy_rejects_wrong_pid_header() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/healthy")
                .header("node-id", "test-master")
                .header("pid", "999999999")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn evictagent_empty_agentid_returns_bad_request() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/evictagent")
                .header("content-type", "application/json")
                .body(Body::from(r#"{"agentid":"","timeoutsec":1}"#))
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn evictagent_not_leader_returns_service_unavailable() {
    let state = test_master_state();
    state
        .is_leader
        .store(false, std::sync::atomic::Ordering::SeqCst);
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/evictagent")
                .header("content-type", "application/json")
                .body(Body::from(r#"{"agentid":"any"}"#))
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::SERVICE_UNAVAILABLE);
}

#[tokio::test]
async fn query_tenant_instances_marks_system_tenant_when_cluster_id_matches() {
    let state = test_master_state();
    let app = build_router(state, None);

    let resp = app
        .oneshot(
            Request::builder()
                .uri("/query-tenant-instances?tenant_id=test")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();

    assert_eq!(resp.status(), StatusCode::OK);
    let body = axum::body::to_bytes(resp.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
    assert_eq!(v["isSystemTenant"], true);
}
