use anyhow::Context;
use std::net::SocketAddr;
use std::sync::Arc;
use std::time::Duration;
use tonic::service::Routes;
use tracing::info;
use yr_proto::bus_service::bus_service_server::BusServiceServer;
use yr_proto::exec_service::exec_service_server::ExecServiceServer;
use yr_proto::inner_service::inner_service_server::InnerServiceServer;
use yr_proto::internal::global_scheduler_service_server::GlobalSchedulerServiceServer;
use yr_proto::internal::local_scheduler_service_server::LocalSchedulerServiceServer;
use yr_proto::runtime_rpc::runtime_rpc_server::RuntimeRpcServer;
use yr_proxy::agent_manager::AgentManager;
use yr_proxy::busproxy::service_registry;
use yr_proxy::busproxy::BusProxyCoordinator;
use yr_proxy::config::Config;
use yr_proxy::grpc_services::ProxyGrpc;
use yr_proxy::http_api;
use yr_proxy::instance_ctrl::InstanceController;
use yr_proxy::instance_manager::InstanceManager;
use yr_proxy::local_scheduler::LocalSchedulerGrpc;
use yr_proxy::observer;
use yr_proxy::registration;
use yr_proxy::resource_view::{ResourceVector, ResourceView};
use yr_proxy::AppContext;
use yr_runtime_manager::config::Config as RmConfig;
use yr_runtime_manager::state::RuntimeManagerState;

/// Spawns child reaper, exit handler, instance health supervision, and metrics → agent (merge mode).
async fn start_embedded_runtime_manager(
    rm_cfg: Arc<RmConfig>,
    st: Arc<RuntimeManagerState>,
) -> anyhow::Result<()> {
    yr_runtime_manager::http_api::mark_process_start();
    let (tx, rx) = tokio::sync::mpsc::channel(256);
    let reaper = yr_runtime_manager::health_check::spawn_child_reaper(tx);
    let ag = Arc::new(yr_runtime_manager::agent::AgentClient::new(&rm_cfg)?);
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
    let ag_m = ag.clone();
    tokio::spawn(async move {
        let mut col = yr_runtime_manager::metrics::MetricsCollector::new();
        loop {
            tokio::time::sleep(metrics_every).await;
            let snap = col.collect(&st_m);
            yr_runtime_manager::metrics::apply_prometheus_snapshot(&snap);
            let json = yr_runtime_manager::metrics::build_resource_update_json(&snap).to_string();
            let unit = yr_runtime_manager::metrics::build_resource_unit(&snap);
            ag_m.update_resources_retry(json, Some(unit)).await;
        }
    });
    tokio::time::sleep(Duration::from_millis(150)).await;
    std::mem::forget(reaper);
    Ok(())
}

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
        yr_common::cli_compat::legacy_flags::FUNCTION_PROXY,
    );
    if config.node_id.trim().is_empty() {
        config.node_id = uuid::Uuid::new_v4().to_string();
    }
    init_litebus_ssl_env(&config)?;
    let _plugins = config
        .schedule_plugins_config()
        .context("parse schedule_plugins JSON")?;

    let etcd = if config.etcd_endpoints_vec().is_empty() {
        None
    } else {
        let ep = config.etcd_endpoints_vec();
        let ms_cfg = yr_metastore_client::MetaStoreClientConfig {
            enable_meta_store: false,
            is_passthrough: false,
            etcd_address: ep.join(","),
            meta_store_address: String::new(),
            etcd_table_prefix: config.etcd_table_prefix.clone(),
            excluded_keys: vec![],
            ssl_config: None,
        };
        let c = yr_metastore_client::MetaStoreClient::connect(ms_cfg)
            .await
            .context("connect etcd")?;
        Some(Arc::new(tokio::sync::Mutex::new(c)))
    };

    let capacity = ResourceVector {
        cpu: 8.0,
        memory: 64.0 * 1024.0 * 1024.0 * 1024.0,
        npu: 0.0,
    };
    let resource_view = ResourceView::new(capacity);
    let agent_manager = AgentManager::new();
    let config = Arc::new(config);

    let embedded_rm: Option<Arc<RuntimeManagerState>> = if config.enable_merge_process {
        let agent_addr = config.runtime_manager_address.trim();
        if agent_addr.is_empty() {
            anyhow::bail!(
                "--runtime-manager-address is required when --enable-merge-process is true \
                 (FunctionAgent gRPC URL for embedded runtime manager callbacks)"
            );
        }
        let agent_uri = if agent_addr.starts_with("http://") || agent_addr.starts_with("https://") {
            agent_addr.to_string()
        } else {
            format!("http://{agent_addr}")
        };
        let rm_cfg = Arc::new(RmConfig::embedded_in_agent(
            config.node_id.clone(),
            agent_uri,
            config.merge_runtime_paths.clone(),
            config.merge_runtime_initial_port,
            config.merge_port_count,
            std::path::PathBuf::from(&config.merge_runtime_log_path),
            config.merge_runtime_bind_mounts.clone(),
        ));
        rm_cfg
            .ensure_log_dir()
            .context("merge mode: create runtime manager log directory")?;
        let ports = Arc::new(yr_runtime_manager::port_manager::SharedPortManager::new(
            config.merge_runtime_initial_port,
            config.merge_port_count,
        )?);
        let st = Arc::new(RuntimeManagerState::new(rm_cfg.clone(), ports));
        start_embedded_runtime_manager(rm_cfg, st.clone())
            .await
            .context("start embedded runtime manager")?;
        info!(
            node_id = %config.node_id,
            agent = %st.config.agent_address,
            "yr-proxy merge_process: embedded runtime manager active"
        );
        Some(st)
    } else {
        None
    };

    let instance_ctrl = InstanceController::new_with_agent_manager(
        config.clone(),
        resource_view.clone(),
        etcd.clone(),
        embedded_rm,
        agent_manager.clone(),
    );

    if let Some(store) = etcd.clone() {
        let mut c = store.lock().await;
        let summary =
            yr_proxy::instance_recover::recover_after_proxy_start(&instance_ctrl, &mut c).await;
        if summary.rehydrated > 0 || summary.stale_in_flight_marked_failed > 0 {
            tracing::info!(
                rehydrated = summary.rehydrated,
                stale_failed = summary.stale_in_flight_marked_failed,
                "instance recover on startup"
            );
        }
    }

    let instance_manager = InstanceManager::new(instance_ctrl.clone(), config.clone());

    let state_store = etcd.clone().map(|store| {
        Arc::new(yr_proxy::busproxy::MetaStoreStateStore::new(store))
            as Arc<dyn yr_proxy::busproxy::StateStore>
    });
    let bus = BusProxyCoordinator::new_with_state_store(
        config.clone(),
        instance_ctrl.clone(),
        state_store,
    );
    bus.spawn_pending_create_reaper(std::time::Duration::from_secs(60));

    let domain_addr = Arc::new(parking_lot::RwLock::new(
        config.domain_scheduler_address.trim().to_string(),
    ));

    let ctx = Arc::new(AppContext {
        config: config.clone(),
        resource_view: resource_view.clone(),
        agent_manager,
        instance_ctrl,
        instance_manager,
        bus,
        etcd: etcd.clone(),
        domain_addr: domain_addr.clone(),
        topology: Arc::new(parking_lot::RwLock::new(None)),
        ready: Arc::new(tokio::sync::Notify::new()),
        ready_flag: Arc::new(std::sync::atomic::AtomicBool::new(false)),
    });

    if let Some(store) = etcd.clone() {
        // Phase 1: Initial reconciliation BEFORE accepting connections (C++ LiteBus parity).
        // This ensures routes, peers, and function metadata are populated before any SDK/driver
        // traffic arrives — eliminating the cold-start race that required SDK warmup.
        observer::initial_sync(ctx.clone()).await;
        yr_proxy::function_meta::initial_sync(ctx.clone()).await;
        ctx.mark_ready();
        info!("initial etcd sync complete, proxy ready to serve");

        // Phase 2: Background watch loops (reconnect on compaction/disconnect).
        let cfg = config.clone();
        tokio::spawn(async move {
            service_registry::run_busproxy_registration(store, cfg).await;
        });
        let obs_ctx = ctx.clone();
        tokio::spawn(async move {
            observer::run_watch_loops(obs_ctx).await;
        });
        let fm_ctx = ctx.clone();
        tokio::spawn(async move {
            yr_proxy::function_meta::run_watch_loop(fm_ctx).await;
        });
    } else {
        ctx.mark_ready();
    }

    let reg_ctx = ctx.clone();
    tokio::spawn(async move {
        registration::run_registration_and_heartbeat(reg_ctx).await;
    });

    let http_addr: SocketAddr = format!("{}:{}", config.host, config.http_port)
        .parse()
        .context("parse http listen addr")?;

    let cpp_address = config.cpp_ignored.address.trim();
    // In C++ deploy mode `--address` is the local-scheduler HTTP/gRPC endpoint
    // used by health checks and peer callbacks.  Multiple local schedulers can
    // run on one host, each with a unique `--address`, while Rust's standalone
    // `http_port` default is process-global (18402).  Binding that default for
    // every proxy makes the second data plane fail with EADDRINUSE.  Therefore
    // keep the standalone HTTP listener only when no C++ compatibility address
    // is provided; the compatibility listener below serves the required HTTP
    // routes on the per-proxy address.
    if cpp_address.is_empty() {
        let http_srv = axum::serve(
            tokio::net::TcpListener::bind(http_addr).await?,
            http_api::router(http_api::HttpState {
                resource_view: resource_view.clone(),
                node_id: config.node_id.clone(),
            })
            .into_make_service(),
        );
        tokio::spawn(async move {
            if let Err(e) = http_srv.await {
                tracing::error!(error = %e, "http server");
            }
        });
    }

    if !cpp_address.is_empty() {
        let compat_addr: SocketAddr = cpp_address
            .parse()
            .context("parse C++ --address compatibility HTTP listen addr")?;
        if compat_addr != http_addr {
            let compat_http = http_api::router(http_api::HttpState {
                resource_view: resource_view.clone(),
                node_id: config.node_id.clone(),
            });
            let compat_routes = Routes::from(compat_http)
                .add_service(LocalSchedulerServiceServer::new(LocalSchedulerGrpc::new(
                    ctx.clone(),
                )))
                .add_service(GlobalSchedulerServiceServer::new(
                    yr_proxy::global_scheduler_forward::GlobalSchedulerForward::new(
                        &config,
                        Some(ctx.agent_manager.clone()),
                    ),
                ));
            let compat_srv = tonic::transport::Server::builder()
                .accept_http1(true)
                .add_routes(compat_routes)
                .serve(compat_addr);
            info!(%compat_addr, "starting yr-proxy C++ compatibility HTTP/gRPC");
            tokio::spawn(async move {
                if let Err(e) = compat_srv.await {
                    tracing::error!(error = %e, "compat http/grpc server");
                }
            });
        }
    }

    let grpc = Arc::new(ProxyGrpc::new(ctx.clone()));
    let local = LocalSchedulerGrpc::new(ctx.clone());

    let grpc_addr: SocketAddr = config
        .grpc_listen_addr()
        .parse()
        .context("parse grpc listen addr")?;

    let gs_forward = yr_proxy::global_scheduler_forward::GlobalSchedulerForward::new(
        &config,
        Some(ctx.agent_manager.clone()),
    );

    // The upstream deploy script health-checks function_proxy by curling
    // `http://<FUNCTION_PROXY_PORT>/local-scheduler/healthy`, i.e. the same
    // port configured as `--address`.  Serve HTTP compatibility routes on
    // that port and fall back to the tonic gRPC services so Rust remains a
    // drop-in replacement for the C++ binary.
    let grpc_http = http_api::router(http_api::HttpState {
        resource_view: resource_view.clone(),
        node_id: config.node_id.clone(),
    });
    let routes = Routes::from(grpc_http)
        .add_service(LocalSchedulerServiceServer::new(local))
        .add_service(BusServiceServer::from_arc(grpc.clone()))
        .add_service(InnerServiceServer::from_arc(grpc.clone()))
        .add_service(RuntimeRpcServer::from_arc(grpc.clone()))
        .add_service(ExecServiceServer::from_arc(grpc))
        .add_service(GlobalSchedulerServiceServer::new(gs_forward));
    info!(%grpc_addr, "starting yr-proxy combined HTTP/gRPC");
    let server = tonic::transport::Server::builder()
        .accept_http1(true)
        .add_routes(routes);

    // POSIX / driver gRPC: serves RuntimeRPC, BusService, ExecService on the legacy port
    // that the Python SDK (libruntime) connects to via YR_SERVER_ADDRESS.
    let posix_addr: SocketAddr = format!("{}:{}", config.host, config.posix_port)
        .parse()
        .context("parse posix grpc addr")?;
    let posix_grpc = Arc::new(ProxyGrpc::new(ctx.clone()));
    info!(%posix_addr, "starting yr-proxy POSIX gRPC");
    tokio::spawn(async move {
        let s = tonic::transport::Server::builder()
            .add_service(RuntimeRpcServer::from_arc(posix_grpc.clone()))
            .add_service(BusServiceServer::from_arc(posix_grpc.clone()))
            .add_service(ExecServiceServer::from_arc(posix_grpc))
            .serve(posix_addr);
        if let Err(e) = s.await {
            tracing::error!(error = %e, "posix grpc server");
        }
    });

    let session_addr: SocketAddr = format!("{}:{}", config.host, config.session_grpc_port)
        .parse()
        .context("parse session grpc addr")?;
    let session_grpc = Arc::new(ProxyGrpc::new(ctx.clone()));
    tokio::spawn(async move {
        let s = tonic::transport::Server::builder()
            .add_service(RuntimeRpcServer::from_arc(session_grpc))
            .serve(session_addr);
        if let Err(e) = s.await {
            tracing::error!(error = %e, "session grpc server");
        }
    });

    server.serve(grpc_addr).await?;

    Ok(())
}
