//! Queues forwarded calls until an instance route (and optional runtime stream) is ready.

use parking_lot::Mutex;
use std::collections::VecDeque;
use yr_proto::inner_service::ForwardCallRequest;

#[derive(Debug)]
pub struct PendingForward {
    pub req: ForwardCallRequest,
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
        self.pending.lock().push_back(p);
    }

    pub fn drain(&self) -> VecDeque<PendingForward> {
        std::mem::take(&mut *self.pending.lock())
    }
}
