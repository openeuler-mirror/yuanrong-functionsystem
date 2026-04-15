//! `PluginToStatus` (`utils/plugin_to_status.h`).

use crate::status::{Status, StatusCode};
use std::collections::HashMap;

#[derive(Debug, Default)]
pub struct PluginToStatus {
    plugin_status: HashMap<String, Status>,
}

impl PluginToStatus {
    pub fn add_plugin_status(&mut self, name: impl Into<String>, status: Status) {
        self.plugin_status.insert(name.into(), status);
    }

    pub fn merge_status(&self) -> Status {
        let mut final_status = Status::ok();
        if self.plugin_status.is_empty() {
            return final_status;
        }
        for (_, st) in &self.plugin_status {
            if st.is_error() {
                final_status = st.clone();
            }
            let piece = st.to_string();
            if !final_status.message.is_empty() && !piece.is_empty() {
                final_status.message.push_str("; ");
            }
            final_status.message.push_str(&piece);
        }
        if final_status.code == StatusCode::Success && !self.plugin_status.is_empty() {
            // Preserve last non-success if any
            for (_, st) in &self.plugin_status {
                if st.is_error() {
                    final_status.code = st.code;
                }
            }
        }
        final_status
    }
}
