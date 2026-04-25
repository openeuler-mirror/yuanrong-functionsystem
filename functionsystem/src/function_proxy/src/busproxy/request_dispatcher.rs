//! Queues forwarded calls until an instance route (and optional runtime stream) is ready.

use parking_lot::Mutex;
use std::collections::VecDeque;
use yr_proto::inner_service::ForwardCallRequest;

#[derive(Debug)]
pub struct PendingForward {
    pub req: ForwardCallRequest,
    pub seq_no: Option<i64>,
}

#[derive(Debug, Default)]
pub struct RequestDispatcher {
    pending: Mutex<VecDeque<PendingForward>>,
    route_ready: Mutex<bool>,
}

impl RequestDispatcher {
    pub fn set_route_ready(&self, ready: bool) {
        *self.route_ready.lock() = ready;
    }

    pub fn route_ready(&self) -> bool {
        *self.route_ready.lock()
    }

    pub fn enqueue(&self, p: PendingForward) {
        let mut q = self.pending.lock();
        if let Some(seq) = p.seq_no {
            let idx = q
                .iter()
                .position(|existing| existing.seq_no.unwrap_or(i64::MAX) > seq)
                .unwrap_or(q.len());
            q.insert(idx, p);
        } else {
            q.push_back(p);
        }
    }

    pub fn drain(&self) -> VecDeque<PendingForward> {
        std::mem::take(&mut *self.pending.lock())
    }

    pub fn pop_front(&self) -> Option<PendingForward> {
        self.pending.lock().pop_front()
    }

    pub fn is_empty(&self) -> bool {
        self.pending.lock().is_empty()
    }

    pub fn front_seq_no(&self) -> Option<i64> {
        self.pending.lock().front().and_then(|p| p.seq_no)
    }
}
