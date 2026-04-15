//! TCP port pool for runtime listen addresses (C++ `port/port_manager`).

mod manager;

pub use manager::{PortManager, SharedPortManager};
