#![allow(clippy::result_large_err)]

use std::net::SocketAddr;
use std::sync::Arc;
use std::time::Duration;

use clap::Parser;
use etcd_client::Client;
use tokio::net::TcpListener;
use tokio_util::sync::CancellationToken;
use tonic::transport::Server;
use tracing::info;
use yr_common::logging::init_logging;
use tokio::sync::Mutex as AsyncMetaMutex;
use yr_metastore_client::{MetaStoreClient, MetaStoreClientConfig};
use yr_proto::internal::domain_scheduler_service_server::DomainSchedulerServiceServer;

use yr_domain_scheduler::config::{CliArgs, DomainSchedulerConfig, ElectionMode};
use yr_domain_scheduler::election;
use yr_domain_scheduler::http_api::build_router;
use yr_domain_scheduler::nodes::{self, LocalNodeManager};
use yr_domain_scheduler::resource_view::ResourceView;
use yr_domain_scheduler::scheduler::SchedulingEngine;
use yr_domain_scheduler::service::{pending_reconcile_tick, DomainSchedulerGrpc};
use yr_domain_scheduler::state::DomainSchedulerState;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    init_logging();
    let cli = CliArgs::parse();
    let config = Arc::new(DomainSchedulerConfig::from_cli(cli).map_err(|e| anyhow::anyhow!(e))?);
    config.validate().map_err(|e| anyhow::anyhow!(e))?;

    let endpoints: Vec<&str> = config.etcd_endpoints.iter().map(|s| s.as_str()).collect();

    let election_client = if matches!(
        config.election_mode,
        ElectionMode::Etcd | ElectionMode::Txn | ElectionMode::K8s
    ) {
        let c = Client::connect(&endpoints, None)
            .await
            .map_err(|e| anyhow::anyhow!("etcd election connect: {e}"))?;
        Some(c)
    } else {
        None
    };

    let resource_view = Arc::new(ResourceView::new());
    let nodes = Arc::new(LocalNodeManager::new(resource_view.clone()));
    let scheduler = Arc::new(SchedulingEngine::new(
        config.clone(),
        resource_view.clone(),
        nodes.clone(),
    ));

    let mut metastore: Option<std::sync::Arc<AsyncMetaMutex<MetaStoreClient>>> = None;

    if let Err(e) = nodes::register_with_global(&config, &nodes).await {
        tracing::warn!(error = %e, "optional global registration failed");
    }

    if !endpoints.is_empty() {
        let ms_cfg = MetaStoreClientConfig {
            enable_meta_store: false,
            is_passthrough: false,
            etcd_address: config.etcd_endpoints.join(","),
            meta_store_address: String::new(),
            etcd_table_prefix: String::new(),
            excluded_keys: vec![],
            ssl_config: None,
        };
        match MetaStoreClient::connect(ms_cfg).await {
            Ok(mut c) => {
                let k = config.master_topology_key();
                match c.get(&k).await {
                    Ok(resp) => {
                        let bytes = resp.kvs.into_iter().next().map(|kv| kv.value);
                        if let Some(bytes) = bytes {
                            if let Ok(s) = String::from_utf8(bytes) {
                                nodes.apply_topology_json(&s, &config.advertise_grpc_addr());
                            }
                        }
                    }
                    Err(e) => tracing::warn!(error = %e, key = %k, "etcd topology get failed"),
                }
                metastore = Some(std::sync::Arc::new(AsyncMetaMutex::new(c)));
            }
            Err(e) => tracing::warn!(error = %e, "etcd metastore connect failed (topology load skipped)"),
        }
    }

    let state = DomainSchedulerState::new(
        config.clone(),
        resource_view.clone(),
        nodes.clone(),
        scheduler.clone(),
        metastore,
    );

    if let Some(client) = election_client {
        election::spawn_election_task((*config).clone(), state.clone(), client);
    }

    let shutdown = CancellationToken::new();

    let reconcile_state = state.clone();
    let reconcile_cancel = shutdown.child_token();
    tokio::spawn(async move {
        let mut tick = tokio::time::interval(Duration::from_secs(1));
        loop {
            tokio::select! {
                _ = reconcile_cancel.cancelled() => break,
                _ = tick.tick() => {
                    pending_reconcile_tick(&reconcile_state).await;
                }
            }
        }
    });

    let house_state = state.clone();
    let house_ms = config.pull_resource_interval_ms.max(500);
    let house_cancel = shutdown.child_token();
    tokio::spawn(async move {
        let mut tick = tokio::time::interval(Duration::from_millis(house_ms));
        loop {
            tokio::select! {
                _ = house_cancel.cancelled() => break,
                _ = tick.tick() => {
                    house_state.resource_view.release_expired_inflight();
                    house_state.heartbeat_observer.tick(
                        &house_state.nodes,
                        &house_state.resource_view,
                    );
                }
            }
        }
    });

    let grpc_impl = DomainSchedulerGrpc::new(state.clone());
    let grpc_service = DomainSchedulerServiceServer::new(grpc_impl);

    let grpc_addr: SocketAddr = config
        .grpc_listen_addr()
        .parse()
        .map_err(|e| anyhow::anyhow!("invalid gRPC host/port: {e}"))?;

    let http_addr: SocketAddr = format!("{}:{}", config.host, config.http_port)
        .parse()
        .map_err(|e| anyhow::anyhow!("invalid HTTP host/port: {e}"))?;

    let grpc_cancel = shutdown.child_token();
    let grpc_task = tokio::spawn(async move {
        let serve = Server::builder()
            .add_service(grpc_service)
            .serve_with_shutdown(grpc_addr, async move {
                grpc_cancel.cancelled().await;
            });
        if let Err(e) = serve.await {
            tracing::error!(error = %e, "gRPC server error");
        }
    });

    let app = build_router(state.clone()).layer(tower_http::trace::TraceLayer::new_for_http());
    let http_cancel = shutdown.child_token();
    let http_listener = TcpListener::bind(http_addr).await?;
    let http_task = tokio::spawn(async move {
        let r = axum::serve(http_listener, app.into_make_service())
            .with_graceful_shutdown(async move {
                http_cancel.cancelled().await;
            })
            .await;
        if let Err(e) = r {
            tracing::error!(error = %e, "HTTP server error");
        }
    });

    info!(
        grpc = %grpc_addr,
        http = %http_addr,
        election_mode = ?config.election_mode,
        global = %config.global_scheduler_address,
        "yr-domain-scheduler listening"
    );

    tokio::spawn(async move {
        let _ = tokio::signal::ctrl_c().await;
        shutdown.cancel();
    });

    tokio::select! {
        r = grpc_task => { r?; }
        r = http_task => { r?; }
    }

    Ok(())
}
