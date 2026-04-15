use crate::resource_reporter;
use crate::AppContext;
use std::sync::Arc;
use std::time::Duration;
use tracing::{info, warn};
use yr_proto::internal::domain_scheduler_service_client::DomainSchedulerServiceClient;
use yr_proto::internal::global_scheduler_service_client::GlobalSchedulerServiceClient;
use yr_proto::internal::{
    HeartbeatPing, RegisterRequest, UpdateResourcesRequest, WorkerStatusNotification,
};

/// Register with global scheduler, apply topology, register as worker with domain scheduler, heartbeat.
pub async fn run_registration_and_heartbeat(ctx: Arc<AppContext>) {
    let global = ctx.config.global_scheduler_address.trim().to_string();
    if global.is_empty() {
        info!("global_scheduler_address empty; skipping registration");
        return;
    }

    let mut interval = tokio::time::interval(Duration::from_secs(5));
    loop {
        interval.tick().await;
        match register_global_and_domain(&ctx, &global).await {
            Ok(()) => break,
            Err(e) => warn!(error = %e, "register with global scheduler"),
        }
    }

    let mut beat = tokio::time::interval(Duration::from_secs(10));
    loop {
        beat.tick().await;
        if let Err(e) = push_resources(&ctx, &global).await {
            warn!(error = %e, "update resources on global scheduler");
        }
        let domain = ctx.domain_addr.read().clone();
        let domain = domain.trim().to_string();
        if domain.is_empty() {
            continue;
        }
        if let Err(e) = domain_heartbeat_and_worker_status(&ctx, &domain).await {
            warn!(error = %e, "domain scheduler heartbeat / worker status");
            // Leader / domain changes: try global register again (re-registration).
            if let Err(e2) = register_global_and_domain(&ctx, &global).await {
                warn!(error = %e2, "re-register after heartbeat failure");
            }
        }
    }
}

async fn register_global_and_domain(ctx: &AppContext, global: &str) -> anyhow::Result<()> {
    let mut client = GlobalSchedulerServiceClient::connect(global.to_string()).await?;
    let resource_json = resource_reporter::build_resource_report_json_arc(
        &ctx.config.node_id,
        &ctx.resource_view,
        &ctx.instance_ctrl,
    );
    let agent_info_json = ctx.agent_manager.list_json();
    let resp = client
        .register(RegisterRequest {
            node_id: ctx.config.node_id.clone(),
            address: ctx.config.advertise_grpc_endpoint(),
            resource_json,
            agent_info_json,
        })
        .await?
        .into_inner();
    if !resp.success {
        anyhow::bail!("register refused: {}", resp.message);
    }
    if !resp.topology.trim().is_empty() {
        match serde_json::from_str::<serde_json::Value>(&resp.topology) {
            Ok(v) => {
                *ctx.topology.write() = Some(v.clone());
                if let Some(addr) = v.get("leader").and_then(|l| l.get("address")).and_then(|a| a.as_str()) {
                    if !addr.is_empty() {
                        info!(leader = %addr, "topology leader from global register");
                    }
                }
            }
            Err(e) => warn!(error = %e, "parse topology JSON from global"),
        }
    }
    if !resp.domain_address.is_empty() {
        *ctx.domain_addr.write() = resp.domain_address.clone();
    } else if !ctx.config.domain_scheduler_address.trim().is_empty() {
        *ctx.domain_addr.write() = ctx.config.domain_scheduler_address.trim().to_string();
    }
    info!(
        domain = %ctx.domain_addr.read(),
        "registered with global scheduler"
    );

    register_domain_worker(ctx).await?;
    Ok(())
}

/// Worker registration / presence on domain scheduler (companion to global register).
async fn register_domain_worker(ctx: &AppContext) -> anyhow::Result<()> {
    let domain = ctx.domain_addr.read().clone();
    let domain = domain.trim().to_string();
    if domain.is_empty() {
        return Ok(());
    }
    let mut client = DomainSchedulerServiceClient::connect(domain).await?;
    let _ = client
        .notify_worker_status(WorkerStatusNotification {
            node_id: ctx.config.node_id.clone(),
            status: "ready".into(),
            reason: "post-global-register".into(),
        })
        .await?;
    Ok(())
}

async fn push_resources(ctx: &AppContext, global: &str) -> anyhow::Result<()> {
    let mut client = GlobalSchedulerServiceClient::connect(global.to_string()).await?;
    let resource_json = resource_reporter::build_resource_report_json_arc(
        &ctx.config.node_id,
        &ctx.resource_view,
        &ctx.instance_ctrl,
    );
    let _ = client
        .update_resources(UpdateResourcesRequest {
            node_id: ctx.config.node_id.clone(),
            resource_json,
        })
        .await?;
    Ok(())
}

async fn domain_heartbeat_and_worker_status(ctx: &AppContext, domain: &str) -> anyhow::Result<()> {
    let mut client = DomainSchedulerServiceClient::connect(domain.to_string()).await?;
    let _ = client
        .heartbeat(HeartbeatPing {
            node_id: ctx.config.node_id.clone(),
            timestamp_ms: crate::state_machine::InstanceMetadata::now_ms(),
        })
        .await?;
    let _ = client
        .notify_worker_status(WorkerStatusNotification {
            node_id: ctx.config.node_id.clone(),
            status: "healthy".into(),
            reason: String::new(),
        })
        .await?;
    Ok(())
}
