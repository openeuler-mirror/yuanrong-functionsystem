//! Logical key/value types with table prefix stripped on reads.

/// Watch event classification.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WatchEventType {
    Put,
    Delete,
}

/// Normalized watch payload (logical keys).
#[derive(Debug, Clone)]
pub struct WatchEvent {
    pub event_type: WatchEventType,
    pub key: Vec<u8>,
    pub value: Vec<u8>,
    pub prev_value: Option<Vec<u8>>,
    pub mod_revision: i64,
}

#[derive(Debug, Clone)]
pub struct KeyValue {
    pub key: Vec<u8>,
    pub value: Vec<u8>,
    pub create_revision: i64,
    pub mod_revision: i64,
    pub version: i64,
    pub lease: i64,
}

/// Range/single-key read with **logical** keys (etcd table prefix stripped).
#[derive(Debug, Clone)]
pub struct GetResponse {
    pub kvs: Vec<KeyValue>,
    pub more: bool,
    pub count: i64,
    /// `header.revision` from the backing etcd response (0 if absent).
    pub header_revision: i64,
}
