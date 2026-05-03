use std::time::Duration;

use base64::Engine;
use base64::engine::general_purpose::URL_SAFE_NO_PAD;
use serde_json::json;
use yr_iam::config::{ElectionMode, IamConfig, IamCredentialType};
use yr_iam::routes::{CodeExchangeBody, LoginBody, TokenExchangeBody};
use yr_iam::token::TokenManager;

fn test_iam_config() -> IamConfig {
    IamConfig {
        host: "127.0.0.1".into(),
        port: 8300,
        etcd_endpoints: vec!["127.0.0.1:2379".into()],
        cluster_id: "test-cluster".into(),
        enable_iam: true,
        token_ttl_default: Duration::from_secs(3600),
        election_mode: ElectionMode::Standalone,
        iam_credential_type: IamCredentialType::Token,
        etcd_table_prefix: String::new(),
        iam_signing_secret: "unit-test-secret".into(),
        instance_id: "test-node".into(),
    }
}

#[test]
fn mint_token_has_cxx_jwt_shape() {
    let cfg = test_iam_config();
    let token = TokenManager::mint(&cfg, "t1", "admin", Duration::from_secs(600)).unwrap();
    let parts: Vec<&str> = token.split('.').collect();
    assert_eq!(parts.len(), 3);
    let header = String::from_utf8(URL_SAFE_NO_PAD.decode(parts[0]).unwrap()).unwrap();
    let payload = String::from_utf8(URL_SAFE_NO_PAD.decode(parts[1]).unwrap()).unwrap();
    let header_json: serde_json::Value = serde_json::from_str(&header).unwrap();
    let payload_json: serde_json::Value = serde_json::from_str(&payload).unwrap();
    assert_eq!(header_json["alg"], json!("HS256"));
    assert_eq!(header_json["typ"], json!("JWT"));
    assert_eq!(payload_json["sub"], json!("t1"));
    assert_eq!(payload_json["role"], json!("admin"));
    assert!(payload_json["exp"].as_u64().unwrap() > 0);
    assert!(!parts[2].is_empty());
}

#[test]
fn mint_tokens_differ_for_different_tenants() {
    let cfg = test_iam_config();
    let a = TokenManager::mint(&cfg, "tenant-a", "r", Duration::from_secs(100)).unwrap();
    let b = TokenManager::mint(&cfg, "tenant-b", "r", Duration::from_secs(100)).unwrap();
    assert_ne!(a, b);
}

#[test]
fn parse_and_verify_round_trips_minted_token() {
    let cfg = test_iam_config();
    let token = TokenManager::mint(&cfg, "tenant-x", "role1", Duration::from_secs(800)).unwrap();
    let claims = TokenManager::parse_and_verify(&cfg, &token).unwrap();
    assert_eq!(claims.tenant_id, "tenant-x");
    assert_eq!(claims.role, "role1");
    assert!(claims.exp > 0);
}

#[test]
fn parse_and_verify_rejects_tampered_signature() {
    let cfg = test_iam_config();
    let mut token = TokenManager::mint(&cfg, "t", "r", Duration::from_secs(100)).unwrap();
    let last = token.pop().unwrap();
    let tamper = if last == 'a' { 'b' } else { 'a' };
    token.push(tamper);
    let err = TokenManager::parse_and_verify(&cfg, &token).unwrap_err();
    assert!(err.to_string().contains("signature") || err.to_string().contains("invalid"));
}

#[test]
fn parse_and_verify_rejects_malformed_token() {
    let cfg = test_iam_config();
    let err = TokenManager::parse_and_verify(&cfg, "not-a-jwt-shape").unwrap_err();
    assert!(err.to_string().contains("malformed"));
}

#[tokio::test]
async fn parse_and_verify_rejects_expired_token() {
    let cfg = test_iam_config();
    let token = TokenManager::mint(&cfg, "exp", "r", Duration::from_secs(1)).unwrap();
    tokio::time::sleep(Duration::from_secs(5)).await;
    let err = TokenManager::parse_and_verify(&cfg, &token).unwrap_err();
    assert!(err.to_string().contains("expired"));
}

#[test]
fn token_exchange_body_deserializes_fields() {
    let v = json!({
        "id_token": "ext-tenant",
        "expires_in": 300
    });
    let b: TokenExchangeBody = serde_json::from_value(v).unwrap();
    assert_eq!(b.id_token, "ext-tenant");
    assert_eq!(b.expires_in, Some(300));
}

#[test]
fn code_exchange_body_deserializes_fields() {
    let v = json!({
        "code": "oauth-code",
        "redirect_uri": "https://app/cb",
        "expires_in": 120
    });
    let b: CodeExchangeBody = serde_json::from_value(v).unwrap();
    assert_eq!(b.code, "oauth-code");
    assert_eq!(b.redirect_uri, "https://app/cb");
    assert_eq!(b.expires_in, Some(120));
}

#[test]
fn login_body_deserializes_username_password() {
    let v = json!({ "username": "alice", "password": "secret" });
    let b: LoginBody = serde_json::from_value(v).unwrap();
    assert_eq!(b.username, "alice");
    assert_eq!(b.password, "secret");
}

#[test]
fn etcd_watch_prefix_new_tokens_is_stable() {
    let cfg = test_iam_config();
    let p = TokenManager::etcd_watch_prefix_new_tokens(&cfg);
    assert!(p.contains("test-cluster"));
    assert!(p.ends_with('/'));
}

#[tokio::test]
#[ignore = "requires etcd at 127.0.0.1:2379"]
async fn store_issue_and_verify_roundtrip() {
    let mut ms = yr_metastore_client::MetaStoreClient::connect_direct("127.0.0.1:2379", "")
        .await
        .expect("etcd");
    let cfg = test_iam_config();
    let tok = TokenManager::issue(
        &mut ms,
        &cfg,
        "store-tenant",
        "editor",
        Duration::from_secs(600),
    )
    .await
    .unwrap();
    let claims = TokenManager::verify(&mut ms, &cfg, &tok).await.unwrap();
    assert_eq!(claims.tenant_id, "store-tenant");
    assert_eq!(claims.role, "editor");
}

#[tokio::test]
#[ignore = "requires etcd at 127.0.0.1:2379"]
async fn store_verify_rejects_token_not_in_etcd() {
    let mut ms = yr_metastore_client::MetaStoreClient::connect_direct("127.0.0.1:2379", "")
        .await
        .expect("etcd");
    let cfg = test_iam_config();
    let tok = TokenManager::mint(&cfg, "ghost", "r", Duration::from_secs(600)).unwrap();
    let err = TokenManager::verify(&mut ms, &cfg, &tok).await.unwrap_err();
    assert!(err.to_string().contains("not present"));
}

#[tokio::test]
#[ignore = "requires etcd at 127.0.0.1:2379"]
async fn store_abandon_removes_tokens() {
    let mut ms = yr_metastore_client::MetaStoreClient::connect_direct("127.0.0.1:2379", "")
        .await
        .expect("etcd");
    let cfg = test_iam_config();
    let tok = TokenManager::issue(&mut ms, &cfg, "abandon-me", "r", Duration::from_secs(600))
        .await
        .unwrap();
    TokenManager::verify(&mut ms, &cfg, &tok).await.unwrap();
    TokenManager::abandon(&mut ms, &cfg, "abandon-me").await.unwrap();
    let err = TokenManager::verify(&mut ms, &cfg, &tok).await.unwrap_err();
    assert!(err.to_string().contains("not present"));
}

#[tokio::test]
#[ignore = "requires etcd at 127.0.0.1:2379"]
async fn store_tenants_are_isolated() {
    let mut ms = yr_metastore_client::MetaStoreClient::connect_direct("127.0.0.1:2379", "")
        .await
        .expect("etcd");
    let cfg = test_iam_config();
    let t1 = TokenManager::issue(&mut ms, &cfg, "ten-a", "r", Duration::from_secs(600))
        .await
        .unwrap();
    let t2 = TokenManager::issue(&mut ms, &cfg, "ten-b", "r", Duration::from_secs(600))
        .await
        .unwrap();
    TokenManager::verify(&mut ms, &cfg, &t1).await.unwrap();
    TokenManager::verify(&mut ms, &cfg, &t2).await.unwrap();
    assert_ne!(t1, t2);
}

#[tokio::test]
#[ignore = "requires etcd at 127.0.0.1:2379"]
async fn store_rotation_tick_runs_without_panic() {
    let mut ms = yr_metastore_client::MetaStoreClient::connect_direct("127.0.0.1:2379", "")
        .await
        .expect("etcd");
    let cfg = test_iam_config();
    TokenManager::issue(&mut ms, &cfg, "rot-tenant", "r", Duration::from_secs(600))
        .await
        .unwrap();
    TokenManager::rotation_tick(&mut ms, &cfg).await;
}
