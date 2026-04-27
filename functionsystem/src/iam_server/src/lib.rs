#![allow(clippy::result_large_err)]

pub mod aksk;
pub mod config;
pub mod election;
pub mod routes;
pub mod state;
pub mod token;
pub mod token_store;
pub mod token_watch;
pub mod user_manager;

use std::cmp;
use std::net::SocketAddr;
use std::sync::Arc;

use etcd_client::Client;
use tokio::net::TcpListener;
use tracing::info;
use yr_common::logging::init_logging;
use yr_metastore_client::{MetaStoreClient, MetaStoreClientConfig};

use crate::config::{CliArgs, ElectionMode, IamConfig, IamCredentialType};
use crate::routes::build_router;
use crate::state::AppState;
use crate::token::TokenManager;

pub async fn run() -> anyhow::Result<()> {
    init_logging();
    let cli = yr_common::cli_compat::parse_with_legacy_flags::<CliArgs>(
        yr_common::cli_compat::legacy_flags::IAM_SERVER,
    );
    let config = IamConfig::from_cli(cli).map_err(|e| anyhow::anyhow!(e))?;
    config.validate().map_err(|e| anyhow::anyhow!(e))?;

    let metastore = if config.enable_iam {
        let ms_cfg = MetaStoreClientConfig {
            enable_meta_store: false,
            is_passthrough: false,
            etcd_address: config.etcd_endpoints.join(","),
            meta_store_address: String::new(),
            etcd_table_prefix: config.etcd_table_prefix.clone(),
            excluded_keys: vec![],
            ssl_config: None,
        };
        let c = MetaStoreClient::connect(ms_cfg)
            .await
            .map_err(|e| anyhow::anyhow!("etcd connect: {e}"))?;
        Some(c)
    } else {
        None
    };

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

    let state = Arc::new(AppState::new(config.clone(), metastore));

    if let Some(client) = election_client {
        election::spawn_election_task(config.clone(), state.clone(), client);
    }

    if config.enable_iam
        && matches!(
            config.iam_credential_type,
            IamCredentialType::Token | IamCredentialType::Both
        )
    {
        let st = state.clone();
        let period_secs = cmp::max(config.token_ttl_default.as_secs().saturating_div(4), 30);
        tokio::spawn(async move {
            let mut interval = tokio::time::interval(std::time::Duration::from_secs(period_secs));
            loop {
                interval.tick().await;
                let Some(ms) = st.metastore.as_ref() else {
                    continue;
                };
                let mut g = ms.lock().await;
                TokenManager::rotation_tick(&mut g, &st.config).await;
            }
        });

        let stw = state.clone();
        tokio::spawn(token_watch::run(stw));
    }

    let app = build_router(state).layer(tower_http::trace::TraceLayer::new_for_http());

    let addr: SocketAddr = format!("{}:{}", config.host, config.port)
        .parse()
        .map_err(|e| anyhow::anyhow!("invalid host/port: {e}"))?;

    info!(%addr, election_mode = ?config.election_mode, enable_iam = config.enable_iam, "yr-iam listening");

    let listener = TcpListener::bind(addr).await?;
    axum::serve(listener, app).await?;
    Ok(())
}
