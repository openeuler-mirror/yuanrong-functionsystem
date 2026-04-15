//! Instance / group / bundle state types from `common/types/instance_state.h` and `common/types/common_state.h`,
//! plus the `STATE_TRANSITION_MAP` from `instance_state_machine.cpp`.

use std::collections::{HashMap, HashSet};
use std::fmt;
use std::str::FromStr;
use std::sync::OnceLock;

/// `enum class InstanceState` — numeric values follow C++ `int32_t` assignment order (0..=12).
#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum InstanceState {
    New = 0,
    Scheduling,
    Creating,
    Running,
    Failed,
    Exiting,
    Fatal,
    ScheduleFailed,
    Exited,
    Evicting,
    Evicted,
    SubHealth,
    Suspend,
}

impl InstanceState {
    pub fn as_i32(self) -> i32 {
        self as i32
    }
}

impl fmt::Display for InstanceState {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            InstanceState::New => "NEW",
            InstanceState::Scheduling => "SCHEDULING",
            InstanceState::Creating => "CREATING",
            InstanceState::Running => "RUNNING",
            InstanceState::Failed => "FAILED",
            InstanceState::Exiting => "EXITING",
            InstanceState::Fatal => "FATAL",
            InstanceState::ScheduleFailed => "SCHEDULE_FAILED",
            InstanceState::Exited => "EXITED",
            InstanceState::Evicting => "EVICTING",
            InstanceState::Evicted => "EVICTED",
            InstanceState::SubHealth => "SUB_HEALTH",
            InstanceState::Suspend => "SUSPEND",
        })
    }
}

impl FromStr for InstanceState {
    type Err = ();

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s.to_ascii_uppercase().as_str() {
            "NEW" => Ok(InstanceState::New),
            "SCHEDULING" => Ok(InstanceState::Scheduling),
            "CREATING" => Ok(InstanceState::Creating),
            "RUNNING" => Ok(InstanceState::Running),
            "FAILED" => Ok(InstanceState::Failed),
            "EXITING" => Ok(InstanceState::Exiting),
            "FATAL" => Ok(InstanceState::Fatal),
            "SCHEDULE_FAILED" => Ok(InstanceState::ScheduleFailed),
            "EXITED" => Ok(InstanceState::Exited),
            "EVICTING" => Ok(InstanceState::Evicting),
            "EVICTED" => Ok(InstanceState::Evicted),
            "SUB_HEALTH" => Ok(InstanceState::SubHealth),
            "SUSPEND" => Ok(InstanceState::Suspend),
            _ => Err(()),
        }
    }
}

#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum GroupState {
    Scheduling = 0,
    Running,
    Failed,
    Suspend,
}

impl fmt::Display for GroupState {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            GroupState::Scheduling => "SCHEDULING",
            GroupState::Running => "RUNNING",
            GroupState::Failed => "FAILED",
            GroupState::Suspend => "SUSPEND",
        })
    }
}

#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ResourceGroupState {
    Pending = 0,
    Created,
    Failed,
}

#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum BundleState {
    Pending = 0,
    Created,
    Failed,
}

pub const INSTANCE_MANAGER_OWNER: &str = "InstanceManagerOwner";
pub const GROUP_MANAGER_OWNER: &str = "GroupManagerOwner";

pub fn no_update_route_state() -> &'static HashSet<InstanceState> {
    static S: OnceLock<HashSet<InstanceState>> = OnceLock::new();
    S.get_or_init(|| HashSet::from([InstanceState::Creating]))
}

pub fn no_update_route_state_with_meta_store() -> &'static HashSet<InstanceState> {
    static S: OnceLock<HashSet<InstanceState>> = OnceLock::new();
    S.get_or_init(|| HashSet::from([InstanceState::Scheduling, InstanceState::Creating]))
}

pub fn persistence_state_set() -> &'static HashSet<InstanceState> {
    static S: OnceLock<HashSet<InstanceState>> = OnceLock::new();
    S.get_or_init(|| {
        HashSet::from([
            InstanceState::New,
            InstanceState::Failed,
            InstanceState::ScheduleFailed,
        ])
    })
}

pub fn terminal_instance_states() -> &'static HashSet<InstanceState> {
    static S: OnceLock<HashSet<InstanceState>> = OnceLock::new();
    S.get_or_init(|| {
        HashSet::from([
            InstanceState::Exiting,
            InstanceState::Exited,
            InstanceState::Evicting,
            InstanceState::Evicted,
            InstanceState::Fatal,
        ])
    })
}

pub fn need_update_route_state(state: InstanceState, meta_store_enabled: bool) -> bool {
    if meta_store_enabled {
        !no_update_route_state_with_meta_store().contains(&state)
    } else {
        !no_update_route_state().contains(&state)
    }
}

pub fn need_persistence_state(state: InstanceState) -> bool {
    persistence_state_set().contains(&state)
}

pub fn is_non_recoverable_status(code: i32) -> bool {
    code == InstanceState::Fatal as i32
        || code == InstanceState::ScheduleFailed as i32
        || code == InstanceState::Evicted as i32
}

pub fn is_waiting_status(code: i32) -> bool {
    code == InstanceState::Scheduling as i32
        || code == InstanceState::Creating as i32
        || code == InstanceState::Exiting as i32
        || code == InstanceState::Evicting as i32
}

pub fn is_terminal_status(state: InstanceState) -> bool {
    terminal_instance_states().contains(&state)
}

/// Full `STATE_TRANSITION_MAP` from `instance_state_machine.cpp` (static initializer).
pub fn instance_state_transition_map() -> &'static HashMap<InstanceState, HashSet<InstanceState>> {
    static MAP: OnceLock<HashMap<InstanceState, HashSet<InstanceState>>> = OnceLock::new();
    MAP.get_or_init(|| {
        use InstanceState::*;
        HashMap::from([
            (New, HashSet::from([Scheduling])),
            (
                Scheduling,
                HashSet::from([
                    Scheduling,
                    Creating,
                    Failed,
                    Fatal,
                    Exiting,
                    ScheduleFailed,
                ]),
            ),
            (
                Creating,
                HashSet::from([Running, Failed, Exiting, Fatal]),
            ),
            (
                Running,
                HashSet::from([
                    Failed,
                    Exiting,
                    Fatal,
                    Evicting,
                    SubHealth,
                    Suspend,
                ]),
            ),
            (
                SubHealth,
                HashSet::from([Failed, Exiting, Fatal, Evicting, Running]),
            ),
            (Failed, HashSet::from([Scheduling, Exiting, Fatal])),
            (Fatal, HashSet::from([Exiting])),
            (Exiting, HashSet::from([Fatal])),
            (Evicting, HashSet::from([Evicted, Fatal])),
            (ScheduleFailed, HashSet::from([Scheduling, Exiting])),
            (Evicted, HashSet::from([Exiting, Fatal])),
            (
                Suspend,
                HashSet::from([Creating, Scheduling, Fatal, Exiting]),
            ),
        ])
    })
}

pub fn transition_allowed(from: InstanceState, to: InstanceState) -> bool {
    instance_state_transition_map()
        .get(&from)
        .is_some_and(|set| set.contains(&to))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn instance_state_display_fromstr_roundtrip() {
        use InstanceState::*;
        for s in [
            New,
            Scheduling,
            Creating,
            Running,
            Failed,
            Exiting,
            Fatal,
            ScheduleFailed,
            Exited,
            Evicting,
            Evicted,
            SubHealth,
            Suspend,
        ] {
            let t = s.to_string();
            assert_eq!(InstanceState::from_str(&t).unwrap(), s);
        }
    }

    #[test]
    fn full_transition_table_matches_cpp_cardinality() {
        let m = instance_state_transition_map();
        assert_eq!(m.len(), 12);
        let total_edges: usize = m.values().map(|v| v.len()).sum();
        assert_eq!(total_edges, 37);
    }

    #[test]
    fn sample_transitions() {
        assert!(transition_allowed(InstanceState::New, InstanceState::Scheduling));
        assert!(transition_allowed(InstanceState::Running, InstanceState::SubHealth));
        assert!(!transition_allowed(InstanceState::New, InstanceState::Running));
        assert!(!transition_allowed(InstanceState::Exited, InstanceState::Running));
    }
}
