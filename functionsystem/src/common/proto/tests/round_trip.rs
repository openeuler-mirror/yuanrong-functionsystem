//! Protobuf round-trip checks for selected public and internal messages.

use prost::Message;
use std::collections::HashMap;

use yr_proto::common::arg::ArgType;
use yr_proto::common::{Arg, ErrorCode};
use yr_proto::core_service::{CreateRequest, SchedulingOptions};
use yr_proto::internal::{RegisterRequest, ScheduleRequest, UpdateResourcesRequest};
use yr_proto::resources::ResourceUnit;

#[test]
fn create_request_round_trip() {
    let original = CreateRequest {
        function: "my-fn".into(),
        args: vec![Arg {
            r#type: ArgType::Value as i32,
            value: vec![1, 2, 3, 4],
            nested_refs: vec!["ref-a".into()],
        }],
        scheduling_ops: Some(SchedulingOptions {
            priority: 7,
            resources: HashMap::from([("cpu".into(), 2.5), ("memory".into(), 8.0)]),
            extension: HashMap::from([("k".into(), "v".into())]),
            affinity: HashMap::new(),
            schedule_affinity: None,
            range: None,
            schedule_timeout_ms: 5000,
            preempted_allowed: true,
            r_group_name: "rg1".into(),
        }),
        request_id: "req-1".into(),
        trace_id: "trace-xyz".into(),
        labels: vec!["env:prod".into()],
        designated_instance_id: "inst-fixed".into(),
        create_options: HashMap::from([("opt".into(), "1".into())]),
    };

    let bytes = original.encode_to_vec();
    let decoded = CreateRequest::decode(bytes.as_slice()).expect("decode CreateRequest");
    assert_eq!(decoded, original);
}

#[test]
fn internal_schedule_request_round_trip() {
    let original = ScheduleRequest {
        request_id: "sr-1".into(),
        tenant_id: "t1".into(),
        function_name: "f".into(),
        function_version: "v2".into(),
        required_resources: HashMap::from([("cpu".into(), 1.0), ("npu".into(), 0.5)]),
        priority: 10,
        labels: HashMap::from([("l".into(), "x".into())]),
        extension: HashMap::from([("e".into(), "y".into())]),
        designated_instance_id: "d".into(),
        args_payload: vec![9, 8, 7],
        updated_resource_json: r#"{"capacity":{"cpu":4},"used":{"cpu":1}}"#.into(),
        source_node_id: "node-src".into(),
        schedule_timeout_ms: 30_000,
        trace_id: "tr".into(),
    };

    let bytes = original.encode_to_vec();
    let decoded = ScheduleRequest::decode(bytes.as_slice()).expect("decode ScheduleRequest");
    assert_eq!(decoded, original);
}

#[test]
fn internal_register_request_round_trip_with_resource_unit() {
    let original = RegisterRequest {
        node_id: "node-a".into(),
        address: "http://127.0.0.1:8401".into(),
        resource_json: r#"{"capacity":{"cpu":8000}}"#.into(),
        agent_info_json: r#"{"role":"function_proxy"}"#.into(),
        resource_unit: Some(ResourceUnit {
            id: "node-a".into(),
            ..Default::default()
        }),
    };

    let bytes = original.encode_to_vec();
    let decoded = RegisterRequest::decode(bytes.as_slice()).expect("decode RegisterRequest");
    assert_eq!(decoded, original);
}

#[test]
fn internal_update_resources_request_round_trip_with_resource_unit() {
    let original = UpdateResourcesRequest {
        node_id: "node-a".into(),
        resource_json: r#"{"capacity":{"cpu":8000},"used":{"cpu":1000}}"#.into(),
        resource_unit: Some(ResourceUnit {
            id: "node-a".into(),
            ..Default::default()
        }),
    };

    let bytes = original.encode_to_vec();
    let decoded =
        UpdateResourcesRequest::decode(bytes.as_slice()).expect("decode UpdateResourcesRequest");
    assert_eq!(decoded, original);
}

#[test]
fn error_code_discriminant_values_match_proto() {
    assert_eq!(ErrorCode::ErrNone as i32, 0);
    assert_eq!(ErrorCode::ErrParamInvalid as i32, 1001);
    assert_eq!(ErrorCode::ErrResourceNotEnough as i32, 1002);
    assert_eq!(ErrorCode::ErrUserCodeLoad as i32, 2001);
    assert_eq!(ErrorCode::ErrRequestBetweenRuntimeBus as i32, 3001);
    assert_eq!(ErrorCode::ErrNpuFaultError as i32, 3016);
}
