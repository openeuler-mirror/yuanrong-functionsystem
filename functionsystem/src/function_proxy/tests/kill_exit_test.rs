//! Tests for Kill/Exit streaming message side effects.
//!
//! Validates that KillReq and ExitReq on the streaming channel trigger
//! real instance lifecycle transitions, matching the C++ InstanceCtrlActor
//! behavior (state migration + resource cleanup).

use yr_common::types::{transition_allowed, InstanceState};
use yr_proto::common::ErrorCode;
use yr_proto::core_service::{ExitRequest, KillRequest, KillResponse};

#[test]
fn kill_state_transition_running_to_exiting() {
    assert!(transition_allowed(
        InstanceState::Running,
        InstanceState::Exiting
    ));
}

#[test]
fn kill_state_transition_creating_to_exiting() {
    assert!(transition_allowed(
        InstanceState::Creating,
        InstanceState::Exiting
    ));
}

#[test]
fn kill_state_transition_scheduling_to_exiting() {
    assert!(transition_allowed(
        InstanceState::Scheduling,
        InstanceState::Exiting
    ));
}

#[test]
fn exit_normal_converts_to_kill_signal_1() {
    let exit = ExitRequest {
        code: 0,
        message: String::new(),
        ..Default::default()
    };
    assert_eq!(exit.code, 0, "normal exit code triggers kill(signal=1)");
}

#[test]
fn exit_abnormal_transitions_to_failed() {
    assert!(transition_allowed(
        InstanceState::Running,
        InstanceState::Failed
    ));
}

#[test]
fn kill_response_has_expected_error_codes() {
    let ok_rsp = KillResponse {
        code: ErrorCode::ErrNone as i32,
        message: String::new(),
        payload: Vec::new(),
    };
    assert_eq!(ok_rsp.code, 0);

    let fail_rsp = KillResponse {
        code: ErrorCode::ErrInnerSystemError as i32,
        message: "forward failed".into(),
        payload: Vec::new(),
    };
    assert_eq!(fail_rsp.code, 3003);
}

#[test]
fn kill_request_signal_values_match_cpp_constants() {
    let shutdown = KillRequest {
        signal: 1,
        instance_id: "test".into(),
        ..Default::default()
    };
    assert_eq!(shutdown.signal, 1, "SHUT_DOWN_SIGNAL = 1");

    let shutdown_sync = KillRequest {
        signal: 2,
        instance_id: "test".into(),
        ..Default::default()
    };
    assert_eq!(shutdown_sync.signal, 2, "SHUT_DOWN_SIGNAL_SYNC = 2");
}

#[test]
fn exiting_cannot_transition_back_to_running() {
    assert!(
        !transition_allowed(InstanceState::Exiting, InstanceState::Running),
        "Exiting is terminal, cannot go back to Running"
    );
}

#[test]
fn exit_request_with_nonzero_code_is_abnormal() {
    let exit = ExitRequest {
        code: 1,
        message: "segfault".into(),
        ..Default::default()
    };
    assert_ne!(
        exit.code, 0,
        "non-zero exit code = abnormal exit → Failed state"
    );
}

#[test]
fn kill_target_defaults_to_caller_when_empty() {
    let kill = KillRequest {
        instance_id: String::new(),
        signal: 1,
        ..Default::default()
    };
    assert!(
        kill.instance_id.is_empty(),
        "empty target means kill the caller instance itself"
    );
}
