//! Score structures (`scheduler_framework/utils/score.h`).

use std::cmp::Ordering;
use std::collections::HashMap;
use yr_proto::resources::value;

pub const MAX_UNIT_SCORE: i32 = 100;
pub const MIN_UNIT_SCORE: i32 = 0;

/// Vector-based resource allocation result (disk, hetero, etc.).
#[derive(Debug, Clone)]
pub struct VectorResourceAllocation {
    pub r#type: String,
    pub selected_indices: Vec<i32>,
    pub allocation_values: value::Vectors,
    pub extended_info: HashMap<String, String>,
}

#[derive(Debug, Clone)]
pub struct NodeScore {
    pub name: String,
    pub hetero_product_name: String,
    pub real_ids: Vec<i32>,
    pub score: f64,
    pub available_for_request: i32,
    pub allocated_vectors: HashMap<String, value::Vectors>,
    pub vector_allocations: Vec<VectorResourceAllocation>,
}

impl NodeScore {
    pub fn new(name: impl Into<String>, score: f64) -> Self {
        Self {
            name: name.into(),
            hetero_product_name: String::new(),
            real_ids: Vec::new(),
            score,
            available_for_request: 0,
            allocated_vectors: HashMap::new(),
            vector_allocations: Vec::new(),
        }
    }

    pub fn score_only(score: f64) -> Self {
        Self::new(String::new(), score)
    }

    pub fn add_weighted(&mut self, mut plugin: NodeScore, weight: f64) {
        plugin.score *= weight;
        self.merge_from(&plugin);
    }

    pub fn merge_from(&mut self, a: &NodeScore) {
        self.score += a.score;
        if !a.hetero_product_name.is_empty() {
            self.hetero_product_name.clone_from(&a.hetero_product_name);
        }
        if !a.real_ids.is_empty() {
            self.real_ids.clone_from(&a.real_ids);
        }
        for (k, v) in &a.allocated_vectors {
            self.allocated_vectors.insert(k.clone(), v.clone());
        }
        if !a.vector_allocations.is_empty() {
            self.vector_allocations
                .extend(a.vector_allocations.iter().cloned());
        }
    }
}

impl PartialEq for NodeScore {
    fn eq(&self, other: &Self) -> bool {
        self.score.total_cmp(&other.score).is_eq() && self.name == other.name
    }
}

impl Eq for NodeScore {}

impl PartialOrd for NodeScore {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl Ord for NodeScore {
    fn cmp(&self, other: &Self) -> Ordering {
        // Max-heap by score (higher score is "greater").
        self.score
            .total_cmp(&other.score)
            .then_with(|| self.name.cmp(&other.name))
    }
}

#[derive(Debug, Clone)]
pub struct PluginScore {
    pub name: String,
    pub score: f64,
}

impl PluginScore {
    pub fn new(name: impl Into<String>, score: f64) -> Self {
        Self {
            name: name.into(),
            score,
        }
    }
}

#[derive(Debug, Clone)]
pub struct NodePluginScores {
    pub node_name: String,
    pub scores: Vec<PluginScore>,
    pub total_score: f64,
}

impl NodePluginScores {
    pub fn new(id: impl Into<String>) -> Self {
        Self {
            node_name: id.into(),
            scores: Vec::new(),
            total_score: 0.0,
        }
    }

    pub fn add_plugin_score(&mut self, plugin_score: PluginScore) {
        self.total_score += plugin_score.score;
        self.scores.push(plugin_score);
    }
}
