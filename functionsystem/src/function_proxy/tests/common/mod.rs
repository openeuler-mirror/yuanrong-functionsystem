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

#[allow(dead_code)]
pub fn new_bus(node_id: &str, grpc_port: u16) -> Arc<BusProxyCoordinator> {
    let config = make_proxy_config(node_id, grpc_port);
    let resource_view = ResourceView::new(ResourceVector {
        cpu: 8.0,
        memory: 64.0,
        npu: 0.0,
    });
    let instance_ctrl = InstanceController::new(config.clone(), resource_view, None, None);
    BusProxyCoordinator::new(config, instance_ctrl)
}
