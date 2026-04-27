#![allow(clippy::result_large_err)]

use std::collections::VecDeque;
use std::net::SocketAddr;
use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use std::time::Duration;

use etcd_client::Client;
use parking_lot::Mutex;
use tokio::net::TcpListener;
use tokio_util::sync::CancellationToken;
use tonic::service::Routes;
use tonic::transport::Server;
use tracing::info;
use yr_common::etcd_keys::{
    ABNORMAL_SCHEDULER_PREFIX, BUSPROXY_PATH_PREFIX, FUNC_META_PATH_PREFIX, INSTANCE_PATH_PREFIX,
};
use yr_common::logging::init_logging;
use yr_master::config::{CliArgs, ElectionMode, MasterConfig};
use yr_master::domain_activator::DomainActivator;
use yr_master::domain_sched_mgr::DomainSchedMgr;
use yr_master::http::build_router;
use yr_master::instances::InstanceManager;
use yr_master::local_sched_mgr::LocalSchedMgr;
use yr_master::node_manager::NodeManager;
use yr_master::schedule_decision::ScheduleDecisionManager;
use yr_master::schedule_manager::ScheduleManager;
use yr_master::scheduler::{GlobalSchedulerImpl, MasterState};
use yr_master::snapshot::SnapshotManager;
use yr_master::system_func_loader::SystemFunctionLoader;
use yr_master::topology::{spawn_metastore_topology_watch, TopologyManager};
use yr_metastore_client::{MetaStoreClient, MetaStoreClientConfig};
use yr_metastore_server::MetaStoreServerConfig;
use yr_proto::internal::global_scheduler_service_server::GlobalSchedulerServiceServer;

use yr_master::election;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    init_logging();
    let cli = yr_common::cli_compat::parse_with_legacy_flags::<CliArgs>(
        yr_common::cli_compat::legacy_flags::FUNCTION_MASTER,
    );
    let config = Arc::new(MasterConfig::from_cli(cli).map_err(|e| anyhow::anyhow!(e))?);
    config.validate().map_err(|e| anyhow::anyhow!(e))?;

    let endpoints: Vec<&str> = config.etcd_endpoints.iter().map(|s| s.as_str()).collect();

    let metastore = if config.enable_meta_store {
        let bind_addr = format!("0.0.0.0:{}", config.meta_store_port);
        let listener = TcpListener::bind(&bind_addr)
            .await
            .map_err(|e| anyhow::anyhow!("MetaStore bind {bind_addr}: {e}"))?;
        let ms_server_cfg = MetaStoreServerConfig {
            listen_addr: bind_addr.clone(),
            etcd_endpoints: config.etcd_endpoints.clone(),
            ..Default::default()
        };
        let embedded = yr_metastore_server::MetaStoreServer::new(ms_server_cfg)
            .await
            .map_err(|e| anyhow::anyhow!("MetaStoreServer::new: {e}"))?;
        tokio::spawn(async move {
            if let Err(e) = embedded.serve(listener).await {
                tracing::error!(error = %e, "embedded MetaStore gRPC server");
            }
        });

        let meta_store_address = {
            let a = config.meta_store_address.trim();
            if a.is_empty() {
                format!("127.0.0.1:{}", config.meta_store_port)
            } else {
                a.to_string()
            }
        };
        let ms_cfg = MetaStoreClientConfig {
            enable_meta_store: true,
            is_passthrough: false,
            etcd_address: config.etcd_endpoints.join(","),
            meta_store_address,
            etcd_table_prefix: config.etcd_table_prefix.clone(),
            excluded_keys: vec![],
            ssl_config: None,
        };
        let c = MetaStoreClient::connect(ms_cfg)
            .await
            .map_err(|e| anyhow::anyhow!("MetaStore client connect: {e}"))?;
        Some(Arc::new(tokio::sync::Mutex::new(c)))
    } else {
        None
    };

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

    let topology = Arc::new(TopologyManager::new(config.clone(), metastore.clone()));
    topology.load_from_etcd().await;

    let is_leader = Arc::new(AtomicBool::new(matches!(
        config.election_mode,
        ElectionMode::Standalone
    )));
    let snapshots = SnapshotManager::new();
    let instances = Arc::new(InstanceManager::new(
        is_leader.clone(),
        snapshots.clone(),
        metastore.clone(),
    ));

    let shutdown = CancellationToken::new();

    if config.enable_meta_store {
        let Some(ms) = metastore.clone() else {
            anyhow::bail!(
                "internal error: metastore client missing while enable_meta_store is true"
            );
        };
        let topo = topology.clone();
        let watch_cancel = shutdown.child_token();
        spawn_metastore_topology_watch(topo, ms.clone(), watch_cancel);

        let prefixes = vec![
            (INSTANCE_PATH_PREFIX.to_string(), "instance"),
            (FUNC_META_PATH_PREFIX.to_string(), "func_meta"),
            (BUSPROXY_PATH_PREFIX.to_string(), "busproxy"),
            (ABNORMAL_SCHEDULER_PREFIX.to_string(), "abnormal"),
        ];
        InstanceManager::spawn_meta_watches(
            instances.clone(),
            ms,
            prefixes,
            shutdown.child_token(),
        );
    }

    let domain_sched_mgr = Arc::new(DomainSchedMgr::new(
        Duration::from_millis(config.domain_schedule_timeout_ms.max(1)),
        Duration::from_secs(config.schedule_retry_sec.max(1)),
    ));
    let local_sched_mgr = Arc::new(LocalSchedMgr::new(Duration::from_millis(1), 1));
    let domain_activator = Arc::new(DomainActivator::new(topology.sched_tree()));
    let system_loader = Arc::new(SystemFunctionLoader::new(config.clone()));
    let scheduling_queue = Arc::new(Mutex::new(VecDeque::new()));

    let schedule_mgr = ScheduleManager::new(&config);
    let schedule_decision = ScheduleDecisionManager::new(schedule_mgr.clone());
    let node_manager = NodeManager::new();

    let state = MasterState::new(
        config.clone(),
        is_leader.clone(),
        topology,
        instances,
        domain_sched_mgr,
        local_sched_mgr,
        domain_activator,
        system_loader,
        scheduling_queue,
        snapshots,
        schedule_mgr.clone(),
        schedule_decision.clone(),
        node_manager,
    );
    schedule_mgr.wire_domain_performers(
        tokio::runtime::Handle::current(),
        state.domain_sched_mgr.clone(),
        state.topology.clone(),
        state.is_leader.clone(),
    );
    schedule_decision.apply_topology_resources(&state.topology);
    state.rebuild_domain_routes();

    let health_cancel = shutdown.child_token();
    let nm = state.node_manager.clone();
    let health_cfg = config.clone();
    tokio::spawn(async move {
        let mut int = tokio::time::interval(Duration::from_millis(
            health_cfg.health_monitor_retry_interval.max(500) as u64,
        ));
        loop {
            tokio::select! {
                _ = health_cancel.cancelled() => break,
                _ = int.tick() => {
                    let stale = nm.stale_nodes(health_cfg.domain_heartbeat_timeout as i64);
                    if !stale.is_empty() {
                        tracing::warn!(?stale, "proxy nodes past heartbeat timeout window");
                    }
                    let bad = nm.unhealthy_nodes(health_cfg.health_monitor_max_failure);
                    if !bad.is_empty() {
                        tracing::warn!(?bad, "proxy nodes over failure threshold");
                    }
                }
            }
        }
    });

    if let Some(client) = election_client {
        election::spawn_election_task((*config).clone(), state.clone(), client);
    }

    let grpc_impl = GlobalSchedulerImpl::new(state.clone());
    let grpc_service = GlobalSchedulerServiceServer::new(grpc_impl);

    let grpc_addr: SocketAddr = format!("{}:{}", config.host, config.port)
        .parse()
        .map_err(|e| anyhow::anyhow!("invalid gRPC host/port: {e}"))?;

    let http_addr: SocketAddr = format!("{}:{}", config.host, config.http_port)
        .parse()
        .map_err(|e| anyhow::anyhow!("invalid HTTP host/port: {e}"))?;

    let grpc_cancel = shutdown.child_token();
    // The upstream deploy scripts health-check function_master by curling
    // `http://<GLOBAL_SCHEDULER_PORT>/global-scheduler/healthy`, i.e. the
    // same port used by the global scheduler gRPC service. Use tonic Routes
    // so HTTP compatibility endpoints and real gRPC methods are both served
    // correctly on the C++-compatible port.
    let grpc_health = axum08::Router::new()
        .route("/healthy", axum08::routing::get(|| async { "" }))
        .route(
            "/global-scheduler/healthy",
            axum08::routing::get(|| async { "" }),
        );
    let grpc_routes = Routes::from(grpc_health).add_service(grpc_service);
    let grpc_task = tokio::spawn(async move {
        let serve = Server::builder()
            .accept_http1(true)
            .add_routes(grpc_routes)
            .serve_with_shutdown(grpc_addr, async move {
                grpc_cancel.cancelled().await;
            });
        if let Err(e) = serve.await {
            tracing::error!(error = %e, "combined gRPC/HTTP server error");
        }
    });

    let app = build_router(state.clone(), metastore.clone())
        .layer(tower_http::trace::TraceLayer::new_for_http());
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
        enable_meta_store = config.enable_meta_store,
        "yr-master listening"
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
