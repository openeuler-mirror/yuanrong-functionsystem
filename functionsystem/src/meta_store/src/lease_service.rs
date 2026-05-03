//! Lease grant / revoke / keepalive; expiry deletes keys bound to the lease.

use std::collections::{HashMap, HashSet};
use std::sync::Arc;
use std::time::{Duration, Instant};

use tokio::sync::Mutex;

use crate::backup::{BackupHandle, BackupOp};
use crate::config::MetaStoreRole;
use crate::kv_store::KvStore;
use crate::lease_validator::LeaseValidator;
use crate::pb::etcdserverpb::{
    LeaseGrantResponse, LeaseKeepAliveResponse, LeaseLeasesResponse, LeaseRevokeResponse,
    LeaseTimeToLiveResponse, ResponseHeader,
};
use crate::pb::mvccpb::Event;
use crate::watch_service::WatchHub;

#[derive(Debug)]
struct LeaseRecord {
    ttl_secs: i64,
    deadline: Instant,
}

#[derive(Clone)]
pub struct LeaseService {
    inner: Arc<LeaseInner>,
}

struct LeaseInner {
    state: Mutex<LeaseState>,
    backup: Option<BackupHandle>,
    role: MetaStoreRole,
}

struct LeaseState {
    next_id: i64,
    leases: HashMap<i64, LeaseRecord>,
}

impl LeaseService {
    pub fn new(backup: Option<BackupHandle>, role: MetaStoreRole) -> Self {
        Self {
            inner: Arc::new(LeaseInner {
                state: Mutex::new(LeaseState {
                    next_id: 1,
                    leases: HashMap::new(),
                }),
                backup,
                role,
            }),
        }
    }

    pub async fn is_valid(&self, id: i64) -> bool {
        if id == 0 {
            return false;
        }
        let g = self.inner.state.lock().await;
        g.leases
            .get(&id)
            .map(|r| r.deadline > Instant::now())
            .unwrap_or(false)
    }

    pub async fn restore_leases(&self, recovered: &[(i64, i64)]) {
        if recovered.is_empty() {
            return;
        }
        let mut g = self.inner.state.lock().await;
        let now = Instant::now();
        for (lease_id, ttl) in recovered {
            if *lease_id <= 0 {
                continue;
            }
            let ttl_secs = (*ttl).max(1);
            g.leases.insert(
                *lease_id,
                LeaseRecord {
                    ttl_secs,
                    deadline: now + Duration::from_secs(ttl_secs as u64),
                },
            );
            g.next_id = g.next_id.max(*lease_id + 1);
        }
    }

    pub async fn sync_backup_snapshot(&self, recovered: &[(i64, i64)]) {
        let mut g = self.inner.state.lock().await;
        let now = Instant::now();
        let remote_ids: HashSet<i64> = recovered
            .iter()
            .filter_map(|(lease_id, _)| (*lease_id > 0).then_some(*lease_id))
            .collect();
        g.leases.retain(|lease_id, _| remote_ids.contains(lease_id));
        for (lease_id, ttl) in recovered {
            if *lease_id <= 0 {
                continue;
            }
            let ttl_secs = (*ttl).max(1);
            g.leases.insert(
                *lease_id,
                LeaseRecord {
                    ttl_secs,
                    deadline: now + Duration::from_secs(ttl_secs as u64),
                },
            );
            g.next_id = g.next_id.max(*lease_id + 1);
        }
    }

    pub async fn apply_backup_put(&self, lease_id: i64, ttl: i64) {
        self.restore_leases(&[(lease_id, ttl)]).await;
    }

    pub async fn apply_backup_delete(&self, lease_id: i64) {
        let mut g = self.inner.state.lock().await;
        g.leases.remove(&lease_id);
    }

    pub async fn grant(
        &self,
        ttl: i64,
        requested_id: i64,
        header: ResponseHeader,
    ) -> Result<LeaseGrantResponse, tonic::Status> {
        if self.inner.role != MetaStoreRole::Master {
            return Err(tonic::Status::permission_denied("slave rejects lease grant"));
        }
        let mut g = self.inner.state.lock().await;
        let lease_id = if requested_id != 0 {
            requested_id
        } else {
            let id = g.next_id;
            g.next_id += 1;
            id
        };
        let ttl = ttl.max(1);
        g.leases.insert(
            lease_id,
            LeaseRecord {
                ttl_secs: ttl,
                deadline: Instant::now() + Duration::from_secs(ttl as u64),
            },
        );
        if let Some(b) = &self.inner.backup {
            b.try_send(BackupOp::LeasePut {
                lease_id,
                ttl_secs: ttl,
            });
        }
        Ok(LeaseGrantResponse {
            header: Some(header),
            id: lease_id,
            ttl,
            error: String::new(),
        })
    }

    pub async fn revoke(
        &self,
        lease_id: i64,
        kv: &KvStore,
        header: ResponseHeader,
    ) -> Result<(LeaseRevokeResponse, Vec<(Vec<u8>, Event)>), tonic::Status> {
        if self.inner.role != MetaStoreRole::Master {
            return Err(tonic::Status::permission_denied("slave rejects lease revoke"));
        }
        {
            let mut g = self.inner.state.lock().await;
            g.leases.remove(&lease_id);
        }
        if let Some(b) = &self.inner.backup {
            b.try_send(BackupOp::LeaseDelete { lease_id });
        }
        let events = delete_keys_for_lease(kv, lease_id).await;
        Ok((
            LeaseRevokeResponse {
                header: Some(header),
            },
            events,
        ))
    }

    pub async fn keep_alive(
        &self,
        lease_id: i64,
        header: ResponseHeader,
    ) -> LeaseKeepAliveResponse {
        let mut g = self.inner.state.lock().await;
        let Some(rec) = g.leases.get_mut(&lease_id) else {
            return LeaseKeepAliveResponse {
                header: Some(header),
                id: lease_id,
                ttl: -1,
            };
        };
        rec.deadline = Instant::now() + Duration::from_secs(rec.ttl_secs as u64);
        let ttl = rec.ttl_secs;
        LeaseKeepAliveResponse {
            header: Some(header),
            id: lease_id,
            ttl,
        }
    }

    pub async fn time_to_live(
        &self,
        lease_id: i64,
        with_keys: bool,
        header: ResponseHeader,
        kv: &KvStore,
    ) -> LeaseTimeToLiveResponse {
        let g = self.inner.state.lock().await;
        let Some(rec) = g.leases.get(&lease_id) else {
            return LeaseTimeToLiveResponse {
                header: Some(header),
                id: lease_id,
                ttl: -1,
                granted_ttl: 0,
                keys: vec![],
            };
        };
        let remain = rec.deadline.saturating_duration_since(Instant::now());
        let ttl = remain.as_secs() as i64;
        let keys = if with_keys {
            keys_for_lease(kv, lease_id).await
        } else {
            vec![]
        };
        LeaseTimeToLiveResponse {
            header: Some(header),
            id: lease_id,
            ttl,
            granted_ttl: rec.ttl_secs,
            keys,
        }
    }

    pub async fn list_leases(&self, header: ResponseHeader) -> LeaseLeasesResponse {
        let g = self.inner.state.lock().await;
        let leases = g
            .leases
            .keys()
            .map(|id| crate::pb::etcdserverpb::LeaseStatus { id: *id })
            .collect();
        LeaseLeasesResponse {
            header: Some(header),
            leases,
        }
    }

    pub fn spawn_expiry(self, kv: Arc<KvStore>, hub: WatchHub, cfg: crate::config::MetaStoreServerConfig) {
        tokio::spawn(async move {
            let mut tick = tokio::time::interval(Duration::from_millis(200));
            loop {
                tick.tick().await;
                let now = Instant::now();
                let expired: Vec<i64> = {
                    let g = self.inner.state.lock().await;
                    g.leases
                        .iter()
                        .filter(|(_, r)| r.deadline <= now)
                        .map(|(id, _)| *id)
                        .collect()
                };
                for id in expired {
                    {
                        let mut g = self.inner.state.lock().await;
                        g.leases.remove(&id);
                    }
                    if let Some(b) = &self.inner.backup {
                        b.try_send(BackupOp::LeaseDelete { lease_id: id });
                    }
                    let evs = delete_keys_for_lease(&kv, id).await;
                    let rev = kv.current_revision().await;
                    let hdr = ResponseHeader {
                        cluster_id: cfg.cluster_id,
                        member_id: cfg.member_id,
                        revision: rev,
                        raft_term: 1,
                    };
                    for (k, ev) in evs {
                        hub.publish(&k, ev, hdr.clone());
                    }
                }
            }
        });
    }
}

async fn keys_for_lease(kv: &KvStore, lease_id: i64) -> Vec<Vec<u8>> {
    let st = kv.get_state().await;
    st.cache
        .iter()
        .filter(|(_, v)| v.lease == lease_id)
        .map(|(k, _)| k.clone())
        .collect()
}

#[async_trait::async_trait]
impl LeaseValidator for LeaseService {
    async fn valid_lease(&self, id: i64) -> bool {
        self.is_valid(id).await
    }
}

async fn delete_keys_for_lease(kv: &KvStore, lease_id: i64) -> Vec<(Vec<u8>, Event)> {
    let keys = keys_for_lease(kv, lease_id).await;
    let mut out = vec![];
    for key in keys {
        let Ok((_, evs)) = kv
            .delete_range(crate::pb::etcdserverpb::DeleteRangeRequest {
                key,
                range_end: vec![],
                prev_kv: false,
            })
            .await
        else {
            continue;
        };
        out.extend(evs);
    }
    out
}
