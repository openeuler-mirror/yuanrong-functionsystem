//! `SchedTree` — mirrors C++ `sched_tree.{h,cpp}` (`MIN_TREE_LEVEL = 2`).

use std::collections::HashMap;

use parking_lot::Mutex;
use prost::Message;
use yr_proto::messages::SchedulerNode as ProtoNode;

use crate::sched_node::{NodeInfo, NodeState, SchedNode};

const MIN_TREE_LEVEL: usize = 2;

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RecoverError {
    BrokenTopology,
    InvalidLevel,
}

pub struct SchedTree {
    inner: Mutex<SchedTreeInner>,
}

struct SchedTreeInner {
    /// `levelNodes_[0]` = locals; higher indices = domain tiers.
    level_nodes: Vec<HashMap<String, SchedNode>>,
    next_parent: Option<SchedNode>,
    max_local_sched_per_domain: usize,
    max_domain_sched_per_domain: usize,
}

impl SchedTree {
    pub fn new(max_local_sched_per_domain: usize, max_domain_sched_per_domain: usize) -> Self {
        Self {
            inner: Mutex::new(SchedTreeInner {
                level_nodes: Vec::new(),
                next_parent: None,
                max_local_sched_per_domain: max_local_sched_per_domain.max(2),
                max_domain_sched_per_domain: max_domain_sched_per_domain.max(2),
            }),
        }
    }

    pub fn with_defaults() -> Self {
        Self::new(4005, 1000)
    }

    /// C++ `AddLeafNode`.
    pub fn add_leaf_node(&self, node_info: NodeInfo) -> Option<SchedNode> {
        let mut g = self.inner.lock();
        if g.level_nodes.len() < MIN_TREE_LEVEL {
            return None;
        }
        if let Some(existing) = g.level_nodes[0].get(&node_info.name) {
            existing.set_node_info(node_info);
            return Some(existing.clone());
        }
        let mut domain_node: Option<SchedNode> = None;
        for (_, n) in &g.level_nodes[1] {
            if n.check_add_leaf(g.max_local_sched_per_domain) {
                domain_node = Some(n.clone());
                break;
            }
        }
        let domain_node = domain_node?;
        let local = g.add_node(node_info, 0);
        domain_node.add_child(&local);
        Some(local)
    }

    /// C++ `AddNonLeafNode`.
    pub fn add_non_leaf_node(&self, node_info: NodeInfo) -> Option<SchedNode> {
        let mut g = self.inner.lock();
        let level_size = g.level_nodes.len();
        for level in (1..=level_size.saturating_sub(1)).rev() {
            if let Some(n) = g.level_nodes[level].get(&node_info.name) {
                n.set_node_info(node_info);
                return Some(n.clone());
            }
        }

        if g.next_parent.is_none() {
            let node = g.add_node(node_info, 1);
            g.next_parent = Some(node.clone());
            return Some(node);
        }

        let mut next_parent = g.next_parent.clone()?;

        while !next_parent.check_add_non_leaf(g.max_domain_sched_per_domain) {
            match next_parent.parent() {
                Some(p) => next_parent = p,
                None => break,
            }
        }

        if next_parent.check_add_non_leaf(g.max_domain_sched_per_domain) {
            let child_level = next_parent.level() - 1;
            let node = g.add_node(node_info, child_level as usize);
            next_parent.add_child(&node);
            if node.check_add_non_leaf(g.max_domain_sched_per_domain) {
                g.next_parent = Some(node.clone());
            }
            return Some(node);
        }

        let new_parent_level = next_parent.level() + 1;
        let node = g.add_node(node_info, new_parent_level as usize);
        node.add_child(&next_parent);
        g.next_parent = Some(node.clone());
        Some(node)
    }

    pub fn serialize_as_bytes(&self) -> Vec<u8> {
        let g = self.inner.lock();
        let Some(root) = g.get_root_node_inner() else {
            return Vec::new();
        };
        let proto = g.node_to_proto(&root);
        proto.encode_to_vec()
    }

    /// Backwards-compatible name used in C++ (`SerializeAsString`).
    pub fn serialize_as_string(&self) -> Vec<u8> {
        self.serialize_as_bytes()
    }

    pub fn recover_from_bytes(&self, topology: &[u8]) -> Result<(), RecoverError> {
        let root: ProtoNode = ProtoNode::decode(topology).map_err(|_| RecoverError::BrokenTopology)?;
        if root.level < 0 {
            return Err(RecoverError::InvalidLevel);
        }
        let level = root.level as usize;
        let root_node = SchedNode::new(
            NodeInfo {
                name: root.name.clone(),
                address: root.address.clone(),
            },
            root.level,
        );
        let mut g = self.inner.lock();
        g.level_nodes.clear();
        g.next_parent = None;
        g.level_nodes.resize(level + 1, HashMap::new());
        g.level_nodes[level].insert(root_node.name(), root_node.clone());
        g.add_child_from_proto(&root_node, &root);
        Ok(())
    }

    /// C++ `GetRootNode`: `nullptr` if `levelNodes_.size() < MIN_TREE_LEVEL`.
    pub fn get_root_node(&self) -> Option<SchedNode> {
        let g = self.inner.lock();
        g.get_root_node_inner()
    }

    /// C++ `ReplaceNonLeafNode`.
    pub fn replace_non_leaf_node(&self, replaced: &str, new_info: NodeInfo) -> Option<SchedNode> {
        let g = self.inner.lock();
        if g.level_nodes.len() < MIN_TREE_LEVEL {
            return None;
        }
        for i in (1..g.level_nodes.len()).rev() {
            if let Some(node) = g.level_nodes[i].get(replaced) {
                if node.state() != NodeState::Broken {
                    break;
                }
                node.set_node_info(new_info);
                node.set_state(NodeState::Connected);
                return Some(node.clone());
            }
        }
        None
    }

    pub fn set_state(&self, node: &SchedNode, state: NodeState) {
        node.set_state(state);
    }

    pub fn find_non_leaf_node(&self, name: &str) -> Option<SchedNode> {
        let g = self.inner.lock();
        if g.level_nodes.is_empty() {
            return None;
        }
        for level in (1..g.level_nodes.len()).rev() {
            if let Some(n) = g.level_nodes[level].get(name) {
                return Some(n.clone());
            }
        }
        None
    }

    pub fn find_leaf_node(&self, name: &str) -> Option<SchedNode> {
        let g = self.inner.lock();
        g.level_nodes
            .first()
            .and_then(|m| m.get(name))
            .cloned()
    }

    /// C++ `RemoveLeafNode`.
    pub fn remove_leaf_node(&self, name: &str) -> Option<SchedNode> {
        let mut g = self.inner.lock();
        if g.level_nodes.is_empty() {
            return None;
        }
        let local = g.level_nodes[0].get(name)?.clone();
        let parent = local.parent()?;
        parent.remove_child_named(name);
        g.level_nodes[0].remove(name);
        Some(parent)
    }

    /// Remove any node by name: leaf uses `RemoveLeafNode`; non-leaf removes the full subtree.
    pub fn remove_node(&self, name: &str) -> Option<()> {
        if self.find_leaf_node(name).is_some() {
            self.remove_leaf_node(name)?;
            return Some(());
        }
        let mut g = self.inner.lock();
        let mut target: Option<(usize, SchedNode)> = None;
        for level in 1..g.level_nodes.len() {
            if let Some(n) = g.level_nodes[level].get(name) {
                target = Some((level, n.clone()));
                break;
            }
        }
        let (_level, node) = target?;
        if let Some(p) = node.parent() {
            p.remove_child_named(name);
        }
        let mut stack = vec![node];
        let mut names_by_level: Vec<Vec<String>> = vec![];
        while let Some(n) = stack.pop() {
            let lvl = n.level() as usize;
            if names_by_level.len() <= lvl {
                names_by_level.resize(lvl + 1, Vec::new());
            }
            names_by_level[lvl].push(n.name());
            for (_, c) in n.children() {
                stack.push(c);
            }
        }
        for (lvl, names) in names_by_level.into_iter().enumerate() {
            if lvl >= g.level_nodes.len() {
                continue;
            }
            for nm in names {
                if let Some(removed) = g.level_nodes[lvl].remove(&nm) {
                    removed.clear_parent();
                }
            }
        }
        if g.next_parent.as_ref().map(|x| x.name()) == Some(name.to_string()) {
            g.next_parent = None;
        }
        Some(())
    }

    pub fn find_nodes(&self, level: u64) -> HashMap<String, SchedNode> {
        let g = self.inner.lock();
        let idx = level as usize;
        if idx >= g.level_nodes.len() {
            return HashMap::new();
        }
        g.level_nodes[idx].clone()
    }

    pub fn level_count(&self) -> usize {
        self.inner.lock().level_nodes.len()
    }
}

impl SchedTreeInner {
    fn get_root_node_inner(&self) -> Option<SchedNode> {
        if self.level_nodes.len() < MIN_TREE_LEVEL {
            return None;
        }
        self.level_nodes.last().and_then(|m| m.values().next().cloned())
    }

    fn add_node(&mut self, node_info: NodeInfo, level: usize) -> SchedNode {
        let node = SchedNode::new(node_info, level as i32);
        if self.level_nodes.len() < level + 1 {
            self.level_nodes.resize(level + 1, HashMap::new());
        }
        self.level_nodes[level].insert(node.name(), node.clone());
        node
    }

    fn node_to_proto(&self, node: &SchedNode) -> ProtoNode {
        let mut p = ProtoNode {
            name: node.name(),
            address: node.address(),
            level: node.level(),
            children: vec![],
        };
        let mut kids: Vec<_> = node.children().into_values().collect();
        kids.sort_by(|a, b| a.name().cmp(&b.name()));
        for c in kids {
            p.children.push(self.node_to_proto(&c));
        }
        p
    }

    fn add_child_from_proto(&mut self, parent: &SchedNode, proto: &ProtoNode) {
        for child in &proto.children {
            if child.level < 0 {
                continue;
            }
            let level = child.level as usize;
            let child_node = SchedNode::new(
                NodeInfo {
                    name: child.name.clone(),
                    address: child.address.clone(),
                },
                child.level,
            );
            if self.level_nodes.len() < level + 1 {
                self.level_nodes.resize(level + 1, HashMap::new());
            }
            self.level_nodes[level].insert(child_node.name(), child_node.clone());
            parent.add_child(&child_node);
            self.add_child_from_proto(&child_node, child);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn add_leaf_after_single_domain() {
        let t = SchedTree::new(4, 2);
        assert!(t
            .add_leaf_node(NodeInfo {
                name: "l1".into(),
                address: "a".into(),
            })
            .is_none());
        t.add_non_leaf_node(NodeInfo {
            name: "d0".into(),
            address: "dom:1".into(),
        });
        assert!(t.get_root_node().is_some());
        assert!(t
            .add_leaf_node(NodeInfo {
                name: "l1".into(),
                address: "local:1".into(),
            })
            .is_some());
    }

    #[test]
    fn roundtrip_proto() {
        let t = SchedTree::new(4005, 2);
        t.add_non_leaf_node(NodeInfo {
            name: "d0".into(),
            address: "d:0".into(),
        });
        t.add_non_leaf_node(NodeInfo {
            name: "d1".into(),
            address: "d:1".into(),
        });
        let _ = t
            .add_leaf_node(NodeInfo {
                name: "leaf".into(),
                address: "l:1".into(),
            })
            .unwrap();
        let bytes = t.serialize_as_bytes();
        let t2 = SchedTree::new(4005, 2);
        t2.recover_from_bytes(&bytes).unwrap();
        assert_eq!(t.serialize_as_bytes(), t2.serialize_as_bytes());
    }
}
