//! User / tenant / role records persisted under [`yr_common::etcd_keys`] IAM prefixes.

use serde::{Deserialize, Serialize};
use yr_common::error::{YrError, YrResult};
use yr_common::etcd_keys::{gen_iam_tenant_key, gen_iam_tenant_prefix, gen_iam_user_key, gen_iam_user_prefix};
use yr_metastore_client::{MetaStoreClient, MetaStoreError};

use crate::config::IamConfig;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct UserRecord {
    pub user_id: String,
    pub tenant_id: String,
    pub roles: Vec<String>,
    #[serde(default)]
    pub email: Option<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TenantRecord {
    pub tenant_id: String,
    #[serde(default)]
    pub display_name: Option<String>,
    #[serde(default)]
    pub status: Option<String>,
}

pub struct UserManager;

impl UserManager {
    pub async fn create_user(
        ms: &mut MetaStoreClient,
        cfg: &IamConfig,
        mut rec: UserRecord,
    ) -> YrResult<UserRecord> {
        if rec.user_id.is_empty() {
            return Err(YrError::Internal("user_id is empty".into()));
        }
        if rec.tenant_id.is_empty() {
            return Err(YrError::Internal("tenant_id is empty".into()));
        }
        if rec.roles.is_empty() {
            rec.roles.push("user".into());
        }
        let key = gen_iam_user_key(&cfg.cluster_id, &rec.user_id);
        let blob = serde_json::to_vec(&rec)
            .map_err(|e| YrError::Serialization(format!("user record: {e}")))?;
        ms.put(&key, &blob)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        Ok(rec)
    }

    pub async fn get_user(
        ms: &mut MetaStoreClient,
        cfg: &IamConfig,
        user_id: &str,
    ) -> YrResult<Option<UserRecord>> {
        let key = gen_iam_user_key(&cfg.cluster_id, user_id);
        let r = ms
            .get(&key)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        let Some(kv) = r.kvs.into_iter().next() else {
            return Ok(None);
        };
        let u: UserRecord = serde_json::from_slice(&kv.value).map_err(|e| {
            YrError::Serialization(format!("user record: {e}"))
        })?;
        Ok(Some(u))
    }

    pub async fn list_users(ms: &mut MetaStoreClient, cfg: &IamConfig) -> YrResult<Vec<UserRecord>> {
        let pfx = gen_iam_user_prefix(&cfg.cluster_id);
        let r = ms
            .get_prefix(&pfx)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        let mut out = Vec::new();
        for kv in r.kvs {
            if let Ok(u) = serde_json::from_slice::<UserRecord>(&kv.value) {
                out.push(u);
            }
        }
        out.sort_by(|a, b| a.user_id.cmp(&b.user_id));
        Ok(out)
    }

    pub async fn delete_user(ms: &mut MetaStoreClient, cfg: &IamConfig, user_id: &str) -> YrResult<()> {
        let key = gen_iam_user_key(&cfg.cluster_id, user_id);
        ms.delete(&key)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        Ok(())
    }

    pub async fn create_tenant(
        ms: &mut MetaStoreClient,
        cfg: &IamConfig,
        mut rec: TenantRecord,
    ) -> YrResult<TenantRecord> {
        if rec.tenant_id.is_empty() {
            return Err(YrError::Internal("tenant_id is empty".into()));
        }
        if rec.status.is_none() {
            rec.status = Some("active".into());
        }
        let key = gen_iam_tenant_key(&cfg.cluster_id, &rec.tenant_id);
        let blob = serde_json::to_vec(&rec)
            .map_err(|e| YrError::Serialization(format!("tenant record: {e}")))?;
        ms.put(&key, &blob)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        Ok(rec)
    }

    pub async fn get_tenant(
        ms: &mut MetaStoreClient,
        cfg: &IamConfig,
        tenant_id: &str,
    ) -> YrResult<Option<TenantRecord>> {
        let key = gen_iam_tenant_key(&cfg.cluster_id, tenant_id);
        let r = ms
            .get(&key)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        let Some(kv) = r.kvs.into_iter().next() else {
            return Ok(None);
        };
        let t: TenantRecord = serde_json::from_slice(&kv.value).map_err(|e| {
            YrError::Serialization(format!("tenant record: {e}"))
        })?;
        Ok(Some(t))
    }

    pub async fn list_tenants(ms: &mut MetaStoreClient, cfg: &IamConfig) -> YrResult<Vec<TenantRecord>> {
        let pfx = gen_iam_tenant_prefix(&cfg.cluster_id);
        let r = ms
            .get_prefix(&pfx)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        let mut out = Vec::new();
        for kv in r.kvs {
            if let Ok(t) = serde_json::from_slice::<TenantRecord>(&kv.value) {
                out.push(t);
            }
        }
        out.sort_by(|a, b| a.tenant_id.cmp(&b.tenant_id));
        Ok(out)
    }

    pub async fn delete_tenant(ms: &mut MetaStoreClient, cfg: &IamConfig, tenant_id: &str) -> YrResult<()> {
        let key = gen_iam_tenant_key(&cfg.cluster_id, tenant_id);
        ms.delete(&key)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        Ok(())
    }

    /// Fixed role catalog (see C++ `constants.h`).
    pub fn builtin_roles() -> Vec<serde_json::Value> {
        vec![
            json_role("admin", 4),
            json_role("developer", 3),
            json_role("user", 2),
            json_role("viewer", 1),
        ]
    }
}

fn json_role(name: &'static str, priority: i32) -> serde_json::Value {
    serde_json::json!({ "name": name, "priority": priority })
}
