//! Tests for instance / group / bundle state helpers and the transition map.

use std::str::FromStr;

use yr_common::types::{
    instance_state_transition_map, is_non_recoverable_status, is_terminal_status,
    is_waiting_status, need_persistence_state, need_update_route_state, transition_allowed,
    BundleState, GroupState, InstanceState, ResourceGroupState,
};

#[test]
fn instance_state_numeric_sequence_starts_at_zero() {
    assert_eq!(InstanceState::New as i32, 0);
    assert_eq!(InstanceState::Scheduling as i32, 1);
    assert_eq!(InstanceState::Creating as i32, 2);
    assert_eq!(InstanceState::Running as i32, 3);
}

#[test]
fn instance_state_middle_discriminants() {
    assert_eq!(InstanceState::Failed as i32, 4);
    assert_eq!(InstanceState::Exiting as i32, 5);
    assert_eq!(InstanceState::Fatal as i32, 6);
    assert_eq!(InstanceState::ScheduleFailed as i32, 7);
}

#[test]
fn instance_state_tail_discriminants() {
    assert_eq!(InstanceState::Exited as i32, 8);
    assert_eq!(InstanceState::Evicting as i32, 9);
    assert_eq!(InstanceState::Evicted as i32, 10);
    assert_eq!(InstanceState::SubHealth as i32, 11);
    assert_eq!(InstanceState::Suspend as i32, 12);
}

#[test]
fn instance_state_as_i32_matches_discriminant() {
    assert_eq!(InstanceState::Running.as_i32(), InstanceState::Running as i32);
}

#[test]
fn instance_state_display_roundtrip_uppercase() {
    let s = InstanceState::Creating.to_string();
    assert_eq!(s, "CREATING");
    assert_eq!(InstanceState::from_str(&s).unwrap(), InstanceState::Creating);
}

#[test]
fn instance_state_fromstr_lowercase_accepted() {
    assert_eq!(
        InstanceState::from_str("running").unwrap(),
        InstanceState::Running
    );
}

#[test]
fn instance_state_fromstr_mixed_case_accepted() {
    assert_eq!(
        InstanceState::from_str("ScHeDuLiNg").unwrap(),
        InstanceState::Scheduling
    );
}

#[test]
fn instance_state_fromstr_unknown_errors() {
    assert!(InstanceState::from_str("nope").is_err());
}

#[test]
fn group_state_ordering() {
    assert_eq!(GroupState::Scheduling as i32, 0);
    assert!((GroupState::Running as i32) > (GroupState::Scheduling as i32));
}

#[test]
fn group_state_display_values() {
    assert_eq!(GroupState::Failed.to_string(), "FAILED");
    assert_eq!(GroupState::Suspend.to_string(), "SUSPEND");
}

#[test]
fn resource_group_state_values() {
    assert_eq!(ResourceGroupState::Pending as i32, 0);
    assert_eq!(ResourceGroupState::Created as i32, 1);
    assert_eq!(ResourceGroupState::Failed as i32, 2);
}

#[test]
fn bundle_state_values() {
    assert_eq!(BundleState::Pending as i32, 0);
    assert_eq!(BundleState::Created as i32, 1);
    assert_eq!(BundleState::Failed as i32, 2);
}

#[test]
fn transition_new_to_scheduling_valid() {
    assert!(transition_allowed(InstanceState::New, InstanceState::Scheduling));
}

#[test]
fn transition_creating_to_running_valid() {
    assert!(transition_allowed(InstanceState::Creating, InstanceState::Running));
}

#[test]
fn transition_running_to_subhealth_valid() {
    assert!(transition_allowed(InstanceState::Running, InstanceState::SubHealth));
}

#[test]
fn transition_subhealth_back_to_running_valid() {
    assert!(transition_allowed(InstanceState::SubHealth, InstanceState::Running));
}

#[test]
fn transition_running_to_exiting_valid() {
    assert!(transition_allowed(InstanceState::Running, InstanceState::Exiting));
}

#[test]
fn transition_fatal_to_exiting_valid() {
    assert!(transition_allowed(InstanceState::Fatal, InstanceState::Exiting));
}

#[test]
fn transition_evicting_to_evicted_valid() {
    assert!(transition_allowed(InstanceState::Evicting, InstanceState::Evicted));
}

#[test]
fn transition_schedule_failed_to_scheduling_valid() {
    assert!(transition_allowed(
        InstanceState::ScheduleFailed,
        InstanceState::Scheduling
    ));
}

#[test]
fn transition_exited_has_no_outgoing_in_map() {
    assert!(!transition_allowed(InstanceState::Exited, InstanceState::Running));
    assert!(!transition_allowed(InstanceState::Exited, InstanceState::New));
}

#[test]
fn transition_new_to_running_invalid() {
    assert!(!transition_allowed(InstanceState::New, InstanceState::Running));
}

#[test]
fn transition_running_to_new_invalid() {
    assert!(!transition_allowed(InstanceState::Running, InstanceState::New));
}

#[test]
fn transition_exited_to_scheduling_invalid() {
    assert!(!transition_allowed(InstanceState::Exited, InstanceState::Scheduling));
}

#[test]
fn map_cardinality_unchanged() {
    let m = instance_state_transition_map();
    assert_eq!(m.len(), 12);
    let edges: usize = m.values().map(|v| v.len()).sum();
    assert_eq!(edges, 37);
}

#[test]
fn need_update_route_without_meta_skips_creating_only() {
    assert!(!need_update_route_state(InstanceState::Creating, false));
    assert!(need_update_route_state(InstanceState::Running, false));
}

#[test]
fn need_update_route_with_meta_skips_scheduling_and_creating() {
    assert!(!need_update_route_state(InstanceState::Scheduling, true));
    assert!(!need_update_route_state(InstanceState::Creating, true));
    assert!(need_update_route_state(InstanceState::Running, true));
}

#[test]
fn need_persistence_for_new_failed_schedule_failed_only() {
    assert!(need_persistence_state(InstanceState::New));
    assert!(need_persistence_state(InstanceState::Failed));
    assert!(need_persistence_state(InstanceState::ScheduleFailed));
    assert!(!need_persistence_state(InstanceState::Running));
}

#[test]
fn is_non_recoverable_matches_fatal_schedule_failed_evicted() {
    assert!(is_non_recoverable_status(InstanceState::Fatal as i32));
    assert!(is_non_recoverable_status(InstanceState::ScheduleFailed as i32));
    assert!(is_non_recoverable_status(InstanceState::Evicted as i32));
    assert!(!is_non_recoverable_status(InstanceState::Running as i32));
}

#[test]
fn is_waiting_status_covers_in_flight_phases() {
    assert!(is_waiting_status(InstanceState::Scheduling as i32));
    assert!(is_waiting_status(InstanceState::Creating as i32));
    assert!(is_waiting_status(InstanceState::Exiting as i32));
    assert!(is_waiting_status(InstanceState::Evicting as i32));
    assert!(!is_waiting_status(InstanceState::Running as i32));
}

#[test]
fn is_terminal_status_matches_static_set() {
    assert!(is_terminal_status(InstanceState::Exiting));
    assert!(is_terminal_status(InstanceState::Exited));
    assert!(is_terminal_status(InstanceState::Fatal));
    assert!(!is_terminal_status(InstanceState::Running));
}

#[test]
fn instance_state_equality() {
    assert_eq!(InstanceState::Failed, InstanceState::Failed);
    assert_ne!(InstanceState::Failed, InstanceState::Running);
}
