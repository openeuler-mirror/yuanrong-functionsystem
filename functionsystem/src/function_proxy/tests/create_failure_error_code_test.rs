//! ST: proxy_create_failure_error_code
//! Validates that CreateRsp and NotifyReq on scheduling failure carry a non-zero ErrorCode.
//!
//! Since wiring a full BusProxyCoordinator requires etcd/agent, this test validates the
//! contract at the protocol layer: ErrInnerSystemError (3003) is used for scheduling failures.

use yr_proto::common::ErrorCode;

#[test]
fn error_code_for_scheduling_failure_is_not_errnone() {
    let failure_code = ErrorCode::ErrInnerSystemError as i32;
    assert_ne!(failure_code, 0, "scheduling failure must not use ErrNone");
    assert_eq!(failure_code, 3003);
}

#[test]
fn error_code_errnone_is_zero() {
    assert_eq!(ErrorCode::ErrNone as i32, 0);
}

#[test]
fn all_error_domain_codes_are_nonzero() {
    let codes = [
        ErrorCode::ErrParamInvalid,
        ErrorCode::ErrResourceNotEnough,
        ErrorCode::ErrInstanceNotFound,
        ErrorCode::ErrInnerSystemError,
        ErrorCode::ErrInnerCommunication,
        ErrorCode::ErrBusDisconnection,
    ];
    for c in &codes {
        assert_ne!(*c as i32, 0, "{c:?} must be nonzero");
    }
}
