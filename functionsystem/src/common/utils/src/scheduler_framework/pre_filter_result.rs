//! Pre-filter iteration results (`policy.h`).

use crate::status::Status;
use std::collections::{HashMap, HashSet};

pub trait PreFilterResult: Send + Sync {
    fn empty(&self) -> bool;
    fn end(&self) -> bool;
    fn next(&mut self);
    fn current(&self) -> &str;
    fn reset(&mut self, cur: &str);
    fn status(&self) -> &Status;
}

/// Keys sorted for deterministic behavior; `reset`/`next` follow `ProtoMapPreFilterResult` semantics.
pub struct MapPreFilterResult {
    keys: Vec<String>,
    status: Status,
    idx: usize,
    end_idx: usize,
    loop_end_idx: usize,
    need_looped: bool,
}

impl MapPreFilterResult {
    pub fn from_string_map_keys<T>(map: &HashMap<String, T>, status: Status) -> Self {
        let mut keys: Vec<String> = map.keys().cloned().collect();
        keys.sort();
        let n = keys.len();
        Self {
            keys,
            status,
            idx: 0,
            end_idx: n,
            loop_end_idx: n,
            need_looped: false,
        }
    }
}

impl PreFilterResult for MapPreFilterResult {
    fn empty(&self) -> bool {
        self.keys.is_empty()
    }

    fn end(&self) -> bool {
        self.idx >= self.end_idx
    }

    fn next(&mut self) {
        self.idx += 1;
        if self.need_looped && self.idx == self.keys.len() {
            self.idx = 0;
            self.end_idx = self.loop_end_idx;
            self.need_looped = false;
        }
    }

    fn current(&self) -> &str {
        if self.end() {
            return "";
        }
        self.keys
            .get(self.idx)
            .map(|s| s.as_str())
            .unwrap_or("")
    }

    fn reset(&mut self, cur: &str) {
        let Some(i) = self.keys.iter().position(|k| k == cur) else {
            return;
        };
        let after = i + 1;
        if after < self.keys.len() {
            self.need_looped = true;
            self.idx = after;
            self.end_idx = self.keys.len();
            self.loop_end_idx = after;
        }
    }

    fn status(&self) -> &Status {
        &self.status
    }
}

pub struct SetPreFilterResult {
    keys: Vec<String>,
    status: Status,
    idx: usize,
}

impl SetPreFilterResult {
    pub fn new(set: &HashSet<String>, status: Status) -> Self {
        let mut keys: Vec<String> = set.iter().cloned().collect();
        keys.sort();
        Self { keys, status, idx: 0 }
    }
}

impl PreFilterResult for SetPreFilterResult {
    fn empty(&self) -> bool {
        self.keys.is_empty()
    }

    fn end(&self) -> bool {
        self.idx >= self.keys.len()
    }

    fn next(&mut self) {
        if self.idx < self.keys.len() {
            self.idx += 1;
        }
    }

    fn current(&self) -> &str {
        self.keys
            .get(self.idx)
            .map(|s| s.as_str())
            .unwrap_or("")
    }

    fn reset(&mut self, _cur: &str) {}

    fn status(&self) -> &Status {
        &self.status
    }
}
