use std::net::SocketAddr;
use std::sync::Arc;
use std::time::Duration;

use anyhow::Context;
use tokio_stream::wrappers::TcpListenerStream;
use tonic::service::Routes;
use tracing::info;
use yr_agent::config::Config;
use yr_agent::deployer::DeployRouter;
use yr_agent::http_api;
use yr_agent::node_manager::NodeManager;
use yr_agent::registration::{spawn_registration_tasks, SchedulerLink};
use yr_agent::rm_client::RuntimeManagerClient;
use yr_agent::service::AgentService;
use yr_proto::internal::function_agent_service_server::FunctionAgentServiceServer;

fn init_litebus_ssl_env(config: &Config) -> anyhow::Result<()> {
    let inputs = yr_common::ssl_config::SslInputs::from_flag_strings(
        &config.cpp_ignored.ssl_enable,
        &config.cpp_ignored.metrics_ssl_enable,
        &config.cpp_ignored.ssl_base_path,
        &config.cpp_ignored.ssl_root_file,
        &config.cpp_ignored.ssl_cert_file,
        &config.cpp_ignored.ssl_key_file,
    );
    let ssl = yr_common::ssl_config::get_ssl_cert_config(&inputs);
    if inputs.ssl_enable {
        yr_common::ssl_config::apply_litebus_ssl_envs(&ssl).context("init LiteBus SSL env")?;
    }
    Ok(())
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    yr_common::logging::init_logging();
    let mut config = yr_common::cli_compat::parse_with_legacy_flags::<Config>(
        yr_common::cli_compat::legacy_flags::FUNCTION_AGENT,
    );
    if config.node_id.trim().is_empty() {
        config.node_id = uuid::Uuid::new_v4().to_string();
    }
    init_litebus_ssl_env(&config)?;
    let config = Arc::new(config);

    info!(
        data_system_host = %config.data_system_host,
        data_system_port = config.data_system_port,
        merge_process = config.enable_merge_process,
        "yr-agent settings"
    );

    std::fs::create_dir_all(&config.code_package_dir)
        .with_context(|| format!("create code_package_dir {}", config.code_package_dir))?;

    let merge_runtime = if config.enable_merge_process {
        let runtime_ld_library_path = config.effective_runtime_ld_library_path();
        if !runtime_ld_library_path.is_empty() {
            std::env::set_var("LD_LIBRARY_PATH", runtime_ld_library_path);
        }
        if !config.cpp_ignored.python_dependency_path.trim().is_empty() {
            let existing = std::env::var("PYTHONPATH").unwrap_or_default();
            let python_path = if existing.is_empty() {
                config.cpp_ignored.python_dependency_path.clone()
            } else {
                format!("{}:{}", config.cpp_ignored.python_dependency_path, existing)
            };
            std::env::set_var("PYTHONPATH", python_path);
        }
        let rm_cfg = Arc::new(config.embedded_runtime_manager_config());
        rm_cfg.ensure_log_dir()?;
        let ports = Arc::new(yr_runtime_manager::port_manager::SharedPortManager::new(
            config.merge_runtime_initial_port,
            config.merge_port_count,
        )?);
        let st = Arc::new(yr_runtime_manager::state::RuntimeManagerState::new(
            rm_cfg.clone(),
            ports,
        ));
        Some((rm_cfg, st))
    } else {
        None
    };

    let deploy = Arc::new(DeployRouter::new(
        std::path::PathBuf::from(&config.code_package_dir),
        config.s3_endpoint.clone(),
        config.s3_bucket.clone(),
    ));

    let rm: Arc<RuntimeManagerClient> = match &merge_runtime {
        Some((rm_cfg, st)) => Arc::new(RuntimeManagerClient::in_process(
            st.clone(),
            rm_cfg.runtime_path_list(),
        )),
        None => Arc::new(RuntimeManagerClient::remote(
            config.runtime_manager_address.clone(),
        )),
    };

    let scheduler = SchedulerLink::new_arc(
        config.local_scheduler_address.clone(),
        config.node_id.clone(),
        config.agent_grpc_endpoint(),
    );

    let agent = AgentService::new(deploy, rm.clone(), scheduler.clone());
    let runtimes = agent.runtimes_handle();
    let node = NodeManager::new_arc();
    spawn_registration_tasks(scheduler.clone(), rm.clone(), runtimes, node.clone());

    let http_state = http_api::HealthState {
        rm: rm.clone(),
        scheduler: scheduler.clone(),
        node_id: config.node_id.clone(),
        node: node.clone(),
    };
    let http_addr: SocketAddr = config
        .http_listen_addr()
        .parse()
        .context("parse http listen addr")?;
    let grpc_addr: SocketAddr = config
        .grpc_listen_addr()
        .parse()
        .context("parse grpc listen addr")?;

    if http_addr != grpc_addr {
        let http_srv = axum::serve(
            tokio::net::TcpListener::bind(http_addr).await?,
            http_api::router(http_state).into_make_service(),
        );
        tokio::spawn(async move {
            if let Err(e) = http_srv.await {
                tracing::error!(error = %e, "http server");
            }
        });
    }

    let listener = tokio::net::TcpListener::bind(grpc_addr)
        .await
        .context("bind agent gRPC")?;
    let local = listener.local_addr()?;
    info!(%local, "yr-agent FunctionAgentService listening");
    node.set_ready(true);

    if let Some((rm_cfg, st)) = merge_runtime {
        yr_runtime_manager::http_api::mark_process_start();
        let (tx, rx) = tokio::sync::mpsc::channel(256);
        let reaper = yr_runtime_manager::health_check::spawn_child_reaper(tx);
        let ag = Arc::new(yr_runtime_manager::agent::AgentClient::new(&rm_cfg)?);
        yr_runtime_manager::oom::spawn_user_space_oom_supervision(st.clone(), ag.clone());
        tokio::spawn(yr_runtime_manager::health_check::handle_child_exits(
            rx,
            st.clone(),
            ag.clone(),
        ));
        tokio::spawn(yr_runtime_manager::instance_health::supervision_loop(
            st.clone(),
        ));
        let metrics_every = Duration::from_millis(rm_cfg.metrics_interval_ms.max(100));
        let st_m = st.clone();
        tokio::spawn(async move {
            let mut col = yr_runtime_manager::metrics::MetricsCollector::new();
            loop {
                tokio::time::sleep(metrics_every).await;
                let snap = col.collect(&st_m);
                yr_runtime_manager::metrics::apply_prometheus_snapshot(&snap);
                let json = serde_json::to_string(&snap).unwrap_or_else(|_| "{}".to_string());
                ag.update_resources_retry(json).await;
            }
        });
        tokio::time::sleep(Duration::from_millis(150)).await;
        std::mem::forget(reaper);
    }

    let incoming = TcpListenerStream::new(listener);
    if http_addr == grpc_addr {
        // C++ deploy uses the same `--port` for function-agent health probes
        // and gRPC callbacks. Serve the minimal health endpoints plus gRPC on
        // that single port so deploy's curl health check and function_proxy
        // callbacks both work.
        let health_routes = axum08::Router::new()
            .route("/healthy", axum08::routing::get(|| async { "" }))
            .route(
                "/function-agent/healthy",
                axum08::routing::get(|| async { "" }),
            );
        let routes =
            Routes::from(health_routes).add_service(FunctionAgentServiceServer::from_arc(agent));
        tonic::transport::Server::builder()
            .accept_http1(true)
            .add_routes(routes)
            .serve_with_incoming(incoming)
            .await?;
    } else {
        tonic::transport::Server::builder()
            .add_service(FunctionAgentServiceServer::from_arc(agent))
            .serve_with_incoming(incoming)
            .await?;
    }

    Ok(())
}
