//! Scenario 12 — IAM E2E: tokens, AK/SK, users/tenants, rejection paths (embedded MetaStore).

use std::collections::BTreeMap;
use std::net::SocketAddr;
use std::sync::Arc;
use std::time::Duration;

use axum::body::Body;
use axum::http::{Request, StatusCode};
use http_body_util::BodyExt;
use serde_json::{json, Value};
use tokio::net::TcpListener;
use tower::ServiceExt;
use yr_common::aksk::{sign_http_request, verify_http_request, AkskKey, SignRequest};
use yr_iam::config::{ElectionMode, IamConfig, IamCredentialType};
use yr_iam::routes::build_router;
use yr_iam::state::AppState;
use yr_metastore_client::MetaStoreClient;
use yr_metastore_server::{MetaStoreServer, MetaStoreServerConfig};

fn e2e_iam_config() -> IamConfig {
    IamConfig {
        host: "127.0.0.1".into(),
        port: 8300,
        etcd_endpoints: vec![],
        cluster_id: "e2e-auth-cluster".into(),
        enable_iam: true,
        token_ttl_default: Duration::from_secs(3600),
        election_mode: ElectionMode::Standalone,
        iam_credential_type: IamCredentialType::Both,
        etcd_table_prefix: String::new(),
        iam_signing_secret: "e2e-auth-hmac-secret".into(),
        instance_id: "iam-e2e-1".into(),
    }
}

async fn spawn_embedded_metastore() -> (SocketAddr, MetaStoreClient, tokio::task::JoinHandle<()>) {
    let listener = TcpListener::bind("127.0.0.1:0").await.expect("bind meta");
    let addr = listener.local_addr().expect("addr");
    let mut cfg = MetaStoreServerConfig::default();
    cfg.listen_addr = addr.to_string();
    cfg.etcd_endpoints = vec![];
    let server = MetaStoreServer::new(cfg).await.expect("meta new");
    let jh = tokio::spawn(async move {
        let _ = server.serve(listener).await;
    });
    tokio::time::sleep(Duration::from_millis(100)).await;
    let ep = format!("http://{}", addr);
    let ms = MetaStoreClient::connect_direct(&ep, "")
        .await
        .expect("meta client");
    (addr, ms, jh)
}

async fn read_json(res: axum::response::Response) -> Value {
    let body = res.into_body().collect().await.unwrap().to_bytes();
    serde_json::from_slice(&body).unwrap_or(Value::Null)
}

fn header_str<'a>(res: &'a axum::response::Response, name: &str) -> &'a str {
    res.headers()
        .get(name)
        .unwrap_or_else(|| panic!("missing header {name}"))
        .to_str()
        .unwrap()
}

#[tokio::test]
async fn e2e_auth_token_issue_verify_abandon_flow() {
    let (_addr, ms, _jh) = spawn_embedded_metastore().await;
    let cfg = e2e_iam_config();
    let router = build_router(Arc::new(AppState::new(cfg, Some(ms))));

    let issue = router
        .clone()
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/v1/tokens")
                .header("content-type", "application/json")
                .body(Body::from(
                    json!({ "tenant_id": "tenant-e2e-a", "role": "editor" }).to_string(),
                ))
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(issue.status(), StatusCode::OK);
    let v = read_json(issue).await;
    let token = v["token"].as_str().expect("token field");
    assert!(!token.is_empty());

    let verify = router
        .clone()
        .oneshot(
            Request::builder()
                .uri(format!("/v1/tokens?token={token}"))
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(verify.status(), StatusCode::OK);
    let c = read_json(verify).await;
    assert_eq!(c["tenant_id"], json!("tenant-e2e-a"));
    assert_eq!(c["role"], json!("editor"));

    let abandon = router
        .clone()
        .oneshot(
            Request::builder()
                .method("DELETE")
                .uri("/v1/tokens?tenant_id=tenant-e2e-a")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(abandon.status(), StatusCode::OK);

    let verify2 = router
        .oneshot(
            Request::builder()
                .uri(format!("/v1/tokens?token={token}"))
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(verify2.status(), StatusCode::UNAUTHORIZED);
}

#[tokio::test]
async fn e2e_auth_aksk_create_resolve_sign_verify_roundtrip() {
    let (_addr, ms, _jh) = spawn_embedded_metastore().await;
    let cfg = e2e_iam_config();
    let router = build_router(Arc::new(AppState::new(cfg.clone(), Some(ms))));

    let post = router
        .clone()
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/v1/aksk")
                .header("content-type", "application/json")
                .body(Body::from(
                    json!({ "tenant_id": "ak-tenant", "role": "ops", "ttl_secs": 3600 }).to_string(),
                ))
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(post.status(), StatusCode::OK);
    let body = read_json(post).await;
    let ak = body["access_key"].as_str().unwrap();
    let sk_hex = body["secret_key"].as_str().unwrap();
    let sk = hex::decode(sk_hex).expect("secret hex");

    let get = router
        .oneshot(
            Request::builder()
                .uri(format!("/v1/aksk?access_key={ak}"))
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(get.status(), StatusCode::OK);
    let g = read_json(get).await;
    assert_eq!(g["tenant_id"], json!("ak-tenant"));

    let headers = BTreeMap::from([("host".into(), "iam.test".into())]);
    let req = SignRequest::new("GET", "/v1/tenant/quota", None, headers, "");
    let auth = sign_http_request(
        &req,
        &AkskKey {
            access_key_id: ak,
            secret_key: &sk,
        },
    );
    let mut signed_req = req.clone();
    for (k, v) in &auth {
        signed_req.headers.insert(k.clone(), v.clone());
    }
    assert!(verify_http_request(
        &signed_req,
        &AkskKey {
            access_key_id: ak,
            secret_key: &sk,
        },
    ));
}

#[tokio::test]
async fn e2e_auth_user_and_tenant_crud() {
    let (_addr, ms, _jh) = spawn_embedded_metastore().await;
    let cfg = e2e_iam_config();
    let router = build_router(Arc::new(AppState::new(cfg, Some(ms))));

    let t_post = router
        .clone()
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/v1/tenants")
                .header("content-type", "application/json")
                .body(Body::from(
                    json!({ "tenant_id": "corp-e2e", "display_name": "Corp" }).to_string(),
                ))
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(t_post.status(), StatusCode::OK);

    let t_list = router
        .clone()
        .oneshot(
            Request::builder()
                .uri("/v1/tenants")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(t_list.status(), StatusCode::OK);
    let tl = read_json(t_list).await;
    let tenants = tl["tenants"].as_array().unwrap();
    assert!(tenants.iter().any(|x| x["tenant_id"] == "corp-e2e"));

    let u_post = router
        .clone()
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/v1/users")
                .header("content-type", "application/json")
                .body(Body::from(
                    json!({
                        "user_id": "alice",
                        "tenant_id": "corp-e2e",
                        "roles": ["admin"]
                    })
                    .to_string(),
                ))
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(u_post.status(), StatusCode::OK);

    let u_get = router
        .oneshot(
            Request::builder()
                .uri("/v1/users?id=alice")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(u_get.status(), StatusCode::OK);
    let u = read_json(u_get).await;
    assert_eq!(u["user_id"], json!("alice"));
    assert_eq!(u["tenant_id"], json!("corp-e2e"));
}

#[tokio::test]
async fn e2e_auth_rejects_invalid_token_on_token_auth_route() {
    let (_addr, ms, _jh) = spawn_embedded_metastore().await;
    let cfg = e2e_iam_config();
    let router = build_router(Arc::new(AppState::new(cfg, Some(ms))));

    let res = router
        .oneshot(
            Request::builder()
                .uri("/v1/token/auth")
                .header("x-auth", "not.a.valid.token")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(res.status(), StatusCode::FORBIDDEN);
}

#[tokio::test]
async fn e2e_auth_follower_rejects_token_issue() {
    let (_addr, ms, _jh) = spawn_embedded_metastore().await;
    let cfg = e2e_iam_config();
    let state = Arc::new(AppState::new(cfg, Some(ms)));
    state.set_leader(false);
    let router = build_router(state);

    let res = router
        .oneshot(
            Request::builder()
                .method("POST")
                .uri("/v1/tokens")
                .header("content-type", "application/json")
                .body(Body::from(json!({ "tenant_id": "x" }).to_string()))
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(res.status(), StatusCode::SERVICE_UNAVAILABLE);
}

#[tokio::test]
async fn legacy_prefixed_token_routes_match_header_contract() {
    let (_addr, ms, _jh) = spawn_embedded_metastore().await;
    let cfg = e2e_iam_config();
    let router = build_router(Arc::new(AppState::new(cfg, Some(ms))));

    let require = router
        .clone()
        .oneshot(
            Request::builder()
                .uri("/iam-server/v1/token/require")
                .header("x-tenant-id", "legacy-tenant")
                .header("x-role", "editor")
                .header("x-ttl", "600")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(require.status(), StatusCode::OK);
    let token = header_str(&require, "x-auth").to_string();
    assert!(!token.is_empty());
    assert!(!header_str(&require, "x-salt").is_empty());
    assert!(header_str(&require, "x-expired-time-span")
        .parse::<u64>()
        .unwrap()
        > 0);

    let verify = router
        .clone()
        .oneshot(
            Request::builder()
                .uri("/iam-server/v1/token/auth")
                .header("x-auth", &token)
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(verify.status(), StatusCode::OK);
    assert_eq!(header_str(&verify, "x-tenant-id"), "legacy-tenant");
    assert_eq!(header_str(&verify, "x-role"), "editor");
    assert!(header_str(&verify, "x-expired-time-span")
        .parse::<u64>()
        .unwrap()
        > 0);

    let abandon = router
        .clone()
        .oneshot(
            Request::builder()
                .uri("/iam-server/v1/token/abandon")
                .header("x-tenant-id", "legacy-tenant")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(abandon.status(), StatusCode::OK);
}

#[tokio::test]
async fn legacy_prefixed_token_auth_returns_forbidden_for_invalid_token() {
    let (_addr, ms, _jh) = spawn_embedded_metastore().await;
    let cfg = e2e_iam_config();
    let router = build_router(Arc::new(AppState::new(cfg, Some(ms))));

    let res = router
        .oneshot(
            Request::builder()
                .uri("/iam-server/v1/token/auth")
                .header("x-auth", "not.a.valid.token")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(res.status(), StatusCode::FORBIDDEN);
}

#[tokio::test]
async fn legacy_prefixed_credential_require_route_exists() {
    let (_addr, ms, _jh) = spawn_embedded_metastore().await;
    let cfg = e2e_iam_config();
    let router = build_router(Arc::new(AppState::new(cfg, Some(ms))));

    let res = router
        .oneshot(
            Request::builder()
                .uri("/iam-server/v1/credential/require")
                .header("x-tenant-id", "legacy-credential-tenant")
                .body(Body::empty())
                .unwrap(),
        )
        .await
        .unwrap();
    assert_eq!(res.status(), StatusCode::OK);
}
