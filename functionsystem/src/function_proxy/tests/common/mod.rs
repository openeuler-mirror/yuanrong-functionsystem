//! Shared helpers for yr-proxy integration tests.

use std::sync::Arc;

use clap::Parser;
use yr_proxy::busproxy::BusProxyCoordinator;
use yr_proxy::config::Config;
use yr_proxy::instance_ctrl::InstanceController;
use yr_proxy::resource_view::{ResourceVector, ResourceView};

pub fn make_proxy_config(node_id: &str, grpc_port: u16) -> Arc<Config> {
    Arc::new(
        Config::try_parse_from([
            "yr-proxy",
            "--node-id",
            node_id,
            "--host",
            "127.0.0.1",
            "--grpc-listen-port",
            &grpc_port.to_string(),
        ])
        .expect("parse test config"),
    )
}

/// DR-mode (direct routing) proxy config, used by tests that exercise the
/// `enable_direct_routing` kill-route hint path.
#[allow(dead_code)]
pub fn make_proxy_config_dr(node_id: &str, grpc_port: u16) -> Arc<Config> {
    Arc::new(
        Config::try_parse_from([
            "yr-proxy",
            "--node-id",
            node_id,
            "--host",
            "127.0.0.1",
            "--grpc-listen-port",
            &grpc_port.to_string(),
            "--enable-direct-routing",
        ])
        .expect("parse test config"),
    )
}

#[allow(dead_code)]
fn bus_from_config(config: Arc<Config>) -> Arc<BusProxyCoordinator> {
    let resource_view = ResourceView::new(ResourceVector {
        cpu: 8.0,
        memory: 64.0,
        npu: 0.0,
    });
    let instance_ctrl = InstanceController::new(config.clone(), resource_view, None, None);
    BusProxyCoordinator::new(config, instance_ctrl)
}

#[allow(dead_code)]
pub fn new_bus(node_id: &str, grpc_port: u16) -> Arc<BusProxyCoordinator> {
    bus_from_config(make_proxy_config(node_id, grpc_port))
}

/// Like [`new_bus`] but with DR mode (`enable_direct_routing`) turned on.
#[allow(dead_code)]
pub fn new_bus_dr(node_id: &str, grpc_port: u16) -> Arc<BusProxyCoordinator> {
    bus_from_config(make_proxy_config_dr(node_id, grpc_port))
}
