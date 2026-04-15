//! Business types for leader observation (port of `functionsystem/src/common/explorer/explorer_actor.h`).
//! LiteBus actors are intentionally omitted.

use async_trait::async_trait;
use std::fmt;

pub const DEFAULT_MASTER_ELECTION_KEY: &str = "/yr/leader/function-master";
pub const FUNCTION_MASTER_K8S_LEASE_NAME: &str = "function-master";
pub const IAM_SERVER_MASTER_ELECTION_KEY: &str = "/yr/leader/function-iam";
pub const IAM_SERVER_K8S_LEASE_NAME: &str = "function-iam";

pub const DEFAULT_ELECT_LEASE_TTL: u32 = 10;
pub const DEFAULT_ELECT_OBSERVE_INTERVAL: u32 = 2;
pub const DEFAULT_ELECT_KEEP_ALIVE_INTERVAL: u32 = 2;

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct LeaderInfo {
    pub name: String,
    pub address: String,
    pub elect_revision: i64,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ElectionInfo {
    pub identity: String,
    pub mode: String,
    pub elect_keep_alive_interval: u32,
    pub elect_lease_ttl: u32,
    pub elect_renew_interval: u32,
}

impl Default for ElectionInfo {
    fn default() -> Self {
        Self {
            identity: String::new(),
            mode: String::new(),
            elect_keep_alive_interval: DEFAULT_ELECT_KEEP_ALIVE_INTERVAL,
            elect_lease_ttl: DEFAULT_ELECT_LEASE_TTL,
            elect_renew_interval: DEFAULT_ELECT_LEASE_TTL,
        }
    }
}

impl fmt::Display for LeaderInfo {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "LeaderInfo(name={}, address={}, rev={})",
            self.name, self.address, self.elect_revision
        )
    }
}

/// Backend-agnostic explorer: watches leadership for etcd, k8s lease, or txn modes.
#[async_trait]
pub trait Explorer: Send + Sync {
    /// Observe leader changes from the underlying implementation (watch / long poll).
    async fn observe(&mut self) -> anyhow::Result<()>;

    /// Publish leader info quickly (e.g. standalone / test double).
    async fn fast_publish(&self, leader: &LeaderInfo) -> anyhow::Result<()>;

    /// Best-effort view of the cached or current leader.
    fn cached_leader(&self) -> Option<LeaderInfo>;
}

#[cfg(test)]
mod tests {
    use super::*;

    struct MemExplorer {
        leader: Option<LeaderInfo>,
    }

    #[async_trait]
    impl Explorer for MemExplorer {
        async fn observe(&mut self) -> anyhow::Result<()> {
            Ok(())
        }
        async fn fast_publish(&self, leader: &LeaderInfo) -> anyhow::Result<()> {
            let _ = leader;
            Ok(())
        }
        fn cached_leader(&self) -> Option<LeaderInfo> {
            self.leader.clone()
        }
    }

    #[tokio::test]
    async fn mem_explorer_trait_object() {
        let mut e = MemExplorer {
            leader: Some(LeaderInfo {
                name: "n".into(),
                address: "127.0.0.1:1".into(),
                elect_revision: 3,
            }),
        };
        e.observe().await.unwrap();
        assert!(e.cached_leader().is_some());
    }

    #[test]
    fn election_info_defaults() {
        let ei = ElectionInfo::default();
        assert_eq!(ei.elect_lease_ttl, DEFAULT_ELECT_LEASE_TTL);
    }
}
