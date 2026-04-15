use std::sync::Arc;
use std::time::Duration;

use etcd_client::{Client, Compare, CompareOp, ProclaimOptions, PutOptions, ResignOptions, Txn, TxnOp};

/// Seconds granted to the txn-election etcd lease (see `spawn_txn_election`).
pub const TXN_ELECTION_LEASE_TTL_SECS: i64 = 10;
use tracing::{error, info, warn};

use crate::config::{ElectionMode, MasterConfig};
use crate::scheduler::MasterState;

/// Runs etcd v3 election. While leader, scheduling mutations are active; followers stay passive.
pub fn spawn_election_task(cfg: MasterConfig, state: Arc<MasterState>, mut client: Client) {
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
                    error!(error = %e, "yr-master election: lease_grant failed");
                    tokio::time::sleep(Duration::from_secs(2)).await;
                    continue;
                }
            };

            let (mut keeper, mut ka_stream) = match client.lease_keep_alive(lease_id).await {
                Ok(x) => x,
                Err(e) => {
                    error!(error = %e, "yr-master election: lease_keep_alive failed");
                    tokio::time::sleep(Duration::from_secs(2)).await;
                    continue;
                }
            };

            let keep_task = tokio::spawn(async move {
                loop {
                    if let Err(e) = keeper.keep_alive().await {
                        warn!(error = %e, "yr-master election: keep_alive send failed");
                        break;
                    }
                    match ka_stream.message().await {
                        Ok(Some(_)) => {}
                        Ok(None) => break,
                        Err(e) => {
                            warn!(error = %e, "yr-master election: keep_alive stream failed");
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
                        warn!("yr-master election: campaign returned no leader key");
                        keep_task.abort();
                        let _ = client.lease_revoke(lease_id).await;
                        tokio::time::sleep(Duration::from_secs(2)).await;
                        continue;
                    }
                },
                Err(e) => {
                    error!(error = %e, "yr-master election: campaign failed");
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
                error!(error = %e, "yr-master election: proclaim failed");
                keep_task.abort();
                let _ = client
                    .resign(Some(ResignOptions::new().with_leader(leader_key)))
                    .await;
                let _ = client.lease_revoke(lease_id).await;
                tokio::time::sleep(Duration::from_secs(2)).await;
                continue;
            }

            state.set_leader(true);
            info!(instance = %instance, "yr-master is etcd election leader (scheduling active)");

            keep_task.await.ok();

            state.set_leader(false);
            warn!("yr-master election: lease keep-alive ended; resigning and retrying");

            let _ = client
                .resign(Some(ResignOptions::new().with_leader(leader_key)))
                .await;
            let _ = client.lease_revoke(lease_id).await;
            tokio::time::sleep(Duration::from_secs(1)).await;
        }
    });
}

fn spawn_txn_election(cfg: MasterConfig, state: Arc<MasterState>, mut client: Client) {
    let election_key = cfg.txn_election_key().into_bytes();
    let holder = if !cfg.node_id.is_empty() {
        cfg.node_id.clone()
    } else {
        cfg.instance_id.clone()
    };

    tokio::spawn(async move {
        loop {
            state.set_leader(false);
            let lease_id = match client
                .lease_grant(TXN_ELECTION_LEASE_TTL_SECS, None)
                .await
            {
                Ok(r) => r.id(),
                Err(e) => {
                    error!(error = %e, "yr-master txn election: lease_grant failed");
                    tokio::time::sleep(Duration::from_secs(2)).await;
                    continue;
                }
            };

            let (mut keeper, mut ka_stream) = match client.lease_keep_alive(lease_id).await {
                Ok(x) => x,
                Err(e) => {
                    error!(error = %e, "yr-master txn election: lease_keep_alive failed");
                    let _ = client.lease_revoke(lease_id).await;
                    tokio::time::sleep(Duration::from_secs(2)).await;
                    continue;
                }
            };

            let keep_task = tokio::spawn(async move {
                loop {
                    if let Err(e) = keeper.keep_alive().await {
                        warn!(error = %e, "yr-master txn election: keep_alive send failed");
                        break;
                    }
                    match ka_stream.message().await {
                        Ok(Some(_)) => {}
                        Ok(None) => break,
                        Err(e) => {
                            warn!(error = %e, "yr-master txn election: keep_alive stream failed");
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
                    holder.as_bytes().to_vec(),
                    Some(PutOptions::new().with_lease(lease_id)),
                )])
                .or_else([]);

            let acquired = match client.txn(txn).await {
                Ok(resp) => resp.succeeded(),
                Err(e) => {
                    error!(error = %e, "yr-master txn election: txn failed");
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
            info!(holder = %holder, "yr-master is txn election leader (scheduling active)");

            keep_task.await.ok();

            state.set_leader(false);
            warn!("yr-master txn election: lease keep-alive ended; retrying");

            let _ = client.lease_revoke(lease_id).await;
            tokio::time::sleep(Duration::from_secs(1)).await;
        }
    });
}
