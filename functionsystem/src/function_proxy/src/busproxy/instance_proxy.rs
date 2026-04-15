//! Per-instance bookkeeping for the data plane (runtime stream + dispatcher).

use super::request_dispatcher::RequestDispatcher;
use std::sync::Arc;

#[derive(Debug)]
pub struct InstanceProxy {
    pub instance_id: String,
    pub dispatcher: Arc<RequestDispatcher>,
}

impl InstanceProxy {
    pub fn new(instance_id: String) -> Self {
        Self {
            instance_id,
            dispatcher: Arc::new(RequestDispatcher::default()),
        }
    }
}
