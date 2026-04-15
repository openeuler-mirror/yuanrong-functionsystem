//! Scheduler topology tree ported from `functionsystem/src/common/scheduler_topology/`.
//!
//! [`NodeState`] uses `Ready` / `NotReady` / `Unknown` for Rust-side health modeling.
//! Serialization uses JSON with the same logical fields as `messages.SchedulerNode` (name, address, level, children)
//! plus node state and optional resource metadata.

use crate::resource_view::ResourceMaps;
use crate::status::{Status, StatusCode};
use dashmap::DashMap;
use parking_lot::{Mutex, RwLock};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::sync::{Arc, Weak};

const MIN_TREE_LEVEL: usize = 2;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, Serialize, Deserialize, Default)]
#[serde(rename_all = "snake_case")]
pub enum NodeState {
    Ready,
    NotReady,
    #[default]
    Unknown,
}

/// Runtime node metadata including resource maps (scalar / set / vector) used by scheduling.
#[derive(Debug)]
pub struct NodeInfo {
    pub name: String,
    pub address: String,
    pub resources: Arc<RwLock<ResourceMaps>>,
    pub labels: DashMap<String, String>,
    pub annotations: DashMap<String, String>,
}

impl Clone for NodeInfo {
    fn clone(&self) -> Self {
        let out = Self {
            name: self.name.clone(),
            address: self.address.clone(),
            resources: Arc::new(RwLock::new(self.resources.read().clone())),
            labels: DashMap::new(),
            annotations: DashMap::new(),
        };
        for e in self.labels.iter() {
            out.labels.insert(e.key().clone(), e.value().clone());
        }
        for e in self.annotations.iter() {
            out.annotations.insert(e.key().clone(), e.value().clone());
        }
        out
    }
}

impl NodeInfo {
    pub fn new(name: impl Into<String>, address: impl Into<String>) -> Self {
        Self {
            name: name.into(),
            address: address.into(),
            resources: Arc::new(RwLock::new(ResourceMaps::default())),
            labels: DashMap::new(),
            annotations: DashMap::new(),
        }
    }
}

/// Basic tree-node operations (abstract `Node` in C++).
///
/// Parent/child links are established with [`link_parent_child`]; `Arc<SchedNode>` is required
/// for the C++ `shared_from_this` pattern.
pub trait Node: Send + Sync {
    fn set_state(&self, state: NodeState);
    fn get_state(&self) -> NodeState;
    fn get_node_info(&self) -> NodeInfo;
    fn get_parent(&self) -> Option<Arc<SchedNode>>;
    fn get_children(&self) -> Vec<Arc<SchedNode>>;
    fn is_leaf(&self) -> bool;
    fn remove_child(&self, name: &str);
    fn set_node_info(&self, info: NodeInfo);
    fn check_add_non_leaf_node(&self, max_children_num: usize) -> bool;
    fn check_add_leaf_node(&self, max_children_num: usize) -> bool;
    fn get_level(&self) -> i32;
}

/// Scheduling-aware node (`SchedNode` in C++).
pub struct SchedNode {
    info: RwLock<NodeInfo>,
    state: RwLock<NodeState>,
    parent: RwLock<Option<Weak<SchedNode>>>,
    children: DashMap<String, Arc<SchedNode>>,
    level: i32,
}

impl std::fmt::Debug for SchedNode {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("SchedNode")
            .field("level", &self.level)
            .field("name", &self.info.read().name)
            .finish_non_exhaustive()
    }
}

impl SchedNode {
    pub fn new(node_info: NodeInfo, level: i32) -> Arc<Self> {
        Arc::new(Self {
            info: RwLock::new(node_info),
            state: RwLock::new(NodeState::Unknown),
            parent: RwLock::new(None),
            children: DashMap::new(),
            level,
        })
    }

    pub fn name(&self) -> String {
        self.info.read().name.clone()
    }
}

impl Node for SchedNode {
    fn set_state(&self, state: NodeState) {
        *self.state.write() = state;
    }

    fn get_state(&self) -> NodeState {
        *self.state.read()
    }

    fn get_node_info(&self) -> NodeInfo {
        self.info.read().clone()
    }

    fn get_parent(&self) -> Option<Arc<SchedNode>> {
        self.parent
            .read()
            .as_ref()
            .and_then(|w| w.upgrade())
    }

    fn get_children(&self) -> Vec<Arc<SchedNode>> {
        self.children.iter().map(|e| e.value().clone()).collect()
    }

    fn is_leaf(&self) -> bool {
        self.level == 0
    }

    fn remove_child(&self, name: &str) {
        self.children.remove(name);
    }

    fn set_node_info(&self, node_info: NodeInfo) {
        *self.info.write() = node_info;
    }

    fn check_add_non_leaf_node(&self, max_children_num: usize) -> bool {
        self.level > 1 && self.children.len() < max_children_num
    }

    fn check_add_leaf_node(&self, max_children_num: usize) -> bool {
        self.level == 1 && self.children.len() < max_children_num
    }

    fn get_level(&self) -> i32 {
        self.level
    }
}

/// Attach `child` to `parent` (C++ `SchedNode::AddChild` + `shared_from_this`).
pub fn link_parent_child(parent: &Arc<SchedNode>, child: &Arc<SchedNode>) {
    let name = child.name();
    parent.children.insert(name, Arc::clone(child));
    *child.parent.write() = Some(Arc::downgrade(parent));
}

#[derive(Debug, thiserror::Error)]
pub enum TopologyError {
    #[error("JSON error: {0}")]
    Json(#[from] serde_json::Error),
    #[error("invalid topology")]
    Invalid,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct NodeWire {
    name: String,
    address: String,
    level: i32,
    #[serde(default)]
    state: NodeState,
    #[serde(default)]
    scalars: HashMap<String, (f64, f64)>,
    #[serde(default)]
    sets: HashMap<String, Vec<String>>,
    #[serde(default)]
    vectors: HashMap<String, Vec<String>>,
    #[serde(default)]
    labels: HashMap<String, String>,
    #[serde(default)]
    annotations: HashMap<String, String>,
    #[serde(default)]
    children: Vec<NodeWire>,
}

impl NodeWire {
    fn from_node(n: &SchedNode) -> Self {
        let info = n.info.read();
        let mut scalars = HashMap::new();
        let mut sets = HashMap::new();
        let mut vectors = HashMap::new();
        let res = info.resources.read();
        for e in res.scalar.iter() {
            scalars.insert(e.key().clone(), *e.value());
        }
        for e in res.sets.iter() {
            sets.insert(e.key().clone(), e.value().iter().cloned().collect());
        }
        for e in res.vectors.iter() {
            vectors.insert(e.key().clone(), e.value().clone());
        }
        let labels: HashMap<_, _> = info.labels.iter().map(|e| (e.key().clone(), e.value().clone())).collect();
        let annotations: HashMap<_, _> = info
            .annotations
            .iter()
            .map(|e| (e.key().clone(), e.value().clone()))
            .collect();
        let children: Vec<_> = n
            .children
            .iter()
            .map(|e| NodeWire::from_node(e.value()))
            .collect();
        Self {
            name: info.name.clone(),
            address: info.address.clone(),
            level: n.level,
            state: *n.state.read(),
            scalars,
            sets,
            vectors,
            labels,
            annotations,
            children,
        }
    }

    fn build_tree(self) -> Result<Arc<SchedNode>, TopologyError> {
        let node = SchedNode::new(empty_info_from_wire(&self)?, self.level);
        *node.state.write() = self.state;
        for w in self.children {
            let c = w.build_tree()?;
            link_parent_child(&node, &c);
        }
        Ok(node)
    }
}

fn empty_info_from_wire(w: &NodeWire) -> Result<NodeInfo, TopologyError> {
    if w.name.is_empty() {
        return Err(TopologyError::Invalid);
    }
    let info = NodeInfo::new(w.name.clone(), w.address.clone());
    let res = info.resources.write();
    for (k, v) in &w.scalars {
        res.scalar.insert(k.clone(), *v);
    }
    for (k, v) in &w.sets {
        res.sets.insert(k.clone(), v.iter().cloned().collect());
    }
    for (k, v) in &w.vectors {
        res.vectors.insert(k.clone(), v.clone());
    }
    drop(res);
    for (k, v) in &w.labels {
        info.labels.insert(k.clone(), v.clone());
    }
    for (k, v) in &w.annotations {
        info.annotations.insert(k.clone(), v.clone());
    }
    Ok(info)
}

/// Hierarchical scheduling tree (`SchedTree` in C++).
pub struct SchedTree {
    level_nodes: Mutex<Vec<DashMap<String, Arc<SchedNode>>>>,
    next_parent: Mutex<Option<Arc<SchedNode>>>,
    max_local_sched_per_domain_node: usize,
    max_domain_sched_per_domain_node: usize,
}

impl std::fmt::Debug for SchedTree {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("SchedTree")
            .field(
                "levels",
                &self.level_nodes.lock().len(),
            )
            .finish_non_exhaustive()
    }
}

impl Default for SchedTree {
    fn default() -> Self {
        Self::new()
    }
}

impl SchedTree {
    pub fn new() -> Self {
        Self {
            level_nodes: Mutex::new(Vec::new()),
            next_parent: Mutex::new(None),
            max_local_sched_per_domain_node: 0,
            max_domain_sched_per_domain_node: 0,
        }
    }

    pub fn with_limits(max_local: usize, max_domain: usize) -> Self {
        Self {
            level_nodes: Mutex::new(Vec::new()),
            next_parent: Mutex::new(None),
            max_local_sched_per_domain_node: max_local,
            max_domain_sched_per_domain_node: max_domain,
        }
    }

    fn add_node(&self, node_info: NodeInfo, level: usize) -> Arc<SchedNode> {
        let node = SchedNode::new(node_info, level as i32);
        let mut lv = self.level_nodes.lock();
        if lv.len() < level + 1 {
            lv.resize_with(level + 1, DashMap::new);
        }
        let name = node.name();
        lv[level].insert(name, Arc::clone(&node));
        node
    }

    pub fn get_root_node(&self) -> Option<Arc<SchedNode>> {
        let lv = self.level_nodes.lock();
        if lv.len() < MIN_TREE_LEVEL {
            return None;
        }
        lv.last().and_then(|m| m.iter().next().map(|e| e.value().clone()))
    }

    pub fn find_nodes(&self, level: u64) -> HashMap<String, Arc<SchedNode>> {
        let lv = self.level_nodes.lock();
        if (level as usize) >= lv.len() {
            return HashMap::new();
        }
        lv[level as usize]
            .iter()
            .map(|e| (e.key().clone(), e.value().clone()))
            .collect()
    }

    pub fn set_state(&self, node: &Arc<SchedNode>, state: NodeState) {
        node.set_state(state);
    }

    pub fn replace_non_leaf_node(&self, replaced: &str, node_info: NodeInfo) -> Option<Arc<SchedNode>> {
        let lv = self.level_nodes.lock();
        if lv.len() < MIN_TREE_LEVEL {
            return None;
        }
        for i in (1..lv.len()).rev() {
            if let Some(n) = lv[i].get(replaced) {
                let n = n.value().clone();
                if n.get_state() != NodeState::NotReady {
                    return None;
                }
                n.set_node_info(node_info);
                n.set_state(NodeState::Ready);
                return Some(n);
            }
        }
        None
    }
}

/// [`Tree`] trait — high-level operations (`add_leaf`, `remove_leaf`, `find`, `walk`).
pub trait Tree {
    fn add_leaf(&mut self, info: NodeInfo) -> Option<Arc<SchedNode>>;
    fn remove_leaf(&mut self, name: &str) -> Option<Arc<SchedNode>>;
    fn find(&self, name: &str) -> Option<Arc<SchedNode>>;
    fn walk(&self, visitor: &mut dyn FnMut(&Arc<SchedNode>));
    fn add_non_leaf(&mut self, info: NodeInfo) -> Option<Arc<SchedNode>>;
    fn get_root(&self) -> Option<Arc<SchedNode>>;
    fn serialize_tree_state(&self) -> Result<String, TopologyError>;
    fn deserialize_tree_state(&mut self, data: &str) -> Result<Status, TopologyError>;
}

impl Tree for SchedTree {
    fn add_leaf(&mut self, info: NodeInfo) -> Option<Arc<SchedNode>> {
        let lv = self.level_nodes.lock();
        if lv.len() < MIN_TREE_LEVEL {
            return None;
        }
        drop(lv);
        if let Some(existing) = self.find_leaf_node(&info.name) {
            existing.set_node_info(info);
            return Some(existing);
        }
        let domain = {
            let lv = self.level_nodes.lock();
            let mut chosen = None;
            for e in lv[1].iter() {
                if e.value().check_add_leaf_node(self.max_local_sched_per_domain_node) {
                    chosen = Some(e.value().clone());
                    break;
                }
            }
            chosen?
        };
        let local = self.add_node(info, 0);
        link_parent_child(&domain, &local);
        Some(local)
    }

    fn remove_leaf(&mut self, name: &str) -> Option<Arc<SchedNode>> {
        let lv = self.level_nodes.lock();
        if lv.is_empty() {
            return None;
        }
        let local = lv[0].get(name)?.value().clone();
        let parent = Node::get_parent(local.as_ref())?;
        parent.remove_child(name);
        lv[0].remove(name);
        Some(parent)
    }

    fn find(&self, name: &str) -> Option<Arc<SchedNode>> {
        self.find_non_leaf_node(name).or_else(|| self.find_leaf_node(name))
    }

    fn walk(&self, visitor: &mut dyn FnMut(&Arc<SchedNode>)) {
        let Some(root) = self.get_root_node() else {
            return;
        };
        fn dfs(n: &Arc<SchedNode>, visitor: &mut dyn FnMut(&Arc<SchedNode>)) {
            visitor(n);
            for c in n.get_children() {
                dfs(&c, visitor);
            }
        }
        dfs(&root, visitor);
    }

    fn add_non_leaf(&mut self, info: NodeInfo) -> Option<Arc<SchedNode>> {
        let level_size = self.level_nodes.lock().len();
        for level in (1..level_size.max(1)).rev() {
            if let Some(n) = self.find_non_leaf_node_at_level(&info.name, level) {
                return Some(n);
            }
        }
        let mut next = self.next_parent.lock();
        if next.is_none() {
            let node = self.add_node(info, 1);
            *next = Some(Arc::clone(&node));
            return Some(node);
        }
        let mut cur = next.as_ref()?.clone();
        while !cur.check_add_non_leaf_node(self.max_domain_sched_per_domain_node) {
            match cur.get_parent() {
                Some(p) => cur = p,
                None => break,
            }
        }
        if cur.check_add_non_leaf_node(self.max_domain_sched_per_domain_node) {
            let child_level = (cur.get_level() - 1).max(0) as usize;
            let node = self.add_node(info, child_level);
            link_parent_child(&cur, &node);
            if node.check_add_non_leaf_node(self.max_domain_sched_per_domain_node) {
                *next = Some(Arc::clone(&node));
            }
            return Some(node);
        }
        let new_level = cur.get_level() + 1;
        let node = self.add_node(info, new_level as usize);
        link_parent_child(&node, &cur);
        *next = Some(Arc::clone(&node));
        Some(node)
    }

    fn get_root(&self) -> Option<Arc<SchedNode>> {
        self.get_root_node()
    }

    fn serialize_tree_state(&self) -> Result<String, TopologyError> {
        let Some(root) = self.get_root_node() else {
            return Ok(String::new());
        };
        let wire = NodeWire::from_node(root.as_ref());
        Ok(serde_json::to_string(&wire)?)
    }

    fn deserialize_tree_state(&mut self, data: &str) -> Result<Status, TopologyError> {
        if data.is_empty() {
            return Ok(Status::ok());
        }
        let wire: NodeWire = serde_json::from_str(data)?;
        if wire.level < 0 {
            return Ok(Status::new(StatusCode::GsSchedTopologyBroken, "negative level"));
        }
        let root = wire.build_tree()?;
        let level = root.get_level().max(0) as usize;
        let mut lv = self.level_nodes.lock();
        lv.clear();
        lv.resize_with(level + 1, DashMap::new);
        drop(lv);
        self.index_subtree(&root);
        *self.next_parent.lock() = Some(self.find_deepest_non_leaf().unwrap_or(root));
        Ok(Status::ok())
    }
}

impl SchedTree {
    fn index_subtree(&self, node: &Arc<SchedNode>) {
        let level = node.get_level().max(0) as usize;
        {
            let mut lv = self.level_nodes.lock();
            if lv.len() < level + 1 {
                lv.resize_with(level + 1, DashMap::new);
            }
            lv[level].insert(node.name(), Arc::clone(node));
        }
        for c in node.get_children() {
            self.index_subtree(&c);
        }
    }

    fn find_deepest_non_leaf(&self) -> Option<Arc<SchedNode>> {
        let lv = self.level_nodes.lock();
        for level in (1..lv.len()).rev() {
            if let Some(e) = lv[level].iter().next() {
                return Some(e.value().clone());
            }
        }
        None
    }

    fn find_non_leaf_node(&self, name: &str) -> Option<Arc<SchedNode>> {
        let lv = self.level_nodes.lock();
        if lv.is_empty() {
            return None;
        }
        for level in (1..lv.len()).rev() {
            if let Some(n) = lv[level].get(name) {
                return Some(n.value().clone());
            }
        }
        None
    }

    fn find_non_leaf_node_at_level(&self, name: &str, level: usize) -> Option<Arc<SchedNode>> {
        let lv = self.level_nodes.lock();
        if level >= lv.len() {
            return None;
        }
        lv[level].get(name).map(|e| e.value().clone())
    }

    fn find_leaf_node(&self, name: &str) -> Option<Arc<SchedNode>> {
        let lv = self.level_nodes.lock();
        if lv.is_empty() {
            return None;
        }
        lv[0].get(name).map(|e| e.value().clone())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sched_tree_roundtrip_serde() {
        let mut tree = SchedTree::with_limits(8, 8);
        tree
            .add_non_leaf(NodeInfo::new("domain-a", "10.0.0.1"))
            .expect("domain");
        tree
            .add_leaf(NodeInfo::new("local-1", "10.0.0.2"))
            .expect("leaf");
        let s = tree.serialize_tree_state().unwrap();
        let mut t2 = SchedTree::with_limits(8, 8);
        assert!(t2.deserialize_tree_state(&s).unwrap().is_ok());
        assert!(t2.find("local-1").is_some());
    }

    #[test]
    fn tree_walk_visits_all() {
        let mut tree = SchedTree::with_limits(8, 8);
        tree.add_non_leaf(NodeInfo::new("d", "a")).unwrap();
        tree.add_leaf(NodeInfo::new("l", "b")).unwrap();
        let mut n = 0;
        tree.walk(&mut |_| n += 1);
        assert!(n >= 2);
    }

    #[test]
    fn node_trait_level_and_leaf() {
        let n = SchedNode::new(NodeInfo::new("x", "y"), 0);
        assert!(Node::is_leaf(n.as_ref()));
        let p = SchedNode::new(NodeInfo::new("p", "z"), 1);
        link_parent_child(&p, &n);
        assert!(n.get_parent().is_some());
    }
}
