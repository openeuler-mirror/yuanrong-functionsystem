//! Tests for GroupCreate and CreateResourceGroup streaming message handling.

use yr_proto::common::ErrorCode;
use yr_proto::core_service::{
    CreateRequests, CreateRequest, CreateResourceGroupRequest,
    CreateResourceGroupResponse, CreateResponses, GroupOptions,
};

#[test]
fn create_requests_carries_batch_metadata() {
    let batch = CreateRequests {
        requests: vec![
            CreateRequest {
                function: "fn-a".into(),
                request_id: "r1".into(),
                ..Default::default()
            },
            CreateRequest {
                function: "fn-b".into(),
                request_id: "r2".into(),
                ..Default::default()
            },
        ],
        tenant_id: "tenant-1".into(),
        request_id: "batch-001".into(),
        trace_id: "trace-001".into(),
        group_opt: Some(GroupOptions {
            timeout: 30,
            group_name: "my-group".into(),
            same_running_lifecycle: true,
            ..Default::default()
        }),
    };
    assert_eq!(batch.requests.len(), 2);
    assert_eq!(batch.tenant_id, "tenant-1");
    assert!(batch.group_opt.is_some());
}

#[test]
fn create_responses_includes_group_id_and_instance_ids() {
    let rsp = CreateResponses {
        code: ErrorCode::ErrNone as i32,
        message: String::new(),
        instance_i_ds: vec!["i1".into(), "i2".into(), "i3".into()],
        group_id: "grp-abc".into(),
    };
    assert_eq!(rsp.code, 0);
    assert_eq!(rsp.instance_i_ds.len(), 3);
    assert!(!rsp.group_id.is_empty());
}

#[test]
fn create_responses_failure_has_error_code() {
    let rsp = CreateResponses {
        code: ErrorCode::ErrInnerSystemError as i32,
        message: "partial failure".into(),
        instance_i_ds: vec!["i1".into()],
        group_id: "grp-abc".into(),
    };
    assert_eq!(rsp.code, 3003);
    assert!(!rsp.message.is_empty());
}

#[test]
fn create_resource_group_request_and_response() {
    let req = CreateResourceGroupRequest {
        r_group_spec: None,
        request_id: "rg-req-1".into(),
        trace_id: "trace-1".into(),
    };
    assert_eq!(req.request_id, "rg-req-1");

    let rsp = CreateResourceGroupResponse {
        code: ErrorCode::ErrNone as i32,
        message: String::new(),
        request_id: "rg-req-1".into(),
    };
    assert_eq!(rsp.code, 0);
    assert_eq!(rsp.request_id, "rg-req-1");
}

#[test]
fn group_options_supports_rgroup_name() {
    let opts = GroupOptions {
        timeout: 60,
        group_name: "batch-job".into(),
        same_running_lifecycle: false,
        r_group_name: "pool-gpu".into(),
        group_policy: 0,
    };
    assert_eq!(opts.r_group_name, "pool-gpu");
    assert_eq!(opts.timeout, 60);
}
