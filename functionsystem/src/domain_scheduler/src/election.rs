use std::sync::Arc;
use std::time::Duration;

use etcd_client::{Client, Compare, CompareOp, ProclaimOptions, PutOptions, ResignOptions, Txn, TxnOp};
use tracing::{error, info, warn};

use crate::config::{DomainSchedulerConfig, ElectionMode};
use crate::state::DomainSchedulerState;

/// Runs etcd v3 election. While leader, scheduling RPCs are active; followers stay passive.
pub fn spawn_election_task(
    cfg: DomainSchedulerConfig,
    state: Arc<DomainSchedulerState>,
    mut client: Client,
) {
    if matches!(cfg.election_mode, ElectionMode::Standalone) {
        return;
    }

    if matches!(cfg.election_mode, ElectionMode::Txn) {
        spawn_txn_election(cfg, state, client);
        return;
    }

    let election_name = cfg.election_name();
    let instance = cfg.instance_id.clone();

    tokio::spawn(async move {
        loop {
            state.set_leader(false);
            let lease_ttl: i64 = 10;
            let lease_id = match client.lease_grant(lease_ttl, None).await {
                Ok(r) => r.id(),
                Err(e) => {
                    error!(error = %e, "yr-domain-scheduler election: lease_grant failed");
                    tokio::time::sleep(Duration::from_secs(2)).await;
                    continue;
                }
            };

            let (mut keeper, mut ka_stream) = match client.lease_keep_alive(lease_id).await {
                Ok(x) => x,
                Err(e) => {
                    error!(error = %e, "yr-domain-scheduler election: lease_keep_alive failed");
                    tokio::time::sleep(Duration::from_secs(2)).await;
                    continue;
                }
            };

            let keep_task = tokio::spawn(async move {
                loop {
                    if let Err(e) = keeper.keep_alive().await {
                        warn!(error = %e, "yr-domain-scheduler election: keep_alive send failed");
                        break;
                    }
                    match ka_stream.message().await {
                        Ok(Some(_)) => {}
                        Ok(None) => break,
                        Err(e) => {
                            warn!(error = %e, "yr-domain-scheduler election: keep_alive stream failed");
                            break;
                        }
                    }
                }
            });

            let campaign_res = client
                .campaign(election_name.clone(), instance.as_bytes().to_vec(), lease_id)
                .await;

            let leader_key = match campaign_res {
                Ok(mut resp) => match resp.take_leader() {
                    Some(k) => k,
                    None => {
                        warn!("yr-domain-scheduler election: campaign returned no leader key");
                        keep_task.abort();
                        let _ = client.lease_revoke(lease_id).await;
                        tokio::time::sleep(Duration::from_secs(2)).await;
                        continue;
                    }
                },
                Err(e) => {
                    error!(error = %e, "yr-domain-scheduler election: campaign failed");
                    keep_task.abort();
                    let _ = client.lease_revoke(lease_id).await;
                    tokio::time::sleep(Duration::from_secs(2)).await;
                    continue;
                }
            };

            if let Err(e) = client
                .proclaim(
                    instance.as_bytes().to_vec(),
                    Some(ProclaimOptions::new().with_leader(leader_key.clone())),
                )
                .await
            {
                error!(error = %e, "yr-domain-scheduler election: proclaim failed");
                keep_task.abort();
                let _ = client
                    .resign(Some(ResignOptions::new().with_leader(leader_key)))
                    .await;
                let _ = client.lease_revoke(lease_id).await;
                tokio::time::sleep(Duration::from_secs(2)).await;
                continue;
            }

            state.set_leader(true);
            info!(instance = %instance, "yr-domain-scheduler is etcd election leader");

            keep_task.await.ok();

            state.set_leader(false);
            warn!("yr-domain-scheduler election: lease keep-alive ended; resigning and retrying");

            let _ = client
                .resign(Some(ResignOptions::new().with_leader(leader_key)))
                .await;
            let _ = client.lease_revoke(lease_id).await;
            tokio::time::sleep(Duration::from_secs(1)).await;
        }
    });
}

fn spawn_txn_election(cfg: DomainSchedulerConfig, state: Arc<DomainSchedulerState>, mut client: Client) {
    let election_key = cfg.txn_election_key().into_bytes();
    let instance = cfg.instance_id.clone();

    tokio::spawn(async move {
        loop {
            state.set_leader(false);
            let lease_ttl: i64 = 10;
            let lease_id = match client.lease_grant(lease_ttl, None).await {
                Ok(r) => r.id(),
                Err(e) => {
                    error!(error = %e, "yr-domain-scheduler txn election: lease_grant failed");
                    tokio::time::sleep(Duration::from_secs(2)).await;
                    continue;
                }
            };

            let (mut keeper, mut ka_stream) = match client.lease_keep_alive(lease_id).await {
                Ok(x) => x,
                Err(e) => {
                    error!(error = %e, "yr-domain-scheduler txn election: lease_keep_alive failed");
                    let _ = client.lease_revoke(lease_id).await;
                    tokio::time::sleep(Duration::from_secs(2)).await;
                    continue;
                }
            };

            let keep_task = tokio::spawn(async move {
                loop {
                    if let Err(e) = keeper.keep_alive().await {
                        warn!(error = %e, "yr-domain-scheduler txn election: keep_alive send failed");
                        break;
                    }
                    match ka_stream.message().await {
                        Ok(Some(_)) => {}
                        Ok(None) => break,
                        Err(e) => {
                            warn!(error = %e, "yr-domain-scheduler txn election: keep_alive stream failed");
                            break;
                        }
                    }
                }
            });

            let txn = Txn::new()
                .when([Compare::create_revision(
                    election_key.clone(),
                    CompareOp::Equal,
                    0,
                )])
                .and_then([TxnOp::put(
                    election_key.clone(),
                    instance.as_bytes().to_vec(),
                    Some(PutOptions::new().with_lease(lease_id)),
                )])
                .or_else([]);

            let acquired = match client.txn(txn).await {
                Ok(resp) => resp.succeeded(),
                Err(e) => {
                    error!(error = %e, "yr-domain-scheduler txn election: txn failed");
                    keep_task.abort();
                    let _ = client.lease_revoke(lease_id).await;
                    tokio::time::sleep(Duration::from_secs(2)).await;
                    continue;
                }
            };

            if !acquired {
                keep_task.abort();
                let _ = client.lease_revoke(lease_id).await;
                tokio::time::sleep(Duration::from_secs(1)).await;
                continue;
            }

            state.set_leader(true);
            info!(instance = %instance, "yr-domain-scheduler is txn election leader");

            keep_task.await.ok();

            state.set_leader(false);
            warn!("yr-domain-scheduler txn election: lease keep-alive ended; retrying");

            let _ = client.lease_revoke(lease_id).await;
            tokio::time::sleep(Duration::from_secs(1)).await;
        }
    });
}
