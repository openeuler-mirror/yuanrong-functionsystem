use rand::RngCore;
use serde::{Deserialize, Serialize};
use yr_common::error::{YrError, YrResult};
use yr_common::etcd_keys::{gen_aksk_key, with_prefix, INTERNAL_IAM_AKSK_PREFIX};
use yr_metastore_client::{MetaStoreClient, MetaStoreError};

use crate::config::IamConfig;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AkskRecord {
    pub tenant_id: String,
    pub access_key: String,
    pub secret_key: String,
}

pub struct AkskManager;

impl AkskManager {
    fn key_new(cfg: &IamConfig, tenant: &str) -> String {
        gen_aksk_key(&cfg.cluster_id, tenant, true)
    }

    fn key_old(cfg: &IamConfig, tenant: &str) -> String {
        gen_aksk_key(&cfg.cluster_id, tenant, false)
    }

    fn key_by_ak(cfg: &IamConfig, access_key: &str) -> String {
        format!("{}/by-ak/{}/{}", INTERNAL_IAM_AKSK_PREFIX, cfg.cluster_id, access_key)
    }

    fn random_hex(bytes: usize) -> String {
        let mut buf = vec![0u8; bytes];
        rand::thread_rng().fill_bytes(&mut buf);
        hex::encode(buf)
    }

    /// Stored form: XOR obfuscation with cluster-derived pad (not encryption; etcd is trusted).
    fn encode_record(cfg: &IamConfig, rec: &AkskRecord) -> YrResult<Vec<u8>> {
        let json = serde_json::to_vec(rec)
            .map_err(|e| YrError::Serialization(format!("aksk: {e}")))?;
        let pad = Self::xor_pad(cfg, json.len());
        let enc: Vec<u8> = json.iter().zip(pad.iter()).map(|(a, b)| a ^ b).collect();
        Ok(enc)
    }

    pub fn decode_stored(cfg: &IamConfig, raw: &[u8]) -> YrResult<AkskRecord> {
        Self::decode_record(cfg, raw)
    }

    fn decode_record(cfg: &IamConfig, raw: &[u8]) -> YrResult<AkskRecord> {
        let pad = Self::xor_pad(cfg, raw.len());
        let dec: Vec<u8> = raw.iter().zip(pad.iter()).map(|(a, b)| a ^ b).collect();
        serde_json::from_slice(&dec).map_err(|e| {
            YrError::Serialization(format!("aksk record: {e}"))
        })
    }

    fn xor_pad(cfg: &IamConfig, len: usize) -> Vec<u8> {
        use sha2::{Digest, Sha256};
        let mut out = Vec::with_capacity(len);
        let mut chunk = 0u64;
        while out.len() < len {
            let mut h = Sha256::new();
            h.update(cfg.cluster_id.as_bytes());
            h.update(b"|aksk|");
            h.update(
                with_prefix(&cfg.etcd_table_prefix, INTERNAL_IAM_AKSK_PREFIX).as_bytes(),
            );
            h.update(chunk.to_le_bytes());
            let digest = h.finalize();
            out.extend_from_slice(&digest);
            chunk += 1;
        }
        out.truncate(len);
        out
    }

    async fn delete_by_ak_index(
        ms: &mut MetaStoreClient,
        cfg: &IamConfig,
        access_key: &str,
    ) -> YrResult<()> {
        let k = Self::key_by_ak(cfg, access_key);
        let _ = ms
            .delete(&k)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        Ok(())
    }

    pub async fn issue(ms: &mut MetaStoreClient, cfg: &IamConfig, tenant_id: &str) -> YrResult<AkskRecord> {
        let rec = AkskRecord {
            tenant_id: tenant_id.to_string(),
            access_key: Self::random_hex(16),
            secret_key: Self::random_hex(32),
        };

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
            let old_rec = Self::decode_record(cfg, &cur)?;
            Self::delete_by_ak_index(ms, cfg, &old_rec.access_key).await?;
            ms.put(&k_old, &cur)
                .await
                .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        }

        let blob = Self::encode_record(cfg, &rec)?;
        ms.put(&k_new, &blob)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        let idx_val = tenant_id.as_bytes().to_vec();
        ms.put(
            &Self::key_by_ak(cfg, &rec.access_key),
            &idx_val,
        )
        .await
        .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        Ok(rec)
    }

    pub async fn resolve_by_access_key(
        ms: &mut MetaStoreClient,
        cfg: &IamConfig,
        access_key: &str,
    ) -> YrResult<AkskRecord> {
        let idx_key = Self::key_by_ak(cfg, access_key);
        let tenant_raw = ms
            .get(&idx_key)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?
            .kvs
            .into_iter()
            .next()
            .map(|kv| kv.value)
            .ok_or_else(|| YrError::Internal("unknown access key".into()))?;
        let tenant_id = String::from_utf8(tenant_raw)
            .map_err(|_| YrError::Internal("invalid tenant index".into()))?;

        let k_new = Self::key_new(cfg, &tenant_id);
        if let Some(raw) = ms
            .get(&k_new)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?
            .kvs
            .into_iter()
            .next()
            .map(|kv| kv.value)
        {
            let rec = Self::decode_record(cfg, &raw)?;
            if rec.access_key == access_key {
                return Ok(rec);
            }
        }
        let k_old = Self::key_old(cfg, &tenant_id);
        if let Some(raw) = ms
            .get(&k_old)
            .await
            .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?
            .kvs
            .into_iter()
            .next()
            .map(|kv| kv.value)
        {
            let rec = Self::decode_record(cfg, &raw)?;
            if rec.access_key == access_key {
                return Ok(rec);
            }
        }
        Err(YrError::Internal(
            "credential not found for access key".into(),
        ))
    }

    pub async fn abandon(ms: &mut MetaStoreClient, cfg: &IamConfig, tenant_id: &str) -> YrResult<()> {
        for key in [Self::key_new(cfg, tenant_id), Self::key_old(cfg, tenant_id)] {
            if let Some(raw) = ms
                .get(&key)
                .await
                .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?
                .kvs
                .into_iter()
                .next()
                .map(|kv| kv.value)
            {
                if let Ok(rec) = Self::decode_record(cfg, &raw) {
                    let _ = Self::delete_by_ak_index(ms, cfg, &rec.access_key).await;
                }
            }
            let _ = ms
                .delete(&key)
                .await
                .map_err(|e: MetaStoreError| YrError::Etcd(e.to_string()))?;
        }
        Ok(())
    }
}
