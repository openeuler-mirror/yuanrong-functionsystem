//! Business policy and leader election abstraction (ports of `business_policy.h` / `leader_actor.h`).
//! LiteBus `ActorBase` wiring is intentionally omitted.

use crate::explorer::LeaderInfo;
use async_trait::async_trait;

pub const MASTER_STATUS: &str = "master";
pub const SLAVE_STATUS: &str = "slave";

/// Reacts to master/slave business transitions.
pub trait BusinessPolicy: Send {
    fn on_change(&mut self);
}

/// Abstract leader campaign / renewal behavior for etcd, k8s, or txn backends.
#[async_trait]
pub trait LeaderActor: Send + Sync {
    async fn elect(&mut self) -> anyhow::Result<()>;

    fn proposal_identity(&self) -> &str;

    fn cached_leader_info(&self) -> &LeaderInfo;
}

/// Port of `leader::GetStatus`: returns `None` when status is unchanged, otherwise the new status string.
pub fn master_slave_transition(
    cur_aid_url: &str,
    master_aid_url: &str,
    cur_status: &str,
) -> Option<String> {
    let status = if cur_aid_url == master_aid_url {
        MASTER_STATUS
    } else {
        SLAVE_STATUS
    };
    if cur_status == status {
        None
    } else {
        Some(status.to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct DummyPolicy(bool);
    impl BusinessPolicy for DummyPolicy {
        fn on_change(&mut self) {
            self.0 = true;
        }
    }

    struct DummyLeader {
        proposal: String,
        cached: LeaderInfo,
    }

    #[async_trait]
    impl LeaderActor for DummyLeader {
        async fn elect(&mut self) -> anyhow::Result<()> {
            Ok(())
        }
        fn proposal_identity(&self) -> &str {
            &self.proposal
        }
        fn cached_leader_info(&self) -> &LeaderInfo {
            &self.cached
        }
    }

    #[test]
    fn business_policy_invoked() {
        let mut p = DummyPolicy(false);
        p.on_change();
        assert!(p.0);
    }

    #[tokio::test]
    async fn dummy_leader_elect() {
        let mut l = DummyLeader {
            proposal: "me".into(),
            cached: LeaderInfo::default(),
        };
        l.elect().await.unwrap();
        assert_eq!(l.proposal_identity(), "me");
    }

    #[test]
    fn master_slave_same_returns_none() {
        assert_eq!(
            master_slave_transition("http://a", "http://a", MASTER_STATUS),
            None
        );
    }

    #[test]
    fn master_slave_change_to_slave() {
        assert_eq!(
            master_slave_transition("http://b", "http://a", MASTER_STATUS),
            Some(SLAVE_STATUS.into())
        );
    }
}
