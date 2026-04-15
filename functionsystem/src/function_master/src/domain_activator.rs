//! Domain process activation when no parent capacity (`domain_activator*.cpp` sketch).

use std::collections::VecDeque;
use std::sync::Arc;

use parking_lot::Mutex;
use tracing::info;

use crate::sched_node::NodeInfo;
use crate::sched_tree::SchedTree;

/// Locals waiting for a new domain when `SchedTree::add_leaf_node` returns `None`.
pub struct DomainActivator {
    pending_locals: Mutex<VecDeque<NodeInfo>>,
    tree: Arc<SchedTree>,
}

impl DomainActivator {
    pub fn new(tree: Arc<SchedTree>) -> Self {
        Self {
            pending_locals: Mutex::new(VecDeque::new()),
            tree,
        }
    }

    pub fn cache_local_waiting_domain(&self, info: NodeInfo) {
        info!(name = %info.name, "domain_activator: cache local pending new domain");
        self.pending_locals.lock().push_back(info);
    }

    /// After a replacement domain registers, drain cached locals into the tree.
    pub fn drain_pending_after_domain_change(&self) -> Vec<NodeInfo> {
        let mut q = self.pending_locals.lock();
        let mut out = Vec::new();
        while let Some(info) = q.pop_front() {
            if self.tree.add_leaf_node(info.clone()).is_some() {
                out.push(info);
            } else {
                q.push_front(info);
                break;
            }
        }
        out
    }

    pub fn pending_len(&self) -> usize {
        self.pending_locals.lock().len()
    }
}
