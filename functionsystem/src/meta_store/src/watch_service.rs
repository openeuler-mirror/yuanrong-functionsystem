//! Watch subscriptions; outbound channel carries `Result` for tonic stream compatibility.

use std::collections::HashMap;
use std::sync::atomic::{AtomicI64, Ordering};
use std::sync::Arc;

use dashmap::DashMap;
use parking_lot::Mutex;
use tokio::sync::mpsc;
use tonic::Status;

use crate::kv_store;
use crate::pb::etcdserverpb::{watch_create_request, WatchCreateRequest, WatchResponse};
use crate::pb::etcdserverpb::ResponseHeader;
use crate::pb::mvccpb::Event;

const COALESCE_THRESHOLD: usize = 1024;

#[derive(Clone, Default)]
pub struct WatchHub {
    next_stream: Arc<AtomicI64>,
    watchers: Arc<DashMap<(i64, i64), Arc<WatchSlot>>>,
}

struct WatchSlot {
    key: Vec<u8>,
    range_end: Vec<u8>,
    prev_kv: bool,
    filters: Vec<i32>,
    buffer: Mutex<Vec<Event>>,
    out: mpsc::Sender<Result<WatchResponse, Status>>,
}

impl WatchHub {
    pub fn new() -> Self {
        Self {
            next_stream: Arc::new(AtomicI64::new(1)),
            watchers: Arc::new(DashMap::new()),
        }
    }

    pub fn next_stream_id(&self) -> i64 {
        self.next_stream.fetch_add(1, Ordering::SeqCst)
    }

    pub fn add_watcher(
        &self,
        stream_id: i64,
        watch_id: i64,
        req: WatchCreateRequest,
        out: mpsc::Sender<Result<WatchResponse, Status>>,
    ) {
        let slot = Arc::new(WatchSlot {
            key: req.key.clone(),
            range_end: req.range_end.clone(),
            prev_kv: req.prev_kv,
            filters: req.filters.clone(),
            buffer: Mutex::new(vec![]),
            out,
        });
        self.watchers.insert((stream_id, watch_id), slot);
    }

    pub fn remove_watcher(&self, stream_id: i64, watch_id: i64) {
        self.watchers.remove(&(stream_id, watch_id));
    }

    pub fn remove_stream(&self, stream_id: i64) {
        let keys: Vec<_> = self
            .watchers
            .iter()
            .filter(|e| e.key().0 == stream_id)
            .map(|e| *e.key())
            .collect();
        for k in keys {
            self.watchers.remove(&k);
        }
    }

    pub fn publish(&self, key: &[u8], mut ev: Event, header: ResponseHeader) {
        for e in self.watchers.iter() {
            let (_sid, wid) = *e.key();
            let slot = e.value();
            if !kv_store::key_in_range(key, &slot.key, &slot.range_end) {
                continue;
            }
            if filter_event(&slot.filters, &ev) {
                continue;
            }
            if !slot.prev_kv {
                ev.prev_kv = None;
            }
            let wr = WatchResponse {
                header: Some(header.clone()),
                watch_id: wid,
                created: false,
                canceled: false,
                compact_revision: 0,
                cancel_reason: String::new(),
                fragment: false,
                events: vec![ev.clone()],
            };
            if slot.out.try_send(Ok(wr)).is_err() {
                let mut buf = slot.buffer.lock();
                buf.push(ev.clone());
                if buf.len() > COALESCE_THRESHOLD {
                    let mut m: HashMap<Vec<u8>, Event> = HashMap::new();
                    for old in buf.drain(..) {
                        if let Some(kv) = old.kv.as_ref() {
                            m.insert(kv.key.clone(), old);
                        }
                    }
                    buf.extend(m.into_values());
                }
            }
        }
    }

    pub fn flush_buffered(&self, stream_id: i64, header: &ResponseHeader) {
        for e in self.watchers.iter() {
            if e.key().0 != stream_id {
                continue;
            }
            let (_sid, wid) = *e.key();
            let slot = e.value();
            let mut buf = slot.buffer.lock();
            if buf.is_empty() {
                continue;
            }
            let events: Vec<_> = buf.drain(..).collect();
            drop(buf);
            let wr = WatchResponse {
                header: Some(header.clone()),
                watch_id: wid,
                created: false,
                canceled: false,
                compact_revision: 0,
                cancel_reason: String::new(),
                fragment: false,
                events,
            };
            let _ = slot.out.try_send(Ok(wr));
        }
    }
}

fn filter_event(filters: &[i32], ev: &Event) -> bool {
    use watch_create_request::FilterType;
    for f in filters {
        let Ok(ft) = FilterType::try_from(*f) else {
            continue;
        };
        match ft {
            FilterType::Noput => {
                if ev.r#type == crate::pb::mvccpb::event::EventType::Put as i32 {
                    return true;
                }
            }
            FilterType::Nodelete => {
                if ev.r#type == crate::pb::mvccpb::event::EventType::Delete as i32 {
                    return true;
                }
            }
        }
    }
    false
}
