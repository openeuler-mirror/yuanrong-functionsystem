//! HTTP admin surface (`/healthy`, `/resources`).

use axum::body::Body;
use axum::http::{Request, StatusCode};
use tower::ServiceExt;
use yr_proxy::http_api::{router, HttpState};
use yr_proxy::resource_view::{ResourceVector, ResourceView};

fn test_state(node_id: &str) -> HttpState {
    let rv = ResourceView::new(ResourceVector {
        cpu: 4.0,
        memory: 8.0,
        npu: 0.0,
    });
    HttpState {
        resource_view: rv,
        node_id: node_id.into(),
    }
}

#[tokio::test]
async fn healthy_ok_matching_node_and_pid() {
    let app = router(test_state("node-99"));
    let pid = std::process::id().to_string();
    let req = Request::builder()
        .uri("/healthy")
        .header("node-id", "node-99")
        .header("pid", pid)
        .body(Body::empty())
        .unwrap();
    let res = app.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::OK);
}

#[tokio::test]
async fn healthy_bad_node_id_returns_400() {
    let app = router(test_state("expected-node"));
    let pid = std::process::id().to_string();
    let req = Request::builder()
        .uri("/healthy")
        .header("node-id", "wrong-node")
        .header("pid", pid)
        .body(Body::empty())
        .unwrap();
    let res = app.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn healthy_bad_pid_returns_400() {
    let app = router(test_state("n1"));
    let req = Request::builder()
        .uri("/healthy")
        .header("node-id", "n1")
        .header("pid", "999999999")
        .body(Body::empty())
        .unwrap();
    let res = app.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn healthy_missing_node_header_returns_400() {
    let app = router(test_state("n1"));
    let pid = std::process::id().to_string();
    let req = Request::builder()
        .uri("/healthy")
        .header("pid", pid)
        .body(Body::empty())
        .unwrap();
    let res = app.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn healthy_missing_pid_header_returns_400() {
    let app = router(test_state("n1"));
    let req = Request::builder()
        .uri("/healthy")
        .header("node-id", "n1")
        .body(Body::empty())
        .unwrap();
    let res = app.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn healthy_trims_header_whitespace() {
    let app = router(test_state("abc"));
    let pid = std::process::id().to_string();
    let req = Request::builder()
        .uri("/healthy")
        .header("node-id", "  abc  ")
        .header("pid", format!("  {pid}  "))
        .body(Body::empty())
        .unwrap();
    let res = app.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::OK);
}

#[tokio::test]
async fn healthy_rejects_empty_node_id_value() {
    let app = router(test_state("real"));
    let pid = std::process::id().to_string();
    let req = Request::builder()
        .uri("/healthy")
        .header("node-id", "   ")
        .header("pid", pid)
        .body(Body::empty())
        .unwrap();
    let res = app.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn healthy_rejects_non_numeric_pid() {
    let app = router(test_state("n1"));
    let req = Request::builder()
        .uri("/healthy")
        .header("node-id", "n1")
        .header("pid", "not-a-number")
        .body(Body::empty())
        .unwrap();
    let res = app.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn resources_returns_json_snapshot() {
    let app = router(test_state("n1"));
    let req = Request::builder()
        .uri("/resources")
        .body(Body::empty())
        .unwrap();
    let res = app.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::OK);
    let body = axum::body::to_bytes(res.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&body).expect("json");
    assert!(v.get("capacity").is_some());
    assert!(v.get("used").is_some());
    assert!(v.get("pending").is_some());
}

#[tokio::test]
async fn resources_capacity_matches_view() {
    let rv = ResourceView::new(ResourceVector {
        cpu: 2.5,
        memory: 3.0,
        npu: 1.0,
    });
    let app = router(HttpState {
        resource_view: rv,
        node_id: "x".into(),
    });
    let req = Request::builder()
        .uri("/resources")
        .body(Body::empty())
        .unwrap();
    let res = app.oneshot(req).await.unwrap();
    let body = axum::body::to_bytes(res.into_body(), usize::MAX)
        .await
        .unwrap();
    let v: serde_json::Value = serde_json::from_slice(&body).unwrap();
    assert_eq!(v["capacity"]["cpu"], 2.5);
}

#[tokio::test]
async fn healthy_accepts_header_name_case_insensitive() {
    let app = router(test_state("nid"));
    let pid = std::process::id().to_string();
    let req = Request::builder()
        .uri("/healthy")
        .header("Node-ID", "nid")
        .header("pid", pid)
        .body(Body::empty())
        .unwrap();
    let res = app.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::OK);
}

#[tokio::test]
async fn get_unknown_route_404() {
    let app = router(test_state("n"));
    let req = Request::builder().uri("/nope").body(Body::empty()).unwrap();
    let res = app.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::NOT_FOUND);
}
