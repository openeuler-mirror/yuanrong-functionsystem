use std::time::{Duration, SystemTime, UNIX_EPOCH};

use base64::Engine;
use base64::engine::general_purpose::URL_SAFE_NO_PAD;
use hmac::{Hmac, Mac};
use serde::{Deserialize, Serialize};
use sha2::{Digest, Sha256};
use tracing::warn;
use yr_common::error::{YrError, YrResult};
use yr_common::etcd_keys::{
    gen_token_key, gen_token_watch_prefix, with_prefix, INTERNAL_IAM_TOKEN_PREFIX,
};
use yr_metastore_client::{MetaStoreClient, MetaStoreError};

use crate::config::IamConfig;

type HmacSha256 = Hmac<sha2::Sha256>;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TokenClaims {
    pub tenant_id: String,
    pub role: String,
    pub iat: u64,
    pub exp: u64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct StoredToken {
    pub token: String,
    pub claims: TokenClaims,
}

pub struct TokenManager;

impl TokenManager {
    fn signing_key(cfg: &IamConfig) -> [u8; 32] {
        let mut hasher = Sha256::new();
        hasher.update(cfg.cluster_id.as_bytes());
        hasher.update(b"|");
        hasher.update(
            with_prefix(&cfg.etcd_table_prefix, INTERNAL_IAM_TOKEN_PREFIX).as_bytes(),
        );
        hasher.update(b"|");
        hasher.update(cfg.iam_signing_secret.as_bytes());
        hasher.update(b"|yr-iam-token-v1");
        let out = hasher.finalize();
        let mut key = [0u8; 32];
        key.copy_from_slice(&out);
        key
    }

    fn sign_payload(key: &[u8; 32], payload_json: &str) -> String {
        let mut mac = HmacSha256::new_from_slice(key).expect("HMAC key length");
        mac.update(payload_json.as_bytes());
        let result = mac.finalize();
        hex::encode(result.into_bytes())
    }

    /// `payload_b64.sig_hex`
    pub fn mint(cfg: &IamConfig, tenant_id: &str, role: &str, ttl: Duration) -> YrResult<String> {
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_err(|e| YrError::Internal(e.to_string()))?
            .as_secs();
        let exp = now.saturating_add(ttl.as_secs().max(1));
        let claims = TokenClaims {
            tenant_id: tenant_id.to_string(),
            role: role.to_string(),
            iat: now,
            exp,
        };
        let payload_json = serde_json::to_string(&claims).map_err(|e| {
            YrError::Serialization(format!("token claims: {e}"))
        })?;
        let key = Self::signing_key(cfg);
        let sig = Self::sign_payload(&key, &payload_json);
        let payload_b64 = URL_SAFE_NO_PAD.encode(payload_json.as_bytes());
        Ok(format!("{payload_b64}.{sig}"))
    }

    pub fn parse_and_verify(cfg: &IamConfig, token: &str) -> YrResult<TokenClaims> {
        let (payload_b64, sig_hex) = token
            .split_once('.')
            .ok_or_else(|| YrError::Internal("malformed token".into()))?;
        let payload_bytes = URL_SAFE_NO_PAD
            .decode(payload_b64)
            .map_err(|_| YrError::Internal("invalid token payload encoding".into()))?;
        let payload_json = String::from_utf8(payload_bytes)
            .map_err(|_| YrError::Internal("invalid token payload utf-8".into()))?;
        let key = Self::signing_key(cfg);
        let expected = Self::sign_payload(&key, &payload_json);
        if !constant_time_eq(&expected, sig_hex) {
            return Err(YrError::Internal("invalid token signature".into()));
        }
        let claims: TokenClaims = serde_json::from_str(&payload_json).map_err(|e| {
            YrError::Serialization(format!("token claims: {e}"))
        })?;
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_err(|e| YrError::Internal(e.to_string()))?
            .as_secs();
        if now >= claims.exp {
            return Err(YrError::Internal("token expired".into()));
        }
        Ok(claims)
    }

    fn key_new(cfg: &IamConfig, tenant: &str) -> String {
        gen_token_key(&cfg.cluster_id, tenant, true)
    }

    fn key_old(cfg: &IamConfig, tenant: &str) -> String {
        gen_token_key(&cfg.cluster_id, tenant, false)
    }

    fn prefix_new(cfg: &IamConfig) -> String {
        let logical = gen_token_watch_prefix(&cfg.cluster_id, true);
        format!("{logical}/")
    }

    /// Logical etcd prefix (with table prefix applied by MetaStoreClient) for watch on `/new/` tokens.
    pub fn etcd_watch_prefix_new_tokens(cfg: &IamConfig) -> String {
        Self::prefix_new(cfg)
    }

    pub async fn issue(
        ms: &mut MetaStoreClient,
        cfg: &IamConfig,
        tenant_id: &str,
        role: &str,
        ttl: Duration,
    ) -> YrResult<String> {
        let token = Self::mint(cfg, tenant_id, role, ttl)?;
        let claims = Self::parse_and_verify(cfg, &token)?;
        let stored = StoredToken {
            token: token.clone(),
            claims,
        };
        let blob = serde_json::to_vec(&stored)
            .map_err(|e| YrError::Serialization(format!("stored token: {e}")))?;

        let k_new = Self::key_new(cfg, tenant_id);
        let k_old = Self::key_old(cfg, tenant_id);
        if let Some(cur) = ms
            .get(&k_new)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?
            .kvs
            .into_iter()
            .next()
            .map(|kv| kv.value)
        {
            ms.put(&k_old, &cur)
                .await
                .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        }
        ms.put(&k_new, &blob)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        Ok(token)
    }

    pub async fn verify(
        ms: &mut MetaStoreClient,
        cfg: &IamConfig,
        token: &str,
    ) -> YrResult<TokenClaims> {
        let claims = Self::parse_and_verify(cfg, token)?;
        let k_new = Self::key_new(cfg, &claims.tenant_id);
        let k_old = Self::key_old(cfg, &claims.tenant_id);

        let matches_stored = |raw: Option<Vec<u8>>| -> YrResult<bool> {
            let Some(raw) = raw else {
                return Ok(false);
            };
            let stored: StoredToken = serde_json::from_slice(&raw).map_err(|e| {
                YrError::Serialization(format!("etcd token record: {e}"))
            })?;
            Ok(stored.token == token)
        };

        let ok_new = matches_stored(
            ms.get(&k_new)
                .await
                .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?
                .kvs
                .into_iter()
                .next()
                .map(|kv| kv.value),
        )?;
        if ok_new {
            return Ok(claims);
        }
        let ok_old = matches_stored(
            ms.get(&k_old)
                .await
                .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?
                .kvs
                .into_iter()
                .next()
                .map(|kv| kv.value),
        )?;
        if ok_old {
            return Ok(claims);
        }
        Err(YrError::Internal(
            "token not present in etcd (new/old)".into(),
        ))
    }

    /// Returns validated claims for the tenant's current `new` token, if present.
    pub async fn claims_for_tenant(
        ms: &mut MetaStoreClient,
        cfg: &IamConfig,
        tenant_id: &str,
    ) -> YrResult<Option<TokenClaims>> {
        let k_new = Self::key_new(cfg, tenant_id);
        let raw = ms
            .get(&k_new)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?
            .kvs
            .into_iter()
            .next()
            .map(|kv| kv.value);
        let Some(raw) = raw else {
            return Ok(None);
        };
        let stored: StoredToken = serde_json::from_slice(&raw).map_err(|e| {
            YrError::Serialization(format!("etcd token record: {e}"))
        })?;
        let claims = Self::parse_and_verify(cfg, &stored.token)?;
        Ok(Some(claims))
    }

    pub async fn abandon(ms: &mut MetaStoreClient, cfg: &IamConfig, tenant_id: &str) -> YrResult<()> {
        let k_new = Self::key_new(cfg, tenant_id);
        let k_old = Self::key_old(cfg, tenant_id);
        let _ = ms
            .delete(&k_new)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        let _ = ms
            .delete(&k_old)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        Ok(())
    }

    /// Background rotation: for each active `new` token, refresh before expiry.
    pub async fn rotation_tick(ms: &mut MetaStoreClient, cfg: &IamConfig) {
        let prefix = Self::prefix_new(cfg);
        let entries = match ms.get_prefix(&prefix).await {
            Ok(r) => r
                .kvs
                .into_iter()
                .map(|kv| {
                    (
                        String::from_utf8_lossy(&kv.key).into_owned(),
                        kv.value,
                    )
                })
                .collect::<Vec<_>>(),
            Err(e) => {
                warn!(error = %e, "token rotation: list prefix failed");
                return;
            }
        };
        for (key, raw) in entries {
            let tenant = key.strip_prefix(&prefix).unwrap_or("");
            if tenant.is_empty() {
                continue;
            }
            let stored: StoredToken = match serde_json::from_slice(&raw) {
                Ok(s) => s,
                Err(_) => continue,
            };
            let now = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .map(|d| d.as_secs())
                .unwrap_or(0);
            let ttl = cfg.token_ttl_default.as_secs().max(60);
            let margin = (ttl / 5).max(30);
            if stored.claims.exp.saturating_sub(now) > margin {
                continue;
            }
            let role = stored.claims.role.clone();
            if let Err(e) = Self::issue(ms, cfg, tenant, &role, cfg.token_ttl_default).await {
                warn!(tenant = %tenant, error = %e, "token rotation: re-issue failed");
            }
        }
    }
}

fn constant_time_eq(a: &str, b: &str) -> bool {
    if a.len() != b.len() {
        return false;
    }
    let x = a.as_bytes().iter().zip(b.as_bytes()).fold(0u8, |acc, (x, y)| acc | (x ^ y));
    x == 0
}
