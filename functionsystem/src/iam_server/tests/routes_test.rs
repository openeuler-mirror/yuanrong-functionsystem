use std::sync::Arc;
use std::time::Duration;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use http_body_util::BodyExt;
use serde_json::{Value, json};
use tower::ServiceExt;
use yr_iam::config::{ElectionMode, IamConfig, IamCredentialType};
use yr_iam::routes::build_router;
use yr_iam::state::AppState;

fn base_config() -> IamConfig {
    IamConfig {
        host: "127.0.0.1".into(),
        port: 8300,
        etcd_endpoints: vec!["127.0.0.1:2379".into()],
        cluster_id: "route-test".into(),
        enable_iam: true,
        token_ttl_default: Duration::from_secs(3600),
        election_mode: ElectionMode::Standalone,
        iam_credential_type: IamCredentialType::Token,
        etcd_table_prefix: String::new(),
        iam_signing_secret: "routes-secret".into(),
        instance_id: "node-1".into(),
    }
}

fn app(cfg: IamConfig, metastore: Option<yr_metastore_client::MetaStoreClient>) -> axum::Router {
    build_router(Arc::new(AppState::new(cfg, metastore)))
}

async fn read_json(res: axum::response::Response) -> Value {
    let body = res.into_body().collect().await.unwrap().to_bytes();
    serde_json::from_slice(&body).unwrap_or(Value::Null)
}

async fn read_text(res: axum::response::Response) -> String {
    let body = res.into_body().collect().await.unwrap().to_bytes();
    String::from_utf8_lossy(&body).into_owned()
}

#[tokio::test]
async fn get_health_path_alias_matches_healthy() {
    let cfg = base_config();
    let router = app(cfg, None);
    let pid = std::process::id().to_string();
    let req = Request::builder()
        .uri("/health")
        .header("node-id", "node-1")
        .header("pid", &pid)
        .body(Body::empty())
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::OK);
}

#[tokio::test]
async fn get_healthy_succeeds_with_matching_node_and_pid_headers() {
    let cfg = base_config();
    let router = app(cfg, None);
    let pid = std::process::id().to_string();
    let req = Request::builder()
        .uri("/healthy")
        .header("node-id", "node-1")
        .header("pid", &pid)
        .body(Body::empty())
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::OK);
    assert_eq!(read_text(res).await, "");
}

#[tokio::test]
async fn get_healthy_fails_on_wrong_node_id() {
    let cfg = base_config();
    let router = app(cfg, None);
    let pid = std::process::id().to_string();
    let req = Request::builder()
        .uri("/healthy")
        .header("node-id", "other")
        .header("pid", &pid)
        .body(Body::empty())
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
    assert_eq!(read_text(res).await, "error nodeID");
}

#[tokio::test]
async fn get_healthy_fails_on_wrong_pid() {
    let cfg = base_config();
    let router = app(cfg, None);
    let req = Request::builder()
        .uri("/healthy")
        .header("node-id", "node-1")
        .header("pid", "999999999")
        .body(Body::empty())
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
    assert_eq!(read_text(res).await, "error PID");
}

#[tokio::test]
async fn get_v1_auth_url_returns_placeholder_url() {
    let cfg = base_config();
    let router = app(cfg, None);
    let req = Request::builder()
        .uri("/v1/auth/url?type=oauth&redirect_uri=https%3A%2F%2Fcb&state=xyz")
        .body(Body::empty())
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::OK);
    let v = read_json(res).await;
    assert_eq!(v["ok"], json!(true));
    let url = v["url"].as_str().unwrap();
    assert!(url.contains("type=oauth"));
    assert!(url.contains("redirect_uri=https://cb"));
    assert!(url.contains("state=xyz"));
}

#[tokio::test]
async fn get_v1_tenant_quota_requires_tenant_id() {
    let cfg = base_config();
    let router = app(cfg, None);
    let req = Request::builder()
        .uri("/v1/tenant/quota")
        .body(Body::empty())
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
    let v = read_json(res).await;
    assert_eq!(v["ok"], json!(false));
}

#[tokio::test]
async fn get_v1_tenant_quota_returns_default_quotas() {
    let cfg = base_config();
    let router = app(cfg, None);
    let req = Request::builder()
        .uri("/v1/tenant/quota?tenant_id=acme")
        .body(Body::empty())
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::OK);
    let v = read_json(res).await;
    assert_eq!(v["ok"], json!(true));
    assert_eq!(v["tenant_id"], json!("acme"));
    assert_eq!(v["cpu_quota"], json!(-1));
    assert_eq!(v["mem_quota"], json!(-1));
}

#[tokio::test]
async fn post_v1_token_exchange_requires_metastore_when_iam_enabled() {
    let cfg = base_config();
    let router = app(cfg, None);
    let body = json!({ "id_token": "ext-1" });
    let req = Request::builder()
        .method("POST")
        .uri("/v1/token/exchange")
        .header("content-type", "application/json")
        .body(Body::from(serde_json::to_vec(&body).unwrap()))
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::SERVICE_UNAVAILABLE);
    let v = read_json(res).await;
    assert!(v["message"].as_str().unwrap().contains("metastore"));
}

#[tokio::test]
async fn post_v1_token_exchange_rejects_empty_id_token() {
    let cfg = base_config();
    let router = app(cfg, None);
    let body = json!({ "id_token": "" });
    let req = Request::builder()
        .method("POST")
        .uri("/v1/token/exchange")
        .header("content-type", "application/json")
        .body(Body::from(serde_json::to_vec(&body).unwrap()))
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn post_v1_token_login_requires_metastore_when_iam_enabled() {
    let cfg = base_config();
    let router = app(cfg, None);
    let body = json!({ "username": "u", "password": "p" });
    let req = Request::builder()
        .method("POST")
        .uri("/v1/token/login")
        .header("content-type", "application/json")
        .body(Body::from(serde_json::to_vec(&body).unwrap()))
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::SERVICE_UNAVAILABLE);
}

#[tokio::test]
async fn post_v1_token_login_rejects_missing_password() {
    let cfg = base_config();
    let router = app(cfg, None);
    let body = json!({ "username": "u", "password": "" });
    let req = Request::builder()
        .method("POST")
        .uri("/v1/token/login")
        .header("content-type", "application/json")
        .body(Body::from(serde_json::to_vec(&body).unwrap()))
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn post_v1_token_code_exchange_validates_required_fields() {
    let cfg = base_config();
    let router = app(cfg.clone(), None);
    let body = json!({ "code": "", "redirect_uri": "https://x" });
    let req = Request::builder()
        .method("POST")
        .uri("/v1/token/code-exchange")
        .header("content-type", "application/json")
        .body(Body::from(serde_json::to_vec(&body).unwrap()))
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);

    let router = app(cfg, None);
    let body = json!({ "code": "c", "redirect_uri": "" });
    let req = Request::builder()
        .method("POST")
        .uri("/v1/token/code-exchange")
        .header("content-type", "application/json")
        .body(Body::from(serde_json::to_vec(&body).unwrap()))
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn iam_disabled_returns_service_unavailable_on_exchange() {
    let mut cfg = base_config();
    cfg.enable_iam = false;
    let router = app(cfg, None);
    let body = json!({ "id_token": "x" });
    let req = Request::builder()
        .method("POST")
        .uri("/v1/token/exchange")
        .header("content-type", "application/json")
        .body(Body::from(serde_json::to_vec(&body).unwrap()))
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::SERVICE_UNAVAILABLE);
}

#[tokio::test]
async fn get_v1_token_require_returns_503_without_metastore() {
    let cfg = base_config();
    let router = app(cfg, None);
    let req = Request::builder()
        .uri("/v1/token/require")
        .header("x-tenant-id", "t1")
        .body(Body::empty())
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::SERVICE_UNAVAILABLE);
}

#[tokio::test]
async fn get_v1_token_require_missing_tenant_is_bad_request() {
    let cfg = base_config();
    let router = app(cfg, None);
    let req = Request::builder()
        .uri("/v1/token/require")
        .body(Body::empty())
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn get_v1_token_auth_missing_header_is_bad_request() {
    let cfg = base_config();
    let router = app(cfg, None);
    let req = Request::builder()
        .uri("/v1/token/auth")
        .body(Body::empty())
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn get_v1_token_auth_requires_metastore() {
    let cfg = base_config();
    let router = app(cfg, None);
    let req = Request::builder()
        .uri("/v1/token/auth")
        .header("x-auth", "dummy.token")
        .body(Body::empty())
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::SERVICE_UNAVAILABLE);
}

#[tokio::test]
async fn get_v1_token_abandon_missing_tenant_is_bad_request() {
    let cfg = base_config();
    let router = app(cfg, None);
    let req = Request::builder()
        .uri("/v1/token/abandon")
        .body(Body::empty())
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn get_v1_token_abandon_requires_metastore() {
    let cfg = base_config();
    let router = app(cfg, None);
    let req = Request::builder()
        .uri("/v1/token/abandon")
        .header("x-tenant-id", "t1")
        .body(Body::empty())
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::SERVICE_UNAVAILABLE);
}

#[tokio::test]
async fn get_v1_token_refresh_missing_x_auth_is_bad_request() {
    let cfg = base_config();
    let router = app(cfg, None);
    let req = Request::builder()
        .uri("/v1/token/refresh")
        .body(Body::empty())
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
}

#[tokio::test]
async fn credential_routes_disabled_when_token_only_mode() {
    let cfg = base_config();
    let router = app(cfg, None);
    let req = Request::builder()
        .uri("/v1/credential/require")
        .header("x-tenant-id", "t1")
        .body(Body::empty())
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::NOT_FOUND);
}

#[tokio::test]
async fn get_v1_credential_auth_missing_x_auth_is_bad_request() {
    let mut cfg = base_config();
    cfg.iam_credential_type = IamCredentialType::Both;
    let router = app(cfg, None);
    let req = Request::builder()
        .uri("/v1/credential/auth")
        .body(Body::empty())
        .unwrap();
    let res = router.oneshot(req).await.unwrap();
    assert_eq!(res.status(), StatusCode::BAD_REQUEST);
}
