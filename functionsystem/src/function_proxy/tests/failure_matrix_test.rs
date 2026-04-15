//! ST: proxy_failure_matrix
//! Protocol-level contracts for yr-proxy failure semantics (P0-4 in 47-ai-execution-guidance.md).
//! No live gRPC — validates error codes, state transitions, and response shapes via yr_proto / yr_common.

use yr_common::types::{transition_allowed, InstanceState};
use yr_proto::common::ErrorCode;
use yr_proto::core_service::CreateResponse;
use yr_proto::runtime_service::NotifyRequest;

/// CreateResponse: if `message` carries failure text, `code` must not be `ErrNone` (0).
fn create_response_failure_contract(resp: &CreateResponse) -> bool {
    resp.message.is_empty() || resp.code != ErrorCode::ErrNone as i32
}

/// NotifyRequest: same contract for runtime notify failures.
fn notify_request_failure_contract(req: &NotifyRequest) -> bool {
    req.message.is_empty() || req.code != ErrorCode::ErrNone as i32
}

#[test]
fn failure_error_codes_are_distinct() {
    let failure_codes = [
        ErrorCode::ErrParamInvalid,
        ErrorCode::ErrResourceNotEnough,
        ErrorCode::ErrInstanceNotFound,
        ErrorCode::ErrInvokeRateLimited,
        ErrorCode::ErrCreateRateLimited,
        ErrorCode::ErrInnerCommunication,
        ErrorCode::ErrInnerSystemError,
        ErrorCode::ErrBusDisconnection,
        ErrorCode::ErrRuntimeManagerOperationError,
    ];
    let none = ErrorCode::ErrNone as i32;
    let mut seen = std::collections::HashSet::new();
    for c in &failure_codes {
        let v = *c as i32;
        assert_ne!(v, none, "{c:?} must differ from ErrNone");
        assert!(
            seen.insert(v),
            "duplicate numeric error code {v} in failure set"
        );
    }
}

#[test]
fn instance_state_transitions_include_exiting() {
    assert!(transition_allowed(
        InstanceState::Creating,
        InstanceState::Exiting
    ));
    assert!(transition_allowed(
        InstanceState::Running,
        InstanceState::Exiting
    ));
}

#[test]
fn create_response_failure_must_have_nonzero_code() {
    let violates = CreateResponse {
        code: ErrorCode::ErrNone as i32,
        message: "scheduling failed".into(),
        instance_id: String::new(),
    };
    assert!(
        !create_response_failure_contract(&violates),
        "non-empty message with ErrNone violates contract"
    );

    let ok = CreateResponse {
        code: ErrorCode::ErrInnerSystemError as i32,
        message: "scheduling failed".into(),
        instance_id: String::new(),
    };
    assert!(create_response_failure_contract(&ok));
}

#[test]
fn notify_request_failure_must_have_nonzero_code() {
    let violates = NotifyRequest {
        request_id: String::new(),
        code: ErrorCode::ErrNone as i32,
        message: "notify failed".into(),
        small_objects: vec![],
        stack_trace_infos: vec![],
        runtime_info: None,
    };
    assert!(
        !notify_request_failure_contract(&violates),
        "non-empty message with ErrNone violates contract"
    );

    let ok = NotifyRequest {
        request_id: "rid".into(),
        code: ErrorCode::ErrInnerSystemError as i32,
        message: "notify failed".into(),
        small_objects: vec![],
        stack_trace_infos: vec![],
        runtime_info: None,
    };
    assert!(notify_request_failure_contract(&ok));
}

#[test]
fn invoke_to_nonexistent_instance_error_code() {
    assert_eq!(ErrorCode::ErrInstanceNotFound as i32, 1003);
}

#[test]
fn resource_not_enough_error_code() {
    assert_eq!(ErrorCode::ErrResourceNotEnough as i32, 1002);
}

#[test]
fn rate_limited_error_codes() {
    assert_eq!(ErrorCode::ErrCreateRateLimited as i32, 1012);
    assert_eq!(ErrorCode::ErrInvokeRateLimited as i32, 1005);
}

#[test]
fn bus_disconnection_error_code() {
    assert_eq!(ErrorCode::ErrBusDisconnection as i32, 3006);
}

#[test]
fn state_transition_from_scheduling_to_exiting_allowed() {
    assert!(transition_allowed(
        InstanceState::Scheduling,
        InstanceState::Exiting
    ));
}
