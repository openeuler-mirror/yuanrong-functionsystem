//! In-memory KV state: BTreeMap cache, monotonic revision, compare helpers.

use std::collections::{BTreeMap, BTreeSet};
use std::sync::Arc;

use serde::{Deserialize, Serialize};
use tokio::sync::RwLock;

use crate::config::MetaStoreServerConfig;
use crate::error::MetaStoreError;
use crate::lease_validator::LeaseValidator;
use crate::pb::etcdserverpb::{
    compare::{CompareResult, CompareTarget, TargetUnion},
    request_op,
    response_op,
    Compare, DeleteRangeRequest, PutRequest, RangeRequest, RequestOp, ResponseHeader, ResponseOp,
    TxnRequest, TxnResponse,
};
use crate::pb::etcdserverpb::{range_request, DeleteRangeResponse, PutResponse, RangeResponse};
use crate::pb::mvccpb::{self, Event};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ValueEntry {
    pub value: Vec<u8>,
    pub create_rev: i64,
    pub mod_rev: i64,
    pub version: i64,
    pub lease: i64,
}

#[derive(Debug, Default, Clone, Serialize, Deserialize)]
pub struct KvState {
    pub revision: i64,
    pub compact_rev: i64,
    pub cache: BTreeMap<Vec<u8>, ValueEntry>,
}

#[derive(Default)]
struct Ongoing {
    keys: dashmap::DashMap<Vec<u8>, ()>,
}

impl Ongoing {
    async fn with_key<R>(&self, key: &[u8], fut: impl std::future::Future<Output = R>) -> R {
        let k = key.to_vec();
        loop {
            if self.keys.insert(k.clone(), ()).is_none() {
                let out = fut.await;
                self.keys.remove(&k);
                return out;
            }
            tokio::task::yield_now().await;
        }
    }
}

pub struct KvStore {
    state: Arc<RwLock<KvState>>,
    ongoing: Arc<Ongoing>,
    config: MetaStoreServerConfig,
}

impl KvStore {
    pub fn new(config: MetaStoreServerConfig) -> Self {
        Self {
            state: Arc::new(RwLock::new(KvState::default())),
            ongoing: Arc::new(Ongoing::default()),
            config,
        }
    }

    pub async fn get_state(&self) -> KvState {
        self.state.read().await.clone()
    }

    pub async fn set_state(&self, s: KvState) {
        *self.state.write().await = s;
    }

    pub fn config(&self) -> &MetaStoreServerConfig {
        &self.config
    }

    fn hdr(rev: i64, cfg: &MetaStoreServerConfig) -> ResponseHeader {
        ResponseHeader {
            cluster_id: cfg.cluster_id,
            member_id: cfg.member_id,
            revision: rev,
            raft_term: 1,
        }
    }

    pub async fn current_revision(&self) -> i64 {
        self.state.read().await.revision
    }

    pub async fn range(&self, req: RangeRequest) -> Result<RangeResponse, MetaStoreError> {
        let st = self.state.read().await;
        if req.revision > 0 && req.revision != st.revision {
            return Err(MetaStoreError::InvalidArgument(format!(
                "revision {} not available (head {})",
                req.revision, st.revision
            )));
        }
        let (start, end) = build_range_keys(&req);
        let mut kvs: Vec<mvccpb::KeyValue> = st
            .cache
            .iter()
            .filter(|(k, ent)| key_in_range(k, &start, &end) && filter_kv_by_revision(&req, ent))
            .map(|(k, ent)| kv_msg(k, ent))
            .collect();

        if req.count_only {
            return Ok(RangeResponse {
                header: Some(Self::hdr(st.revision, &self.config)),
                kvs: vec![],
                count: kvs.len() as i64,
                more: false,
            });
        }

        sort_kvs(
            &mut kvs,
            range_request::SortTarget::try_from(req.sort_target)
                .unwrap_or(range_request::SortTarget::Key),
            range_request::SortOrder::try_from(req.sort_order)
                .unwrap_or(range_request::SortOrder::None),
        );

        let more = if req.limit > 0 && (kvs.len() as i64) > req.limit {
            kvs.truncate(req.limit as usize);
            true
        } else {
            false
        };

        if req.keys_only {
            for kv in &mut kvs {
                kv.value.clear();
            }
        }

        Ok(RangeResponse {
            header: Some(Self::hdr(st.revision, &self.config)),
            kvs,
            count: 0,
            more,
        })
    }

    pub async fn put(
        &self,
        req: PutRequest,
        lease: Option<std::sync::Arc<dyn LeaseValidator>>,
    ) -> Result<(PutResponse, Vec<(Vec<u8>, Event)>), MetaStoreError> {
        if req.key.is_empty() {
            return Err(MetaStoreError::InvalidArgument("empty key".into()));
        }
        let key = req.key.clone();
        self.ongoing
            .with_key(&key, async {
                let mut st = self.state.write().await;
                Self::put_locked(&mut st, req, &self.config, lease.as_deref()).await
            })
            .await
    }

    async fn put_locked(
        st: &mut KvState,
        req: PutRequest,
        cfg: &MetaStoreServerConfig,
        lease: Option<&dyn LeaseValidator>,
    ) -> Result<(PutResponse, Vec<(Vec<u8>, Event)>), MetaStoreError> {
        let key = req.key.clone();
        let prev = st.cache.get(&key).cloned();
        if prev.is_none() && (req.ignore_value || req.ignore_lease) {
            return Err(MetaStoreError::InvalidArgument(
                "ignore_* on missing key".into(),
            ));
        }
        let req_lease = req.lease;
        if req_lease != 0 {
            let Some(l) = lease else {
                return Err(MetaStoreError::InvalidArgument(
                    "lease id set but no lease service".into(),
                ));
            };
            if !l.valid_lease(req_lease).await {
                return Err(MetaStoreError::InvalidArgument(format!(
                    "invalid lease {req_lease}"
                )));
            }
        }

        let value = if req.ignore_value {
            prev.as_ref().unwrap().value.clone()
        } else {
            req.value.clone()
        };
        let stored_lease = if req.ignore_lease {
            prev.as_ref().unwrap().lease
        } else {
            req_lease
        };

        st.revision += 1;
        let rev = st.revision;
        let (create_rev, version) = if let Some(p) = &prev {
            (p.create_rev, p.version + 1)
        } else {
            (rev, 1)
        };

        let ent = ValueEntry {
            value,
            create_rev,
            mod_rev: rev,
            version,
            lease: stored_lease,
        };
        let kv = kv_msg(&key, &ent);
        st.cache.insert(key.clone(), ent);

        let mut ev = Event {
            r#type: mvccpb::event::EventType::Put as i32,
            kv: Some(kv),
            prev_kv: None,
        };
        let prev_for_resp = if req.prev_kv {
            prev.as_ref().map(|p| kv_msg(&key, p))
        } else {
            None
        };

        if req.prev_kv {
            if let Some(ref p) = prev {
                ev.prev_kv = Some(kv_msg(&key, p));
            }
        }

        Ok((
            PutResponse {
                header: Some(Self::hdr(rev, cfg)),
                prev_kv: prev_for_resp,
            },
            vec![(key, ev)],
        ))
    }

    pub async fn delete_range(
        &self,
        req: DeleteRangeRequest,
    ) -> Result<(DeleteRangeResponse, Vec<(Vec<u8>, Event)>), MetaStoreError> {
        let mut st = self.state.write().await;
        let (start, end) = build_delete_range(&req);
        let keys: Vec<Vec<u8>> = st
            .cache
            .keys()
            .filter(|k| key_in_range(k, &start, &end))
            .cloned()
            .collect();
        if keys.is_empty() {
            return Ok((
                DeleteRangeResponse {
                    header: Some(Self::hdr(st.revision, &self.config)),
                    deleted: 0,
                    prev_kvs: vec![],
                },
                vec![],
            ));
        }

        st.revision += 1;
        let rev = st.revision;
        let mut prev_kvs = vec![];
        let mut events = vec![];

        for key in keys {
            if let Some(ent) = st.cache.remove(&key) {
                if req.prev_kv {
                    prev_kvs.push(kv_msg(&key, &ent));
                }
                let del_kv = mvccpb::KeyValue {
                    key: key.clone(),
                    create_revision: ent.create_rev,
                    mod_revision: rev,
                    version: 0,
                    value: vec![],
                    lease: 0,
                };
                events.push((
                    key,
                    Event {
                        r#type: mvccpb::event::EventType::Delete as i32,
                        kv: Some(del_kv),
                        prev_kv: None,
                    },
                ));
            }
        }

        Ok((
            DeleteRangeResponse {
                header: Some(Self::hdr(rev, &self.config)),
                deleted: events.len() as i64,
                prev_kvs,
            },
            events,
        ))
    }

    pub async fn compact(&self, revision: i64) -> Result<i64, MetaStoreError> {
        let mut st = self.state.write().await;
        if revision > st.revision {
            return Err(MetaStoreError::InvalidArgument(
                "compact revision beyond head".into(),
            ));
        }
        st.compact_rev = st.compact_rev.max(revision);
        Ok(st.revision)
    }

    pub async fn txn(
        &self,
        req: TxnRequest,
        lease: Option<std::sync::Arc<dyn LeaseValidator>>,
    ) -> Result<(TxnResponse, Vec<(Vec<u8>, Event)>), MetaStoreError> {
        let mut st = self.state.write().await;
        let compares_ok = req.compare.iter().all(|c| eval_compare(c, &st));
        let ops = if compares_ok {
            &req.success
        } else {
            &req.failure
        };

        txn_check_duplicate_keys(ops)?;

        let mut writes = false;
        for op in ops {
            if op_mutation(op) {
                writes = true;
                break;
            }
        }

        let mut events = vec![];
        let mut responses = vec![];

        if !writes {
            for op in ops {
                responses.push(exec_readonly_op(op, &st, &self.config)?);
            }
            return Ok((
                TxnResponse {
                    header: Some(Self::hdr(st.revision, &self.config)),
                    succeeded: compares_ok,
                    responses,
                },
                events,
            ));
        }

        st.revision += 1;
        let txn_rev = st.revision;

        for op in ops {
            let (resp, evs) =
                exec_op_in_txn(op, &mut st, txn_rev, &self.config, lease.as_deref()).await?;
            responses.push(resp);
            events.extend(evs);
        }

        Ok((
            TxnResponse {
                header: Some(Self::hdr(st.revision, &self.config)),
                succeeded: compares_ok,
                responses,
            },
            events,
        ))
    }
}

fn op_mutation(op: &RequestOp) -> bool {
    match &op.request {
        Some(request_op::Request::RequestPut(_))
        | Some(request_op::Request::RequestDeleteRange(_)) => true,
        Some(request_op::Request::RequestTxn(t)) => {
            t.success.iter().any(op_mutation) || t.failure.iter().any(op_mutation)
        }
        _ => false,
    }
}

fn txn_check_duplicate_keys(ops: &[RequestOp]) -> Result<(), MetaStoreError> {
    let keys = collect_put_keys(ops)?;
    let mut set = BTreeSet::new();
    for k in keys {
        if !set.insert(k) {
            return Err(MetaStoreError::InvalidArgument(
                "duplicate key in txn".into(),
            ));
        }
    }
    Ok(())
}

fn collect_put_keys(ops: &[RequestOp]) -> Result<Vec<Vec<u8>>, MetaStoreError> {
    let mut out = vec![];
    for o in ops {
        match &o.request {
            Some(request_op::Request::RequestPut(p)) => out.push(p.key.clone()),
            Some(request_op::Request::RequestDeleteRange(d)) => {
                if !d.range_end.is_empty() && d.range_end != vec![0] && d.range_end != get_prefix(&d.key)
                {
                    return Err(MetaStoreError::InvalidArgument(
                        "txn delete_range: only single key or prefix supported".into(),
                    ));
                }
                out.push(d.key.clone());
            }
            Some(request_op::Request::RequestTxn(t)) => {
                out.extend(collect_put_keys(&t.success)?);
                out.extend(collect_put_keys(&t.failure)?);
            }
            _ => {}
        }
    }
    Ok(out)
}

fn exec_readonly_op(
    op: &RequestOp,
    st: &KvState,
    cfg: &MetaStoreServerConfig,
) -> Result<ResponseOp, MetaStoreError> {
    match &op.request {
        Some(request_op::Request::RequestRange(r)) => {
            let (start, end) = build_range_keys(r);
            let kvs: Vec<_> = st
                .cache
                .iter()
                .filter(|(k, ent)| key_in_range(k, &start, &end) && filter_kv_by_revision(r, ent))
                .map(|(k, e)| kv_msg(k, e))
                .collect();
            Ok(ResponseOp {
                response: Some(response_op::Response::ResponseRange(RangeResponse {
                    header: Some(KvStore::hdr(st.revision, cfg)),
                    kvs,
                    count: 0,
                    more: false,
                })),
            })
        }
        Some(request_op::Request::RequestTxn(t)) => {
            let compares_ok = t.compare.iter().all(|c| eval_compare(c, st));
            let branch = if compares_ok {
                &t.success
            } else {
                &t.failure
            };
            txn_check_duplicate_keys(branch)?;
            let mut writes = false;
            for o in branch {
                if op_mutation(o) {
                    writes = true;
                    break;
                }
            }
            if writes {
                return Err(MetaStoreError::InvalidArgument(
                    "nested write txn not supported in readonly branch".into(),
                ));
            }
            let mut inner_resp = vec![];
            for o in branch {
                inner_resp.push(exec_readonly_op(o, st, cfg)?);
            }
            Ok(ResponseOp {
                response: Some(response_op::Response::ResponseTxn(TxnResponse {
                    header: Some(KvStore::hdr(st.revision, cfg)),
                    succeeded: compares_ok,
                    responses: inner_resp,
                })),
            })
        }
        _ => Err(MetaStoreError::InvalidArgument(
            "unsupported readonly txn op".into(),
        )),
    }
}

async fn exec_op_in_txn(
    op: &RequestOp,
    st: &mut KvState,
    txn_rev: i64,
    cfg: &MetaStoreServerConfig,
    lease: Option<&dyn LeaseValidator>,
) -> Result<(ResponseOp, Vec<(Vec<u8>, Event)>), MetaStoreError> {
    match &op.request {
        Some(request_op::Request::RequestRange(r)) => {
            let (start, end) = build_range_keys(r);
            let kvs: Vec<_> = st
                .cache
                .iter()
                .filter(|(k, ent)| key_in_range(k, &start, &end) && filter_kv_by_revision(r, ent))
                .map(|(k, e)| kv_msg(k, e))
                .collect();
            Ok((
                ResponseOp {
                    response: Some(response_op::Response::ResponseRange(RangeResponse {
                        header: Some(KvStore::hdr(st.revision, cfg)),
                        kvs,
                        count: 0,
                        more: false,
                    })),
                },
                vec![],
            ))
        }
        Some(request_op::Request::RequestPut(p)) => {
            // Use txn_rev for this mutation (etcd: all ops share txn revision)
            let key = p.key.clone();
            let prev = st.cache.get(&key).cloned();
            if prev.is_none() && (p.ignore_value || p.ignore_lease) {
                return Err(MetaStoreError::InvalidArgument(
                    "ignore_* on missing key".into(),
                ));
            }
            if p.lease != 0 {
                let Some(l) = lease else {
                    return Err(MetaStoreError::InvalidArgument(
                        "lease id set but no lease service".into(),
                    ));
                };
                if !l.valid_lease(p.lease).await {
                    return Err(MetaStoreError::InvalidArgument("invalid lease".into()));
                }
            }
            let value = if p.ignore_value {
                prev.as_ref().unwrap().value.clone()
            } else {
                p.value.clone()
            };
            let lease_id = if p.ignore_lease {
                prev.as_ref().unwrap().lease
            } else {
                p.lease
            };
            let (create_rev, version) = if let Some(pr) = &prev {
                (pr.create_rev, pr.version + 1)
            } else {
                (txn_rev, 1)
            };
            let ent = ValueEntry {
                value,
                create_rev,
                mod_rev: txn_rev,
                version,
                lease: lease_id,
            };
            let kv = kv_msg(&key, &ent);
            st.cache.insert(key.clone(), ent);
            let mut ev = Event {
                r#type: mvccpb::event::EventType::Put as i32,
                kv: Some(kv),
                prev_kv: None,
            };
            let prev_put = if p.prev_kv {
                prev.as_ref().map(|pr| kv_msg(&key, pr))
            } else {
                None
            };
            if p.prev_kv {
                if let Some(ref pr) = prev {
                    ev.prev_kv = Some(kv_msg(&key, pr));
                }
            }
            Ok((
                ResponseOp {
                    response: Some(response_op::Response::ResponsePut(PutResponse {
                        header: Some(KvStore::hdr(st.revision, cfg)),
                        prev_kv: prev_put,
                    })),
                },
                vec![(key, ev)],
            ))
        }
        Some(request_op::Request::RequestDeleteRange(d)) => {
            let (start, end) = build_delete_range(d);
            let keys: Vec<Vec<u8>> = st
                .cache
                .keys()
                .filter(|k| key_in_range(k, &start, &end))
                .cloned()
                .collect();
            let mut prev_kvs = vec![];
            let mut events = vec![];
            for key in keys {
                if let Some(ent) = st.cache.remove(&key) {
                    if d.prev_kv {
                        prev_kvs.push(kv_msg(&key, &ent));
                    }
                    let del_kv = mvccpb::KeyValue {
                        key: key.clone(),
                        create_revision: ent.create_rev,
                        mod_revision: txn_rev,
                        version: 0,
                        value: vec![],
                        lease: 0,
                    };
                    events.push((
                        key,
                        Event {
                            r#type: mvccpb::event::EventType::Delete as i32,
                            kv: Some(del_kv),
                            prev_kv: None,
                        },
                    ));
                }
            }
            Ok((
                ResponseOp {
                    response: Some(response_op::Response::ResponseDeleteRange(
                        DeleteRangeResponse {
                            header: Some(KvStore::hdr(st.revision, cfg)),
                            deleted: events.len() as i64,
                            prev_kvs,
                        },
                    )),
                },
                events,
            ))
        }
        Some(request_op::Request::RequestTxn(_t)) => Err(MetaStoreError::InvalidArgument(
            "nested write txn not supported".into(),
        )),
        None => Err(MetaStoreError::InvalidArgument("empty txn op".into())),
    }
}

fn kv_msg(key: &[u8], ent: &ValueEntry) -> mvccpb::KeyValue {
    mvccpb::KeyValue {
        key: key.to_vec(),
        create_revision: ent.create_rev,
        mod_revision: ent.mod_rev,
        version: ent.version,
        value: ent.value.clone(),
        lease: ent.lease,
    }
}

fn filter_kv_by_revision(req: &RangeRequest, ent: &ValueEntry) -> bool {
    if req.min_mod_revision > 0 && ent.mod_rev < req.min_mod_revision {
        return false;
    }
    if req.max_mod_revision > 0 && ent.mod_rev > req.max_mod_revision {
        return false;
    }
    if req.min_create_revision > 0 && ent.create_rev < req.min_create_revision {
        return false;
    }
    if req.max_create_revision > 0 && ent.create_rev > req.max_create_revision {
        return false;
    }
    true
}

fn sort_kvs(kvs: &mut [mvccpb::KeyValue], target: range_request::SortTarget, order: range_request::SortOrder) {
    use range_request::{SortOrder, SortTarget};
    let desc = order == SortOrder::Descend;
    kvs.sort_by(|a, b| {
        let cmp = match target {
            SortTarget::Key => a.key.cmp(&b.key),
            SortTarget::Mod => a.mod_revision.cmp(&b.mod_revision),
            SortTarget::Version => a.version.cmp(&b.version),
            SortTarget::Create => a.create_revision.cmp(&b.create_revision),
            SortTarget::Value => a.value.cmp(&b.value),
        };
        if desc {
            cmp.reverse()
        } else {
            cmp
        }
    });
}

pub(crate) fn get_prefix(key: &[u8]) -> Vec<u8> {
    for (i, v) in key.iter().enumerate().rev() {
        if *v < 0xFF {
            let mut end = key[..=i].to_vec();
            end[i] = *v + 1;
            return end;
        }
    }
    vec![0]
}

fn build_range_keys(req: &RangeRequest) -> (Vec<u8>, Vec<u8>) {
    let mut key = req.key.clone();
    let mut end = req.range_end.clone();
    if key.is_empty() && end.is_empty() {
        key = vec![0];
        end = vec![0];
    } else if !key.is_empty() && end.is_empty() {
        // single key: end is empty means only `key`
        return (key, vec![]);
    } else if end == vec![0] {
        if key.is_empty() {
            key = vec![0];
        }
        end = vec![0];
    } else if !key.is_empty() && !end.is_empty() && end == get_prefix(&key) {
        // prefix range [key, key+1)
    }
    (key, end)
}

pub(crate) fn build_delete_range(req: &DeleteRangeRequest) -> (Vec<u8>, Vec<u8>) {
    let mut key = req.key.clone();
    let mut end = req.range_end.clone();
    if key.is_empty() && end.is_empty() {
        key = vec![0];
        end = vec![0];
    } else if !key.is_empty() && end.is_empty() {
        return (key, vec![]);
    } else if end == vec![0] {
        if key.is_empty() {
            key = vec![0];
        }
        end = vec![0];
    }
    (key, end)
}

pub(crate) fn key_in_range(key: &[u8], start: &[u8], end: &[u8]) -> bool {
    if key < start {
        return false;
    }
    if end.is_empty() {
        return key == start;
    }
    if end == [0] {
        return true;
    }
    key < end
}

fn eval_compare(c: &Compare, st: &KvState) -> bool {
    if !c.range_end.is_empty() {
        return false;
    }
    let key = &c.key;
    let entry = st.cache.get(key);
    let Ok(target) = CompareTarget::try_from(c.target) else {
        return false;
    };
    let Ok(result) = CompareResult::try_from(c.result) else {
        return false;
    };
    let Some(tu) = &c.target_union else {
        return false;
    };
    match target {
        CompareTarget::Version => {
            let lhs = entry.map(|e| e.version).unwrap_or(0);
            let TargetUnion::Version(rhs) = tu else {
                return false;
            };
            cmp_i64(result, lhs, *rhs)
        }
        CompareTarget::Mod => {
            let lhs = entry.map(|e| e.mod_rev).unwrap_or(0);
            let TargetUnion::ModRevision(rhs) = tu else {
                return false;
            };
            cmp_i64(result, lhs, *rhs)
        }
        CompareTarget::Create => {
            let lhs = entry.map(|e| e.create_rev).unwrap_or(0);
            let TargetUnion::CreateRevision(rhs) = tu else {
                return false;
            };
            cmp_i64(result, lhs, *rhs)
        }
        CompareTarget::Value => {
            let lhs = entry.map(|e| e.value.as_slice());
            let TargetUnion::Value(rhs) = tu else {
                return false;
            };
            cmp_bytes(result, lhs, Some(rhs.as_slice()))
        }
        CompareTarget::Lease => {
            let lhs = entry.map(|e| e.lease).unwrap_or(0);
            let TargetUnion::Lease(rhs) = tu else {
                return false;
            };
            cmp_i64(result, lhs, *rhs)
        }
    }
}

fn cmp_i64(op: CompareResult, a: i64, b: i64) -> bool {
    use CompareResult::*;
    match op {
        Equal => a == b,
        Greater => a > b,
        Less => a < b,
        NotEqual => a != b,
    }
}

fn cmp_bytes(op: CompareResult, a: Option<&[u8]>, b: Option<&[u8]>) -> bool {
    use CompareResult::*;
    let ord = match (a, b) {
        (Some(x), Some(y)) => x.cmp(y),
        (None, None) => std::cmp::Ordering::Equal,
        (None, Some(_)) => std::cmp::Ordering::Less,
        (Some(_), None) => std::cmp::Ordering::Greater,
    };
    match op {
        Equal => ord == std::cmp::Ordering::Equal,
        Greater => ord == std::cmp::Ordering::Greater,
        Less => ord == std::cmp::Ordering::Less,
        NotEqual => ord != std::cmp::Ordering::Equal,
    }
}

