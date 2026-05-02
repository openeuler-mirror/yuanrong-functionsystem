use std::net::SocketAddr;
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::{broadcast, mpsc};
use tonic::transport::Server;
use tracing::info;
use yr_proto::internal::runtime_manager_service_server::RuntimeManagerServiceServer;
use yr_runtime_manager::agent::AgentClient;
use yr_runtime_manager::config::Config;
use yr_runtime_manager::health_check;
use yr_runtime_manager::metrics::MetricsCollector;
use yr_runtime_manager::port_manager::SharedPortManager;
use yr_runtime_manager::service::RuntimeManagerGrpc;
use yr_runtime_manager::state::RuntimeManagerState;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    yr_common::logging::init_logging();
    yr_runtime_manager::http_api::mark_process_start();
    let cfg = Arc::new(yr_common::cli_compat::parse_with_legacy_flags::<Config>(
        yr_common::cli_compat::legacy_flags::RUNTIME_MANAGER,
    ));
    cfg.ensure_log_dir()?;

    let ports = Arc::new(SharedPortManager::new(
        cfg.runtime_initial_port,
        cfg.port_count,
    )?);
    let state = Arc::new(RuntimeManagerState::new(cfg.clone(), ports));
    let agent = Arc::new(AgentClient::new(&cfg)?);

    yr_runtime_manager::oom::spawn_user_space_oom_supervision(state.clone(), agent.clone());

    let (tx, rx) = mpsc::channel::<health_check::ChildExitEvent>(256);
    let reaper = health_check::spawn_child_reaper(tx);
    tokio::spawn(health_check::handle_child_exits(
        rx,
        state.clone(),
        agent.clone(),
    ));

    let health_state = state.clone();
    tokio::spawn(yr_runtime_manager::instance_health::supervision_loop(
        health_state,
    ));

    let metrics_state = state.clone();
    let metrics_agent = agent.clone();
    let metrics_every = Duration::from_millis(cfg.metrics_interval_ms.max(100));
    tokio::spawn(async move {
        let mut col = MetricsCollector::new();
        loop {
            tokio::time::sleep(metrics_every).await;
            let snap = col.collect(&metrics_state);
            yr_runtime_manager::metrics::apply_prometheus_snapshot(&snap);
            let resource_unit = yr_runtime_manager::metrics::build_resource_unit(&snap);
            let json = yr_runtime_manager::metrics::build_resource_update_json(&snap).to_string();
            metrics_agent
                .update_resources_retry(json, Some(resource_unit))
                .await;
        }
    });

    let grpc_addr: SocketAddr = cfg.grpc_listen_addr().parse()?;
    let http_addr: SocketAddr = cfg.http_listen_addr().parse()?;

    let state_grpc_shutdown = state.clone();
    let grpc = RuntimeManagerGrpc::new(cfg.clone(), state);
    let grpc_service = RuntimeManagerServiceServer::new(grpc);

    info!(
        grpc = %grpc_addr,
        http = %http_addr,
        node_id = %cfg.node_id,
        agent = %cfg.agent_address,
        "yr-runtime-manager starting"
    );

    let (shutdown_tx, _) = broadcast::channel::<()>(4);
    let mut shutdown_grpc = shutdown_tx.subscribe();
    let shutdown_http = shutdown_tx.subscribe();
    tokio::spawn(async move {
        let _ = tokio::signal::ctrl_c().await;
        info!("shutdown signal received");
        let _ = shutdown_tx.send(());
    });

    let tonic_srv = Server::builder()
        .add_service(grpc_service)
        .serve_with_shutdown(grpc_addr, async move {
            let _ = shutdown_grpc.recv().await;
            yr_runtime_manager::runtime_ops::shutdown_all_runtimes(&state_grpc_shutdown, false);
        });

    tokio::try_join!(
        async move { tonic_srv.await.map_err(|e| anyhow::anyhow!(e)) },
        yr_runtime_manager::http_api::serve(http_addr, shutdown_http, cfg.clone())
    )?;

    drop(reaper);
    Ok(())
}
