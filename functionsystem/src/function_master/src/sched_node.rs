//! `SchedNode` — mirrors C++ `function_master/common/scheduler_topology/sched_node.{h,cpp}`.

use std::collections::HashMap;
use std::sync::{Arc, Weak};

use parking_lot::RwLock;

/// C++ `NodeState`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub enum NodeState {
    #[default]
    Connected,
    Broken,
}

/// C++ `NodeInfo`.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct NodeInfo {
    pub name: String,
    pub address: String,
}

pub(crate) struct SchedNodeInner {
    node_info: RwLock<NodeInfo>,
    level: i32,
    state: RwLock<NodeState>,
    parent: RwLock<Option<Weak<SchedNodeInner>>>,
    children: RwLock<HashMap<String, SchedNode>>,
}

/// Reference-counted scheduler tree node (`std::shared_ptr<SchedNode>` analogue).
#[derive(Clone)]
pub struct SchedNode(pub(crate) Arc<SchedNodeInner>);

impl SchedNode {
    pub(crate) fn new(info: NodeInfo, level: i32) -> Self {
        Self(Arc::new(SchedNodeInner {
            node_info: RwLock::new(info),
            level,
            state: RwLock::new(NodeState::Connected),
            parent: RwLock::new(None),
            children: RwLock::new(HashMap::new()),
        }))
    }

    pub fn name(&self) -> String {
        self.0.node_info.read().name.clone()
    }

    pub fn address(&self) -> String {
        self.0.node_info.read().address.clone()
    }

    pub fn node_info(&self) -> NodeInfo {
        self.0.node_info.read().clone()
    }

    pub fn set_node_info(&self, info: NodeInfo) {
        *self.0.node_info.write() = info;
    }

    pub fn level(&self) -> i32 {
        self.0.level
    }

    pub fn is_leaf(&self) -> bool {
        self.0.level == 0
    }

    pub fn set_state(&self, s: NodeState) {
        *self.0.state.write() = s;
    }

    pub fn state(&self) -> NodeState {
        *self.0.state.read()
    }

    pub fn parent(&self) -> Option<SchedNode> {
        self.0
            .parent
            .read()
            .as_ref()
            .and_then(|w| w.upgrade())
            .map(SchedNode)
    }

    pub fn children(&self) -> HashMap<String, SchedNode> {
        self.0.children.read().clone()
    }

    pub fn child_count(&self) -> usize {
        self.0.children.read().len()
    }

    /// C++ `CheckAddNonLeafNode`: parent level must be **> 1** and under capacity.
    pub fn check_add_non_leaf(&self, max_children: usize) -> bool {
        self.0.level > 1 && self.child_count() < max_children
    }

    /// C++ `CheckAddLeafNode`: parent must be **level == 1** and under capacity.
    pub fn check_add_leaf(&self, max_children: usize) -> bool {
        self.0.level == 1 && self.child_count() < max_children
    }

    pub(crate) fn add_child(&self, child: &SchedNode) {
        self.0
            .children
            .write()
            .insert(child.name(), child.clone());
        *child.0.parent.write() = Some(Arc::downgrade(&self.0));
    }

    pub(crate) fn remove_child_named(&self, name: &str) {
        self.0.children.write().remove(name);
    }

    pub(crate) fn clear_parent(&self) {
        *self.0.parent.write() = None;
    }
}
