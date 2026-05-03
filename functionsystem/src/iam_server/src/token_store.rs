//! In-memory token cache (short TTL cap) plus etcd persistence via [`crate::token::TokenManager`].
//! AK/SK wire payloads use [`yr_common::aksk::EncAKSKContent`] for parity with C++ JSON shapes.

use std::collections::HashMap;
use std::sync::RwLock;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use rand::RngCore;
use serde_json::json;
use yr_common::aksk::EncAKSKContent;
use yr_common::error::YrResult;
use yr_metastore_client::MetaStoreClient;

use crate::aksk::AkskRecord;
use crate::config::IamConfig;
use crate::token::{IssuedToken, TokenClaims, TokenManager};

const MAX_POSITIVE_CACHE_SECS: u64 = 60;

fn now_secs() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}

struct CacheEntry {
    claims: TokenClaims,
    /// Stop using this cache entry after this unix second (inclusive cap).
    valid_until: u64,
}

/// Process-wide token verification cache; etcd remains source of truth.
pub struct TokenStore {
    by_token: RwLock<HashMap<String, CacheEntry>>,
}

impl Default for TokenStore {
    fn default() -> Self {
        Self::new()
    }
}

impl TokenStore {
    pub fn new() -> Self {
        Self {
            by_token: RwLock::new(HashMap::new()),
        }
    }

    fn cache_valid_until(claims: &TokenClaims) -> u64 {
        let now = now_secs();
        let cap = now.saturating_add(MAX_POSITIVE_CACHE_SECS);
        claims.exp.min(cap)
    }

    pub fn invalidate_tenant(&self, tenant_id: &str) {
        let mut g = self.by_token.write().expect("token cache lock");
        g.retain(|_, v| v.claims.tenant_id != tenant_id);
    }

    pub fn invalidate_token(&self, token: &str) {
        let mut g = self.by_token.write().expect("token cache lock");
        g.remove(token);
    }

    pub async fn verify(
        &self,
        ms: &mut MetaStoreClient,
        cfg: &IamConfig,
        token: &str,
    ) -> YrResult<TokenClaims> {
        {
            let g = self.by_token.read().expect("token cache lock");
            if let Some(ent) = g.get(token) {
                let now = now_secs();
                if now < ent.valid_until && now < ent.claims.exp {
                    return Ok(ent.claims.clone());
                }
            }
        }

        let claims = TokenManager::verify(ms, cfg, token).await?;
        let valid_until = Self::cache_valid_until(&claims);
        let mut g = self.by_token.write().expect("token cache lock");
        g.insert(
            token.to_string(),
            CacheEntry {
                claims: claims.clone(),
                valid_until,
            },
        );
        Ok(claims)
    }

    pub async fn issue(
        &self,
        ms: &mut MetaStoreClient,
        cfg: &IamConfig,
        tenant_id: &str,
        role: &str,
        ttl: Duration,
    ) -> YrResult<String> {
        Ok(self
            .issue_with_metadata(ms, cfg, tenant_id, role, ttl)
            .await?
            .token)
    }

    pub async fn issue_with_metadata(
        &self,
        ms: &mut MetaStoreClient,
        cfg: &IamConfig,
        tenant_id: &str,
        role: &str,
        ttl: Duration,
    ) -> YrResult<IssuedToken> {
        let issued = TokenManager::issue_with_metadata(ms, cfg, tenant_id, role, ttl).await?;
        let valid_until = Self::cache_valid_until(&issued.claims);
        let mut g = self.by_token.write().expect("token cache lock");
        g.retain(|_, v| v.claims.tenant_id != tenant_id);
        g.insert(
            issued.token.clone(),
            CacheEntry {
                claims: issued.claims.clone(),
                valid_until,
            },
        );
        Ok(issued)
    }

    pub async fn abandon(
        &self,
        ms: &mut MetaStoreClient,
        cfg: &IamConfig,
        tenant_id: &str,
    ) -> YrResult<()> {
        self.invalidate_tenant(tenant_id);
        TokenManager::abandon(ms, cfg, tenant_id).await
    }

    /// JSON object aligned with C++ `EncAKSKContent` field names for REST clients.
    pub fn aksk_enc_json(rec: &AkskRecord, role: impl AsRef<str>, ttl_secs: u64) -> serde_json::Value {
        let mut data_key = vec![0u8; 16];
        rand::thread_rng().fill_bytes(&mut data_key);
        let now = now_secs();
        let (exp_ts, span) = if ttl_secs == 0 {
            (0u64, 0u64)
        } else {
            (now.saturating_add(ttl_secs), ttl_secs)
        };
        let enc = EncAKSKContent {
            tenant_id: rec.tenant_id.clone(),
            access_key: rec.access_key.clone(),
            secret_key: rec.secret_key.clone(),
            data_key: hex::encode(data_key),
            expired_time_stamp: exp_ts,
            expired_time_span: span,
            role: role.as_ref().to_string(),
            status: yr_common::status::Status::ok(),
        };
        serde_json::to_value(&enc).unwrap_or_else(|_| {
            json!({
                "tenantID": rec.tenant_id,
                "accessKey": rec.access_key,
                "secretKey": rec.secret_key,
            })
        })
    }
}
