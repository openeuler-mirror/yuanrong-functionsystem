//! Standalone MetaStore gRPC server (`yr-metastore-server` / `meta_store`).

use clap::Parser;
use tokio::net::TcpListener;
use tracing::info;
use yr_common::logging::init_logging;
use yr_metastore_server::{MetaStoreRole, MetaStoreServer, MetaStoreServerConfig};

#[derive(Parser, Debug)]
#[command(
    name = "meta_store",
    about = "openYuanrong MetaStore gRPC server (etcd-compatible wire)"
)]
struct Cli {
    /// Listen address, e.g. `0.0.0.0:23790`
    #[arg(long = "listen_addr", default_value = "0.0.0.0:23790")]
    listen_addr: String,

    /// Comma-separated etcd peers for backup/sync (empty = memory-only / local snapshot).
    #[arg(long = "etcd_address", default_value = "")]
    etcd_address: String,

    /// `master` or `slave`
    #[arg(long = "role", default_value = "master")]
    role: String,

    /// C++ `meta_service` flag; accepted for drop-in layouts (JSON not loaded yet).
    #[arg(long = "config_path", default_value = "")]
    _config_path: String,

    /// C++ `meta_service` log JSON path; accepted (Rust uses `RUST_LOG` / tracing).
    #[arg(long = "log_config_path", default_value = "")]
    _log_config_path: String,
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    init_logging();
    let cli = Cli::parse();

    let etcd_endpoints: Vec<String> = cli
        .etcd_address
        .split(',')
        .map(|s| s.trim().to_string())
        .filter(|s| !s.is_empty())
        .collect();

    let role = match cli.role.to_ascii_lowercase().as_str() {
        "slave" => MetaStoreRole::Slave,
        _ => MetaStoreRole::Master,
    };

    let cfg = MetaStoreServerConfig {
        listen_addr: cli.listen_addr.clone(),
        etcd_endpoints,
        role,
        ..Default::default()
    };

    let listener = TcpListener::bind(&cfg.listen_addr)
        .await
        .map_err(|e| anyhow::anyhow!("bind {}: {e}", cfg.listen_addr))?;

    let server = MetaStoreServer::new(cfg)
        .await
        .map_err(|e| anyhow::anyhow!("MetaStoreServer::new: {e}"))?;
    info!(addr = %cli.listen_addr, "metastore listening");
    server
        .serve(listener)
        .await
        .map_err(|e| anyhow::anyhow!("metastore serve: {e}"))?;
    Ok(())
}
