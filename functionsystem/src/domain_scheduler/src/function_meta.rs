//! Function / instance scheduling metadata derived from `ScheduleRequest` labels and extensions.

use std::collections::HashMap;

use serde::Deserialize;
use yr_proto::internal::ScheduleRequest;

#[derive(Debug, Clone, Default)]
pub struct FunctionScheduleMeta {
    pub affinity_labels: HashMap<String, String>,
    pub anti_affinity_labels: HashMap<String, String>,
    pub failure_domains: Vec<String>,
    pub heterogeneous_keys: Vec<String>,
}

#[derive(Debug, Deserialize)]
struct ExtensionSchedulingJson {
    #[serde(default)]
    affinity: Option<HashMap<String, String>>,
    #[serde(default)]
    anti_affinity: Option<HashMap<String, String>>,
    #[serde(default)]
    failure_domains: Option<Vec<String>>,
    #[serde(default)]
    heterogeneous_resource_keys: Option<Vec<String>>,
}

fn parse_extension_scheduling(req: &ScheduleRequest) -> ExtensionSchedulingJson {
    let Some(raw) = req.extension.get("scheduling") else {
        return ExtensionSchedulingJson {
            affinity: None,
            anti_affinity: None,
            failure_domains: None,
            heterogeneous_resource_keys: None,
        };
    };
    serde_json::from_str(raw).unwrap_or_else(|_| ExtensionSchedulingJson {
        affinity: None,
        anti_affinity: None,
        failure_domains: None,
        heterogeneous_resource_keys: None,
    })
}

/// Parse once per request; safe to call from filters (cheap for empty extension).
pub fn parse_function_schedule_meta(req: &ScheduleRequest) -> FunctionScheduleMeta {
    let ext = parse_extension_scheduling(req);
    let mut meta = FunctionScheduleMeta::default();
    if let Some(a) = ext.affinity {
        meta.affinity_labels = a;
    }
    if let Some(a) = ext.anti_affinity {
        meta.anti_affinity_labels = a;
    }
    if let Some(fd) = ext.failure_domains {
        meta.failure_domains = fd;
    }
    if let Some(h) = ext.heterogeneous_resource_keys {
        meta.heterogeneous_keys = h;
    }
    meta
}
