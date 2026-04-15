pub mod kv;
pub mod object;
pub mod router;
pub mod error;

pub use error::{DsError, DsResult};

pub const CRATE_ID: &str = "yr-data-client-lite";
