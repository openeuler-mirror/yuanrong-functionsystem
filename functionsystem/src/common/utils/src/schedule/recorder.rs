//! Tracks transient schedule failures for retry / observability (`schedule_recorder` in C++).

use std::sync::Arc;

use dashmap::DashMap;

use crate::status::Status;

/// In-memory recorder (C++ backed a litebus actor; here `DashMap` replaces cross-thread state).
#[derive(Debug, Clone)]
pub struct ScheduleRecorder {
    errs: Arc<DashMap<String, Status>>,
}

impl Default for ScheduleRecorder {
    fn default() -> Self {
        Self::new()
    }
}

impl ScheduleRecorder {
    pub fn new() -> Self {
        Self {
            errs: Arc::new(DashMap::new()),
        }
    }

    pub fn record_schedule_err(&self, request_id: impl Into<String>, status: Status) {
        self.errs.insert(request_id.into(), status);
    }

    pub fn try_query_schedule_err(&self, request_id: &str) -> Option<Status> {
        self.errs.get(request_id).map(|e| e.clone())
    }

    pub fn erase_schedule_err(&self, request_id: &str) {
        self.errs.remove(request_id);
    }
}
