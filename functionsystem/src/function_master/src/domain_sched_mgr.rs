//! Domain scheduler registration and forward scheduling (`domain_sched_mgr*.cpp` analogue).

use std::time::Duration;

use dashmap::DashMap;
use tonic::transport::Channel;
use tracing::{debug, warn};
use yr_proto::internal::domain_scheduler_service_client::DomainSchedulerServiceClient;
use yr_proto::internal::{
    GroupScheduleRequest, GroupScheduleResponse, ScheduleRequest, ScheduleResponse,
};

#[derive(Debug, Clone)]
pub struct DomainEndpoint {
    pub grpc_uri: String,
}

/// Tracks domain scheduler endpoints and forwards schedule RPCs with retry/timeout.
pub struct DomainSchedMgr {
    by_name: DashMap<String, DomainEndpoint>,
    schedule_timeout: Duration,
    group_retry: Duration,
}

impl DomainSchedMgr {
    pub fn new(schedule_timeout: Duration, group_retry: Duration) -> Self {
        Self {
            by_name: DashMap::new(),
            schedule_timeout,
            group_retry,
        }
    }

    pub fn register(&self, name: impl Into<String>, grpc_uri: impl Into<String>) {
        let name = name.into();
        let grpc_uri = normalize_uri(grpc_uri.into());
        debug!(%name, %grpc_uri, "domain_sched_mgr: register");
        self.by_name.insert(name, DomainEndpoint { grpc_uri });
    }

    pub fn unregister(&self, name: &str) {
        self.by_name.remove(name);
    }

    pub fn disconnect_all(&self) {
        self.by_name.clear();
    }

    pub fn endpoint_for(&self, name: &str, fallback_address: &str) -> Option<String> {
        if let Some(e) = self.by_name.get(name) {
            return Some(e.grpc_uri.clone());
        }
        if !fallback_address.is_empty() {
            return Some(normalize_uri(fallback_address.to_string()));
        }
        None
    }

    /// C++ `DomainSchedMgr::Schedule` — forward to domain with bounded retries.
    pub async fn forward_schedule(
        &self,
        domain_name: &str,
        domain_address: &str,
        req: ScheduleRequest,
    ) -> Result<ScheduleResponse, String> {
        let uri = self
            .endpoint_for(domain_name, domain_address)
            .ok_or_else(|| "no domain endpoint".to_string())?;
        let mut attempt = 0u32;
        loop {
            attempt += 1;
            let channel = Channel::from_shared(uri.clone())
                .map_err(|e| e.to_string())?
                .connect()
                .await
                .map_err(|e| format!("connect {uri}: {e}"))?;
            let mut client = DomainSchedulerServiceClient::new(channel);
            match tokio::time::timeout(
                self.schedule_timeout,
                client.schedule(tonic::Request::new(req.clone())),
            )
            .await
            {
                Ok(Ok(resp)) => return Ok(resp.into_inner()),
                Ok(Err(e)) if attempt >= 3 => return Err(e.to_string()),
                Ok(Err(e)) => {
                    warn!(error = %e, attempt, "domain_sched_mgr: schedule rpc error, retry");
                    tokio::time::sleep(Duration::from_millis(50 * attempt as u64)).await;
                }
                Err(_) if attempt >= 3 => return Err("schedule timeout".into()),
                Err(_) => {
                    warn!(attempt, "domain_sched_mgr: schedule timeout, retry");
                    tokio::time::sleep(Duration::from_millis(50 * attempt as u64)).await;
                }
            }
        }
    }

    /// Group schedule with retry cycle (C++ `DoGroupSchedule` defer).
    pub async fn forward_group_schedule(
        &self,
        domain_name: &str,
        domain_address: &str,
        req: GroupScheduleRequest,
    ) -> Result<GroupScheduleResponse, String> {
        let uri = self
            .endpoint_for(domain_name, domain_address)
            .ok_or_else(|| "no domain endpoint".to_string())?;
        loop {
            let channel = Channel::from_shared(uri.clone())
                .map_err(|e| e.to_string())?
                .connect()
                .await
                .map_err(|e| format!("connect {uri}: {e}"))?;
            let mut client = DomainSchedulerServiceClient::new(channel);
            match tokio::time::timeout(
                self.schedule_timeout.saturating_mul(2),
                client.group_schedule(tonic::Request::new(req.clone())),
            )
            .await
            {
                Ok(Ok(resp)) => return Ok(resp.into_inner()),
                Ok(Err(e)) => {
                    warn!(error = %e, "domain_sched_mgr: group_schedule error, retry");
                }
                Err(_) => warn!("domain_sched_mgr: group_schedule timeout, retry"),
            }
            tokio::time::sleep(self.group_retry).await;
        }
    }

    pub fn heartbeat_tick(&self) {
        tracing::warn!(
            "domain scheduler heartbeat not implemented — no periodic heartbeat to root domain"
        );
    }

    pub fn push_topology_digest(&self, _digest: &[u8]) {
        tracing::warn!(
            "domain scheduler topology digest push not implemented — domains are not notified of topology version"
        );
    }
}

fn normalize_uri(mut s: String) -> String {
    if s.starts_with("http://") || s.starts_with("https://") {
        return s;
    }
    if s.contains("://") {
        return s;
    }
    s.insert_str(0, "http://");
    s
}
