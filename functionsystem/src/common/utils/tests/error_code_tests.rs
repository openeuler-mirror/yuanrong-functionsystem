//! Tests for `StatusCode`, `Status`, `YrError`, and alignment with `yr_proto::common::ErrorCode`.

use std::error::Error;
use std::io;

use tonic::Code;
use yr_common::error::YrError;
use yr_common::status::{Status, StatusCode};
use yr_proto::common::ErrorCode as ProtoErrorCode;

#[test]
fn status_code_core_values() {
    assert_eq!(StatusCode::Failed as i32, -1);
    assert_eq!(StatusCode::Success as i32, 0);
    assert_eq!(StatusCode::Reserved as i32, 1);
    assert_eq!(StatusCode::ParameterError as i32, 3);
}

#[test]
fn status_code_grpc_status_range() {
    assert_eq!(StatusCode::GrpcOk as i32, 104);
    assert_eq!(StatusCode::GrpcUnauthenticated as i32, 120);
}

#[test]
fn status_code_plugin_and_schedule() {
    assert_eq!(StatusCode::PluginRegisterError as i32, 200);
    assert_eq!(StatusCode::DiskScheduleFailed as i32, 210);
}

#[test]
fn status_code_instance_transaction_block() {
    assert_eq!(StatusCode::InstanceTransactionWrongVersion as i32, 300);
    assert_eq!(StatusCode::InstanceTransactionWrongParameter as i32, 304);
}

#[test]
fn status_code_err_block_1000s() {
    assert_eq!(StatusCode::ErrParamInvalid as i32, 1001);
    assert_eq!(StatusCode::ErrInstanceBusy as i32, 1021);
}

#[test]
fn status_code_user_and_bus_errors() {
    assert_eq!(StatusCode::ErrUserCodeLoad as i32, 2001);
    assert_eq!(StatusCode::ErrInnerSystemError as i32, 3003);
    assert_eq!(StatusCode::ErrNpuFaultError as i32, 3016);
}

#[test]
fn status_code_bp_block() {
    assert_eq!(StatusCode::BpDatasystemError as i32, 10_000);
    assert_eq!(StatusCode::DsScaleDown as i32, 10_011);
}

#[test]
fn status_code_fa_block() {
    assert_eq!(StatusCode::FaHttpRegisterHandlerNullError as i32, 20_000);
    assert_eq!(StatusCode::FaFunctionMetaEmptyMemory as i32, 20_009);
}

#[test]
fn status_code_runtime_manager_block() {
    assert_eq!(StatusCode::RuntimeManagerPortUnavailable as i32, 80_000);
    assert_eq!(StatusCode::RuntimeManagerCondaEnvNotExist as i32, 80_031);
}

#[test]
fn status_code_iam_metastore_tail() {
    assert_eq!(StatusCode::IamWaitInitializeComplete as i32, 90_000);
    assert_eq!(StatusCode::MetaStoreBackUpErr as i32, 91_000);
}

#[test]
fn status_code_try_from_roundtrip_samples() {
    for code in [
        StatusCode::Success,
        StatusCode::GrpcDeadlineExceeded,
        StatusCode::ErrScheduleCanceled,
        StatusCode::LsAgentEvicted,
        StatusCode::FuncAgentObsIllegalRanges,
    ] {
        let n: i32 = code.into();
        assert_eq!(StatusCode::try_from(n).unwrap(), code);
    }
}

#[test]
fn status_code_try_from_rejects_unknown() {
    assert!(StatusCode::try_from(999_999).is_err());
}

#[test]
fn status_code_duplicate_alias_matches_canonical() {
    assert_eq!(
        StatusCode::FuncAgentObsInitOptionsError as i32,
        StatusCode::FuncAgentInvalidAccessKeyError as i32
    );
}

#[test]
fn status_display_includes_numeric_code() {
    let s = Status::new(StatusCode::Failed, "boom");
    let t = format!("{s}");
    assert!(t.contains("Failed"));
    assert!(t.contains("-1"));
    assert!(t.contains("boom"));
}

#[test]
fn status_display_with_data_appendix() {
    let s = Status::new(StatusCode::ParameterError, "bad").with_data("extra");
    let t = format!("{s}");
    assert!(t.contains("data=extra"));
}

#[test]
fn status_ok_helpers() {
    let ok = Status::ok();
    assert!(ok.is_ok());
    assert!(!ok.is_error());
}

#[test]
fn status_new_is_error_when_not_success() {
    let s = Status::new(StatusCode::FileNotFound, "x");
    assert!(s.is_error());
    assert!(!s.is_ok());
}

#[test]
fn yr_error_config_display() {
    let e = YrError::Config("bad file".into());
    assert_eq!(e.to_string(), "configuration error: bad file");
}

#[test]
fn yr_error_etcd_display() {
    let e = YrError::Etcd("conn".into());
    assert_eq!(e.to_string(), "etcd error: conn");
}

#[test]
fn yr_error_serialization_display() {
    let e = YrError::Serialization("bincode".into());
    assert_eq!(e.to_string(), "serialization error: bincode");
}

#[test]
fn yr_error_actor_internal_display() {
    assert_eq!(
        YrError::Actor("a".into()).to_string(),
        "actor error: a"
    );
    assert_eq!(
        YrError::Internal("i".into()).to_string(),
        "internal error: i"
    );
}

#[test]
fn yr_error_grpc_from_tonic_chains_source() {
    let st = tonic::Status::new(Code::Unavailable, "down");
    let e: YrError = st.into();
    let src = e.source().expect("source");
    assert!(src.to_string().contains("down") || format!("{src}").contains("grpc"));
}

#[test]
fn yr_error_io_from_std_io_chains_source() {
    let io_err = io::Error::other("disk full");
    let e: YrError = io_err.into();
    assert!(e.source().is_some());
}

#[test]
fn proto_error_code_matches_status_code_hotspots() {
    assert_eq!(ProtoErrorCode::ErrNone as i32, StatusCode::Success as i32);
    assert_eq!(
        ProtoErrorCode::ErrParamInvalid as i32,
        StatusCode::ErrParamInvalid as i32
    );
    assert_eq!(
        ProtoErrorCode::ErrResourceNotEnough as i32,
        StatusCode::ErrResourceNotEnough as i32
    );
    assert_eq!(
        ProtoErrorCode::ErrInstanceExited as i32,
        StatusCode::ErrInstanceExited as i32
    );
}

#[test]
fn proto_error_code_middle_block() {
    assert_eq!(ProtoErrorCode::ErrGroupExitTogether as i32, 1011);
    assert_eq!(ProtoErrorCode::ErrInstanceSuspend as i32, 1020);
}

#[test]
fn proto_error_code_user_and_system() {
    assert_eq!(ProtoErrorCode::ErrUserFunctionException as i32, 2002);
    assert_eq!(ProtoErrorCode::ErrEtcdOperationError as i32, 3005);
    assert_eq!(ProtoErrorCode::ErrNpuFaultError as i32, 3016);
}

#[test]
fn status_code_try_from_boundary_minus_one_and_zero() {
    assert_eq!(StatusCode::try_from(-1).unwrap(), StatusCode::Failed);
    assert_eq!(StatusCode::try_from(0).unwrap(), StatusCode::Success);
}

#[test]
fn status_code_try_from_grpc_not_found() {
    assert_eq!(
        StatusCode::try_from(109).unwrap(),
        StatusCode::GrpcNotFound
    );
}

#[test]
fn status_code_try_from_ls_forward_timeout() {
    assert_eq!(
        StatusCode::try_from(50018).unwrap(),
        StatusCode::LsForwardDomainTimeout
    );
}
