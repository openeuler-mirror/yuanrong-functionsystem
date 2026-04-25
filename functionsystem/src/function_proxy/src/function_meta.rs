//! Function metadata cache: etcd keys under [`yr_common::etcd_keys::FUNC_META_PATH_PREFIX`], aligned with C++ `function_meta_manager` / watcher.

use dashmap::DashMap;
use futures::StreamExt;
use std::sync::Arc;
use tracing::{info, warn};
use yr_common::etcd_keys::FUNC_META_PATH_PREFIX;
use yr_metastore_client::{MetaStoreClient, WatchEventType};

use crate::instance_manager::FunctionMetaChangeKind;
use crate::AppContext;

/// Latest etcd payload per `tenant/func/version` key (same shape as `gen_func_meta_key` input).
#[derive(Default)]
pub struct FunctionMetaCache {
    /// `None` value = key known from a successful get during schedule, full body not cached yet.
    entries: DashMap<String, Option<Vec<u8>>>,
}

impl FunctionMetaCache {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn insert_key(&self, k: String) {
        self.entries.entry(k).or_insert(None);
    }

    pub fn upsert_value(&self, k: String, value: Vec<u8>) {
        self.entries.insert(k, Some(value));
    }

    pub fn remove_key(&self, k: &str) {
        self.entries.remove(k);
    }

    pub fn contains(&self, func_key: &str) -> bool {
        self.entries.contains_key(func_key)
    }

    /// Raw etcd value when the watch or reconcile populated it.
    pub fn get_raw(&self, func_key: &str) -> Option<Vec<u8>> {
        self.entries.get(func_key).and_then(|e| e.value().clone())
    }

    /// Map logical MetaStore key → `tenant/func/version`.
    pub fn logical_key_to_function_key(logical_etcd_key: &str) -> Option<String> {
        let base = format!("{}/", FUNC_META_PATH_PREFIX.trim_end_matches('/'));
        let rest = logical_etcd_key.strip_prefix(&base)?;
        let parts: Vec<&str> = rest.split('/').collect();
        if parts.len() >= 5 && parts[1] == "function" && parts[3] == "version" {
            Some(format!("{}/{}/{}", parts[0], parts[2], parts[4]))
        } else {
            None
        }
    }

    pub fn apply_watch_put(&self, logical_key: &str, value: &[u8]) {
        if let Some(fk) = Self::logical_key_to_function_key(logical_key) {
            self.upsert_value(fk, value.to_vec());
        }
    }

    pub fn apply_watch_delete(&self, logical_key: &str) {
        if let Some(fk) = Self::logical_key_to_function_key(logical_key) {
            self.remove_key(&fk);
        }
    }

    pub async fn reconcile_all(store: &mut MetaStoreClient, cache: &FunctionMetaCache) {
        let prefix = format!("{}/", FUNC_META_PATH_PREFIX.trim_end_matches('/'));
        let res = match store.get_prefix(&prefix).await {
            Ok(r) => r,
            Err(e) => {
                warn!(error = %e, "function meta reconcile get_prefix");
                return;
            }
        };
        for kv in res.kvs {
            let s = String::from_utf8_lossy(&kv.key);
            cache.apply_watch_put(s.as_ref(), &kv.value);
        }
    }
}

/// One-shot initial reconciliation: called from main BEFORE gRPC serve.
pub async fn initial_sync(ctx: Arc<AppContext>) {
    let Some(etcd) = ctx.etcd.clone() else {
        return;
    };
    let cache = ctx.instance_ctrl.function_meta_cache().clone();
    let mut c = etcd.lock().await;
    FunctionMetaCache::reconcile_all(&mut c, &cache).await;
    info!("initial_sync: function metadata reconciled from etcd");
}

/// Long-running watch loop: called from a spawned task AFTER initial_sync completes.
pub async fn run_watch_loop(ctx: Arc<AppContext>) {
    let Some(etcd) = ctx.etcd.clone() else {
        return;
    };

    let prefix = format!("{}/", FUNC_META_PATH_PREFIX.trim_end_matches('/'));
    let cache = ctx.instance_ctrl.function_meta_cache().clone();

    let ctx_w = ctx.clone();
    let etcd_w = etcd.clone();
    let cache_w = cache.clone();
    tokio::spawn(async move {
        loop {
            let mut stream = {
                let mut c = etcd_w.lock().await;
                c.watch_prefix(&prefix)
            };
            loop {
                match stream.next().await {
                    Some(Ok(ev)) => {
                        let logical = String::from_utf8_lossy(&ev.key);
                        match ev.event_type {
                            WatchEventType::Put => {
                                cache_w.apply_watch_put(logical.as_ref(), &ev.value);
                                if let Some(fk) =
                                    FunctionMetaCache::logical_key_to_function_key(logical.as_ref())
                                {
                                    ctx_w.instance_manager.on_function_meta_change(
                                        &fk,
                                        FunctionMetaChangeKind::Upsert,
                                    );
                                }
                            }
                            WatchEventType::Delete => {
                                let fk = FunctionMetaCache::logical_key_to_function_key(
                                    logical.as_ref(),
                                );
                                cache_w.apply_watch_delete(logical.as_ref());
                                if let Some(ref func_key) = fk {
                                    ctx_w.instance_manager.on_function_meta_change(
                                        func_key,
                                        FunctionMetaChangeKind::Delete,
                                    );
                                }
                            }
                        }
                    }
                    Some(Err(e)) => {
                        warn!(error = %e, "function meta watch");
                        let mut c2 = etcd_w.lock().await;
                        FunctionMetaCache::reconcile_all(&mut c2, &cache_w).await;
                        break;
                    }
                    None => {
                        warn!("function meta watch ended");
                        tokio::time::sleep(std::time::Duration::from_secs(1)).await;
                        break;
                    }
                }
            }
        }
    });

    info!("function metadata watch loop started");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_function_key_from_logical_path() {
        let k = "/yr/functions/business/yrk/tenant/default/function/hello/version/$latest";
        assert_eq!(
            FunctionMetaCache::logical_key_to_function_key(k),
            Some("default/hello/$latest".into())
        );
    }

    #[test]
    fn upsert_then_get_raw() {
        let c = FunctionMetaCache::new();
        c.upsert_value("t/f/v".into(), b"{}"[..].to_vec());
        assert_eq!(c.get_raw("t/f/v"), Some(b"{}"[..].to_vec()));
    }
}
