use std::sync::Arc;
use std::time::Duration;

use axum::Json;
use axum::Router;
use axum::routing::{get, post};
use axum::extract::{Query, State};
use axum::http::{HeaderMap, StatusCode};
use axum::response::{IntoResponse, Response};
use serde::Deserialize;
use serde_json::{Value, json};
use tokio::sync::MutexGuard;
use yr_common::error::YrError;
use yr_metastore_client::MetaStoreClient;

use crate::aksk::AkskManager;
use crate::config::IamCredentialType;
use crate::state::AppState;
use crate::token::TokenManager;
use crate::user_manager::{TenantRecord, UserManager, UserRecord};

fn ok_json(body: Value) -> Response {
    let mut m = serde_json::Map::new();
    m.insert("ok".into(), json!(true));
    if let Value::Object(map) = body {
        for (k, v) in map {
            m.insert(k, v);
        }
    } else {
        m.insert("data".into(), body);
    }
    (StatusCode::OK, Json(Value::Object(m))).into_response()
}

fn err_json(status: StatusCode, msg: impl Into<String>) -> Response {
    (
        status,
        Json(json!({
            "ok": false,
            "message": msg.into(),
        })),
    )
        .into_response()
}

fn map_yr_err(e: YrError) -> Response {
    let status = match &e {
        YrError::Internal(s) if s.contains("expired") || s.contains("not present") => {
            StatusCode::UNAUTHORIZED
        }
        YrError::Internal(s)
            if s.contains("malformed")
                || s.contains("invalid token")
                || s.contains("unknown access key") =>
        {
            StatusCode::UNAUTHORIZED
        }
        YrError::Serialization(_) => StatusCode::BAD_REQUEST,
        _ => StatusCode::INTERNAL_SERVER_ERROR,
    };
    err_json(status, e.to_string())
}

async fn lock_meta(
    state: &AppState,
) -> std::result::Result<MutexGuard<'_, MetaStoreClient>, Response> {
    match &state.metastore {
        Some(m) => Ok(m.lock().await),
        None => Err(err_json(
            StatusCode::SERVICE_UNAVAILABLE,
            "metastore not configured",
        )),
    }
}

fn header<'a>(headers: &'a HeaderMap, name: &str) -> Option<&'a str> {
    headers
        .get(name)
        .and_then(|v| v.to_str().ok())
        .map(str::trim)
        .filter(|s| !s.is_empty())
}

pub async fn healthy(State(state): State<Arc<AppState>>, headers: HeaderMap) -> impl IntoResponse {
    let node_hdr = header(&headers, "node-id");
    let pid_hdr = header(&headers, "pid");

    if node_hdr.is_none() && pid_hdr.is_none() {
        return (StatusCode::OK, "").into_response();
    }

    let expected_node = state.config.instance_id.as_str();
    let node_ok = node_hdr.is_some_and(|v| v == expected_node);
    if !node_ok {
        return (StatusCode::BAD_REQUEST, "error nodeID").into_response();
    }
    let pid = std::process::id();
    let pid_ok = pid_hdr
        .and_then(|s| s.parse::<u32>().ok())
        == Some(pid);
    if !pid_ok {
        return (StatusCode::BAD_REQUEST, "error PID").into_response();
    }
    (StatusCode::OK, "").into_response()
}

pub async fn token_auth(State(state): State<Arc<AppState>>, headers: HeaderMap) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !matches!(
        state.config.iam_credential_type,
        IamCredentialType::Token | IamCredentialType::Both
    ) {
        return err_json(StatusCode::NOT_FOUND, "token credential mode disabled");
    }
    let Some(token) = header(&headers, "x-auth") else {
        return err_json(StatusCode::BAD_REQUEST, "missing X-Auth");
    };
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    match state
        .token_store
        .verify(&mut ms, &state.config, token)
        .await
    {
        Ok(claims) => ok_json(json!({
            "tenant_id": claims.tenant_id,
            "role": claims.role,
            "iat": claims.iat,
            "exp": claims.exp,
        })),
        Err(e) => map_yr_err(e),
    }
}

pub async fn token_require(State(state): State<Arc<AppState>>, headers: HeaderMap) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !matches!(
        state.config.iam_credential_type,
        IamCredentialType::Token | IamCredentialType::Both
    ) {
        return err_json(StatusCode::NOT_FOUND, "token credential mode disabled");
    }
    if !state.require_leader() {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "not leader");
    }
    let Some(tenant) = header(&headers, "x-tenant-id") else {
        return err_json(StatusCode::BAD_REQUEST, "missing X-Tenant-ID");
    };
    let role = header(&headers, "x-role").unwrap_or("default");
    let ttl_secs = header(&headers, "x-ttl")
        .and_then(|s| s.parse::<u64>().ok())
        .map(Duration::from_secs)
        .unwrap_or(state.config.token_ttl_default);
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    match state
        .token_store
        .issue(&mut ms, &state.config, tenant, role, ttl_secs)
        .await
    {
        Ok(token) => ok_json(json!({ "token": token })),
        Err(e) => map_yr_err(e),
    }
}

pub async fn token_refresh(State(state): State<Arc<AppState>>, headers: HeaderMap) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !matches!(
        state.config.iam_credential_type,
        IamCredentialType::Token | IamCredentialType::Both
    ) {
        return err_json(StatusCode::NOT_FOUND, "token credential mode disabled");
    }
    if !state.require_leader() {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "not leader");
    }
    let Some(old) = header(&headers, "x-auth") else {
        return err_json(StatusCode::BAD_REQUEST, "missing X-Auth");
    };
    let ttl_secs = header(&headers, "x-ttl")
        .and_then(|s| s.parse::<u64>().ok())
        .map(Duration::from_secs)
        .unwrap_or(state.config.token_ttl_default);
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    let claims = match state
        .token_store
        .verify(&mut ms, &state.config, old)
        .await
    {
        Ok(c) => c,
        Err(e) => return map_yr_err(e),
    };
    match state
        .token_store
        .issue(
            &mut ms,
            &state.config,
            &claims.tenant_id,
            &claims.role,
            ttl_secs,
        )
        .await
    {
        Ok(token) => ok_json(json!({ "token": token })),
        Err(e) => map_yr_err(e),
    }
}

pub async fn token_abandon(State(state): State<Arc<AppState>>, headers: HeaderMap) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !matches!(
        state.config.iam_credential_type,
        IamCredentialType::Token | IamCredentialType::Both
    ) {
        return err_json(StatusCode::NOT_FOUND, "token credential mode disabled");
    }
    if !state.require_leader() {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "not leader");
    }
    let Some(tenant) = header(&headers, "x-tenant-id") else {
        return err_json(StatusCode::BAD_REQUEST, "missing X-Tenant-ID");
    };
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    match state
        .token_store
        .abandon(&mut ms, &state.config, tenant)
        .await
    {
        Ok(()) => ok_json(json!({ "abandoned": true })),
        Err(e) => map_yr_err(e),
    }
}

pub async fn credential_require(State(state): State<Arc<AppState>>, headers: HeaderMap) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !matches!(
        state.config.iam_credential_type,
        IamCredentialType::AkSk | IamCredentialType::Both
    ) {
        return err_json(StatusCode::NOT_FOUND, "AK/SK credential mode disabled");
    }
    if !state.require_leader() {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "not leader");
    }
    let Some(tenant) = header(&headers, "x-tenant-id") else {
        return err_json(StatusCode::BAD_REQUEST, "missing X-Tenant-ID");
    };
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    match AkskManager::issue(&mut ms, &state.config, tenant).await {
        Ok(rec) => ok_json(json!({
            "tenant_id": rec.tenant_id,
            "access_key": rec.access_key,
            "secret_key": rec.secret_key,
        })),
        Err(e) => map_yr_err(e),
    }
}

pub async fn credential_auth(State(state): State<Arc<AppState>>, headers: HeaderMap) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !matches!(
        state.config.iam_credential_type,
        IamCredentialType::AkSk | IamCredentialType::Both
    ) {
        return err_json(StatusCode::NOT_FOUND, "AK/SK credential mode disabled");
    }
    let Some(ak) = header(&headers, "x-auth") else {
        return err_json(StatusCode::BAD_REQUEST, "missing X-Auth (access key)");
    };
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    match AkskManager::resolve_by_access_key(&mut ms, &state.config, ak).await {
        Ok(rec) => ok_json(json!({
            "tenant_id": rec.tenant_id,
            "access_key": rec.access_key,
            "secret_key": rec.secret_key,
        })),
        Err(e) => map_yr_err(e),
    }
}

pub async fn credential_abandon(State(state): State<Arc<AppState>>, headers: HeaderMap) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !matches!(
        state.config.iam_credential_type,
        IamCredentialType::AkSk | IamCredentialType::Both
    ) {
        return err_json(StatusCode::NOT_FOUND, "AK/SK credential mode disabled");
    }
    if !state.require_leader() {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "not leader");
    }
    let Some(tenant) = header(&headers, "x-tenant-id") else {
        return err_json(StatusCode::BAD_REQUEST, "missing X-Tenant-ID");
    };
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    match AkskManager::abandon(&mut ms, &state.config, tenant).await {
        Ok(()) => ok_json(json!({ "abandoned": true })),
        Err(e) => map_yr_err(e),
    }
}

// ======== External auth endpoints (Keycloak/Casdoor) ========

#[derive(Deserialize, Default)]
pub struct TokenExchangeBody {
    #[serde(default)]
    pub id_token: String,
    #[serde(default)]
    pub expires_in: Option<u64>,
}

/// C++ IAMActor: /v1/token/exchange
/// Exchange an external id_token for an internal token.
pub async fn token_exchange(
    State(state): State<Arc<AppState>>,
    Json(body): Json<TokenExchangeBody>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if body.id_token.is_empty() {
        return err_json(StatusCode::BAD_REQUEST, "missing id_token");
    }
    let expires_in = body.expires_in.unwrap_or(7200).clamp(60, 7200);
    let ttl = Duration::from_secs(expires_in);

    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };

    // External verifier not yet integrated; issue token using id_token as tenant_id
    match state
        .token_store
        .issue(&mut ms, &state.config, &body.id_token, "default", ttl)
        .await
    {
        Ok(token) => ok_json(json!({
            "token": token,
            "tenant_id": body.id_token,
            "expires_in": expires_in,
            "role": "default",
        })),
        Err(e) => map_yr_err(e),
    }
}

#[derive(Deserialize, Default)]
pub struct CodeExchangeBody {
    #[serde(default)]
    pub code: String,
    #[serde(default)]
    pub redirect_uri: String,
    #[serde(default)]
    pub expires_in: Option<u64>,
}

/// C++ IAMActor: /v1/token/code-exchange
/// Exchange an OAuth authorization code for an internal token.
pub async fn token_code_exchange(
    State(state): State<Arc<AppState>>,
    Json(body): Json<CodeExchangeBody>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if body.code.is_empty() {
        return err_json(StatusCode::BAD_REQUEST, "missing code");
    }
    if body.redirect_uri.is_empty() {
        return err_json(StatusCode::BAD_REQUEST, "missing redirect_uri");
    }
    let expires_in = body.expires_in.unwrap_or(7200).clamp(60, 7200);
    let ttl = Duration::from_secs(expires_in);

    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };

    // External OAuth verifier not yet integrated; use code as placeholder tenant_id
    match state
        .token_store
        .issue(&mut ms, &state.config, &body.code, "default", ttl)
        .await
    {
        Ok(token) => ok_json(json!({
            "token": token,
            "tenant_id": body.code,
            "expires_in": expires_in,
            "role": "default",
        })),
        Err(e) => map_yr_err(e),
    }
}

#[derive(Deserialize, Default)]
pub struct LoginBody {
    #[serde(default)]
    pub username: String,
    #[serde(default)]
    pub password: String,
}

/// C++ IAMActor: /v1/token/login
/// Username/password login.
pub async fn token_login(
    State(state): State<Arc<AppState>>,
    Json(body): Json<LoginBody>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if body.username.is_empty() || body.password.is_empty() {
        return err_json(StatusCode::BAD_REQUEST, "missing username or password");
    }

    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };

    // Password verification not yet integrated; issue token using username as tenant_id
    let ttl = state.config.token_ttl_default;
    match state
        .token_store
        .issue(&mut ms, &state.config, &body.username, "default", ttl)
        .await
    {
        Ok(token) => ok_json(json!({
            "token": token,
            "tenant_id": body.username,
            "expires_in": ttl.as_secs(),
            "role": "default",
        })),
        Err(e) => map_yr_err(e),
    }
}

#[derive(Deserialize, Default)]
pub struct AuthUrlQuery {
    #[serde(default, rename = "type")]
    pub auth_type: Option<String>,
    #[serde(default)]
    pub redirect_uri: Option<String>,
    #[serde(default)]
    pub state: Option<String>,
}

/// C++ IAMActor: /v1/auth/url
/// Generate an OAuth authorization URL for the external provider.
pub async fn auth_url(
    State(state): State<Arc<AppState>>,
    Query(q): Query<AuthUrlQuery>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }

    let auth_type = q.auth_type.as_deref().unwrap_or("login");
    let redirect_uri = q.redirect_uri.as_deref().unwrap_or("");
    let state_param = q.state.as_deref().unwrap_or("");

    // External provider not yet configured; return placeholder URL
    let url = format!(
        "https://auth.example.com/authorize?type={}&redirect_uri={}&state={}",
        auth_type, redirect_uri, state_param
    );

    ok_json(json!({ "url": url }))
}

#[derive(Deserialize, Default)]
pub struct TenantQuotaQuery {
    pub tenant_id: Option<String>,
}

/// C++ IAMActor: /v1/tenant/quota
/// Query tenant resource quotas.
pub async fn tenant_quota(
    State(state): State<Arc<AppState>>,
    Query(q): Query<TenantQuotaQuery>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    let Some(tenant_id) = q.tenant_id.filter(|s| !s.is_empty()) else {
        return err_json(StatusCode::BAD_REQUEST, "missing tenant_id");
    };

    // Quota store not yet implemented; return default unlimited quotas
    ok_json(json!({
        "tenant_id": tenant_id,
        "cpu_quota": -1,
        "mem_quota": -1,
    }))
}

// ----- REST-style resources: /v1/tokens, /v1/aksk, /v1/users, /v1/tenants, /v1/roles -----

#[derive(Deserialize, Default)]
pub struct TokensRestBody {
    #[serde(default)]
    pub tenant_id: String,
    #[serde(default)]
    pub role: Option<String>,
    #[serde(default)]
    pub ttl_secs: Option<u64>,
}

#[derive(Deserialize, Default)]
pub struct TokensRestQuery {
    #[serde(default)]
    pub token: Option<String>,
    #[serde(default)]
    pub tenant_id: Option<String>,
}

pub async fn v1_tokens_post(
    State(state): State<Arc<AppState>>,
    Json(body): Json<TokensRestBody>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !matches!(
        state.config.iam_credential_type,
        IamCredentialType::Token | IamCredentialType::Both
    ) {
        return err_json(StatusCode::NOT_FOUND, "token credential mode disabled");
    }
    if !state.require_leader() {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "not leader");
    }
    if body.tenant_id.is_empty() {
        return err_json(StatusCode::BAD_REQUEST, "missing tenant_id");
    }
    let role = body.role.as_deref().unwrap_or("default");
    let ttl_secs = body
        .ttl_secs
        .map(Duration::from_secs)
        .unwrap_or(state.config.token_ttl_default);
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    match state
        .token_store
        .issue(
            &mut ms,
            &state.config,
            &body.tenant_id,
            role,
            ttl_secs,
        )
        .await
    {
        Ok(token) => ok_json(json!({ "token": token })),
        Err(e) => map_yr_err(e),
    }
}

pub async fn v1_tokens_get(
    State(state): State<Arc<AppState>>,
    Query(q): Query<TokensRestQuery>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !matches!(
        state.config.iam_credential_type,
        IamCredentialType::Token | IamCredentialType::Both
    ) {
        return err_json(StatusCode::NOT_FOUND, "token credential mode disabled");
    }
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    if let Some(token) = q.token.as_ref().filter(|s| !s.is_empty()) {
        return match state.token_store.verify(&mut ms, &state.config, token).await {
            Ok(claims) => ok_json(json!({
                "tenant_id": claims.tenant_id,
                "role": claims.role,
                "iat": claims.iat,
                "exp": claims.exp,
            })),
            Err(e) => map_yr_err(e),
        };
    }
    if let Some(tenant) = q.tenant_id.as_ref().filter(|s| !s.is_empty()) {
        return match TokenManager::claims_for_tenant(&mut ms, &state.config, tenant).await {
            Ok(Some(c)) => ok_json(json!({
                "tenant_id": c.tenant_id,
                "role": c.role,
                "iat": c.iat,
                "exp": c.exp,
            })),
            Ok(None) => err_json(StatusCode::NOT_FOUND, "no active token for tenant"),
            Err(e) => map_yr_err(e),
        };
    }
    err_json(
        StatusCode::BAD_REQUEST,
        "query token= or tenant_id= is required",
    )
}

pub async fn v1_tokens_delete(
    State(state): State<Arc<AppState>>,
    Query(q): Query<TokensRestQuery>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !matches!(
        state.config.iam_credential_type,
        IamCredentialType::Token | IamCredentialType::Both
    ) {
        return err_json(StatusCode::NOT_FOUND, "token credential mode disabled");
    }
    if !state.require_leader() {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "not leader");
    }
    let Some(tenant) = q.tenant_id.as_ref().filter(|s| !s.is_empty()) else {
        return err_json(StatusCode::BAD_REQUEST, "missing tenant_id");
    };
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    match state
        .token_store
        .abandon(&mut ms, &state.config, tenant)
        .await
    {
        Ok(()) => ok_json(json!({ "deleted": true })),
        Err(e) => map_yr_err(e),
    }
}

#[derive(Deserialize, Default)]
pub struct AkskRestBody {
    #[serde(default)]
    pub tenant_id: String,
    #[serde(default)]
    pub role: Option<String>,
    #[serde(default)]
    pub ttl_secs: Option<u64>,
}

#[derive(Deserialize, Default)]
pub struct AkskRestQuery {
    #[serde(default)]
    pub tenant_id: Option<String>,
    #[serde(default)]
    pub access_key: Option<String>,
}

pub async fn v1_aksk_post(
    State(state): State<Arc<AppState>>,
    Json(body): Json<AkskRestBody>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !matches!(
        state.config.iam_credential_type,
        IamCredentialType::AkSk | IamCredentialType::Both
    ) {
        return err_json(StatusCode::NOT_FOUND, "AK/SK credential mode disabled");
    }
    if !state.require_leader() {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "not leader");
    }
    if body.tenant_id.is_empty() {
        return err_json(StatusCode::BAD_REQUEST, "missing tenant_id");
    }
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    match AkskManager::issue(&mut ms, &state.config, &body.tenant_id).await {
        Ok(rec) => {
            let role = body.role.as_deref().unwrap_or("default");
            let ttl = body.ttl_secs.unwrap_or(0);
            let enc = crate::token_store::TokenStore::aksk_enc_json(&rec, role, ttl);
            ok_json(json!({
                "tenant_id": rec.tenant_id,
                "access_key": rec.access_key,
                "secret_key": rec.secret_key,
                "aksk": enc,
            }))
        }
        Err(e) => map_yr_err(e),
    }
}

pub async fn v1_aksk_get(
    State(state): State<Arc<AppState>>,
    Query(q): Query<AkskRestQuery>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !matches!(
        state.config.iam_credential_type,
        IamCredentialType::AkSk | IamCredentialType::Both
    ) {
        return err_json(StatusCode::NOT_FOUND, "AK/SK credential mode disabled");
    }
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    if let Some(ak) = q.access_key.as_ref().filter(|s| !s.is_empty()) {
        return match AkskManager::resolve_by_access_key(&mut ms, &state.config, ak).await {
            Ok(rec) => ok_json(json!({
                "tenant_id": rec.tenant_id,
                "access_key": rec.access_key,
                "secret_key": rec.secret_key,
            })),
            Err(e) => map_yr_err(e),
        };
    }
    if let Some(tenant) = q.tenant_id.as_ref().filter(|s| !s.is_empty()) {
        let k = yr_common::etcd_keys::gen_aksk_key(&state.config.cluster_id, tenant, true);
        let raw = match ms.get(&k).await {
            Ok(r) => r,
            Err(e) => return map_yr_err(YrError::Etcd(e.to_string())),
        };
        let Some(kv) = raw.kvs.into_iter().next() else {
            return err_json(StatusCode::NOT_FOUND, "no aksk for tenant");
        };
        return match AkskManager::decode_stored(&state.config, &kv.value) {
            Ok(rec) => ok_json(json!({
                "tenant_id": rec.tenant_id,
                "access_key": rec.access_key,
                "secret_key": rec.secret_key,
            })),
            Err(e) => map_yr_err(e),
        };
    }
    err_json(
        StatusCode::BAD_REQUEST,
        "query access_key= or tenant_id= is required",
    )
}

pub async fn v1_aksk_delete(
    State(state): State<Arc<AppState>>,
    Query(q): Query<AkskRestQuery>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !matches!(
        state.config.iam_credential_type,
        IamCredentialType::AkSk | IamCredentialType::Both
    ) {
        return err_json(StatusCode::NOT_FOUND, "AK/SK credential mode disabled");
    }
    if !state.require_leader() {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "not leader");
    }
    let Some(tenant) = q.tenant_id.as_ref().filter(|s| !s.is_empty()) else {
        return err_json(StatusCode::BAD_REQUEST, "missing tenant_id");
    };
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    match AkskManager::abandon(&mut ms, &state.config, tenant).await {
        Ok(()) => ok_json(json!({ "deleted": true })),
        Err(e) => map_yr_err(e),
    }
}

#[derive(Deserialize, Default)]
pub struct IdQuery {
    #[serde(default)]
    pub id: Option<String>,
}

pub async fn v1_users_post(
    State(state): State<Arc<AppState>>,
    Json(body): Json<UserRecord>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !state.require_leader() {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "not leader");
    }
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    match UserManager::create_user(&mut ms, &state.config, body).await {
        Ok(u) => ok_json(serde_json::to_value(u).unwrap_or(json!({}))),
        Err(e) => map_yr_err(e),
    }
}

pub async fn v1_users_get(
    State(state): State<Arc<AppState>>,
    Query(q): Query<IdQuery>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    if let Some(id) = q.id.as_ref().filter(|s| !s.is_empty()) {
        return match UserManager::get_user(&mut ms, &state.config, id).await {
            Ok(Some(u)) => ok_json(serde_json::to_value(u).unwrap_or(json!({}))),
            Ok(None) => err_json(StatusCode::NOT_FOUND, "user not found"),
            Err(e) => map_yr_err(e),
        };
    }
    match UserManager::list_users(&mut ms, &state.config).await {
        Ok(list) => ok_json(json!({ "users": list })),
        Err(e) => map_yr_err(e),
    }
}

pub async fn v1_users_delete(
    State(state): State<Arc<AppState>>,
    Query(q): Query<IdQuery>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !state.require_leader() {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "not leader");
    }
    let Some(id) = q.id.as_ref().filter(|s| !s.is_empty()) else {
        return err_json(StatusCode::BAD_REQUEST, "missing id");
    };
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    match UserManager::delete_user(&mut ms, &state.config, id).await {
        Ok(()) => ok_json(json!({ "deleted": true })),
        Err(e) => map_yr_err(e),
    }
}

pub async fn v1_tenants_post(
    State(state): State<Arc<AppState>>,
    Json(body): Json<TenantRecord>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !state.require_leader() {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "not leader");
    }
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    match UserManager::create_tenant(&mut ms, &state.config, body).await {
        Ok(t) => ok_json(serde_json::to_value(t).unwrap_or(json!({}))),
        Err(e) => map_yr_err(e),
    }
}

pub async fn v1_tenants_get(
    State(state): State<Arc<AppState>>,
    Query(q): Query<IdQuery>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    if let Some(id) = q.id.as_ref().filter(|s| !s.is_empty()) {
        return match UserManager::get_tenant(&mut ms, &state.config, id).await {
            Ok(Some(t)) => ok_json(serde_json::to_value(t).unwrap_or(json!({}))),
            Ok(None) => err_json(StatusCode::NOT_FOUND, "tenant not found"),
            Err(e) => map_yr_err(e),
        };
    }
    match UserManager::list_tenants(&mut ms, &state.config).await {
        Ok(list) => ok_json(json!({ "tenants": list })),
        Err(e) => map_yr_err(e),
    }
}

pub async fn v1_tenants_delete(
    State(state): State<Arc<AppState>>,
    Query(q): Query<IdQuery>,
) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    if !state.require_leader() {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "not leader");
    }
    let Some(id) = q.id.as_ref().filter(|s| !s.is_empty()) else {
        return err_json(StatusCode::BAD_REQUEST, "missing id");
    };
    let mut ms = match lock_meta(&state).await {
        Ok(g) => g,
        Err(r) => return r,
    };
    match UserManager::delete_tenant(&mut ms, &state.config, id).await {
        Ok(()) => ok_json(json!({ "deleted": true })),
        Err(e) => map_yr_err(e),
    }
}

pub async fn v1_roles_get(State(state): State<Arc<AppState>>) -> Response {
    if !state.config.enable_iam {
        return err_json(StatusCode::SERVICE_UNAVAILABLE, "IAM disabled");
    }
    let roles = UserManager::builtin_roles();
    ok_json(json!({ "roles": roles }))
}

pub fn build_router(state: Arc<AppState>) -> Router<()> {
    Router::new()
        .route("/healthy", get(healthy))
        .route("/health", get(healthy))
        .route("/iam-server/healthy", get(healthy))
        .route(
            "/v1/tokens",
            get(v1_tokens_get).post(v1_tokens_post).delete(v1_tokens_delete),
        )
        .route(
            "/v1/aksk",
            get(v1_aksk_get).post(v1_aksk_post).delete(v1_aksk_delete),
        )
        .route(
            "/v1/users",
            get(v1_users_get).post(v1_users_post).delete(v1_users_delete),
        )
        .route(
            "/v1/tenants",
            get(v1_tenants_get)
                .post(v1_tenants_post)
                .delete(v1_tenants_delete),
        )
        .route("/v1/roles", get(v1_roles_get))
        .route("/v1/token/auth", get(token_auth))
        .route("/v1/token/require", get(token_require))
        .route("/v1/token/refresh", get(token_refresh))
        .route("/v1/token/abandon", get(token_abandon))
        .route("/v1/token/exchange", post(token_exchange))
        .route("/v1/token/code-exchange", post(token_code_exchange))
        .route("/v1/token/login", post(token_login))
        .route("/v1/auth/url", get(auth_url))
        .route("/v1/tenant/quota", get(tenant_quota))
        .route("/v1/credential/require", get(credential_require))
        .route("/v1/credential/auth", get(credential_auth))
        .route("/v1/credential/abandon", get(credential_abandon))
        .with_state(state)
}
