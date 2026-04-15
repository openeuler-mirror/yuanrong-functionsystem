//! Construct protobuf messages (via `yr-proto`) and verify encode/decode and field wiring.

use std::collections::HashMap;

use prost::Message;
use yr_proto::common::arg::ArgType;
use yr_proto::common::{Arg, ErrorCode, SmallObject};
use yr_proto::core_service::{CallResult, CreateRequest, SchedulingOptions};
use yr_proto::runtime_rpc::{streaming_message, StreamingMessage};
use yr_proto::runtime_service::CallRequest;

fn mk_scheduling_ops() -> SchedulingOptions {
    SchedulingOptions {
        priority: 3,
        resources: HashMap::from([("cpu".into(), 1.0), ("memory".into(), 512.0)]),
        extension: HashMap::from([("k".into(), "v".into())]),
        affinity: HashMap::new(),
        schedule_affinity: None,
        range: None,
        schedule_timeout_ms: 10_000,
        preempted_allowed: false,
        r_group_name: "rg".into(),
    }
}

#[test]
fn create_request_encode_decode_empty() {
    let m = CreateRequest {
        function: "".into(),
        args: vec![],
        scheduling_ops: None,
        request_id: "".into(),
        trace_id: "".into(),
        labels: vec![],
        designated_instance_id: "".into(),
        create_options: HashMap::new(),
    };
    let bytes = m.encode_to_vec();
    let d = CreateRequest::decode(bytes.as_slice()).unwrap();
    assert_eq!(d, m);
}

#[test]
fn create_request_all_scalar_fields() {
    let m = CreateRequest {
        function: "f".into(),
        args: vec![],
        scheduling_ops: Some(mk_scheduling_ops()),
        request_id: "r".into(),
        trace_id: "t".into(),
        labels: vec!["a:b".into()],
        designated_instance_id: "d".into(),
        create_options: HashMap::from([("opt".into(), "1".into())]),
    };
    let d = CreateRequest::decode(m.encode_to_vec().as_slice()).unwrap();
    assert_eq!(d.function, "f");
    assert_eq!(d.request_id, "r");
    assert_eq!(d.trace_id, "t");
    assert_eq!(d.labels, vec!["a:b"]);
    assert_eq!(d.designated_instance_id, "d");
    assert_eq!(d.create_options.get("opt").map(String::as_str), Some("1"));
}

#[test]
fn create_request_with_arg_object_ref() {
    let m = CreateRequest {
        function: "g".into(),
        args: vec![Arg {
            r#type: ArgType::ObjectRef as i32,
            value: vec![],
            nested_refs: vec!["o1".into()],
        }],
        scheduling_ops: None,
        request_id: "r1".into(),
        trace_id: "t1".into(),
        labels: vec![],
        designated_instance_id: "".into(),
        create_options: HashMap::new(),
    };
    let d = CreateRequest::decode(m.encode_to_vec().as_slice()).unwrap();
    assert_eq!(d.args.len(), 1);
    assert_eq!(d.args[0].r#type, ArgType::ObjectRef as i32);
}

#[test]
fn call_request_minimal_roundtrip() {
    let m = CallRequest {
        function: "fn".into(),
        args: vec![],
        trace_id: "".into(),
        return_object_id: "".into(),
        is_create: false,
        sender_id: "".into(),
        request_id: "".into(),
        return_object_i_ds: vec![],
        create_options: HashMap::new(),
        span_id: "".into(),
    };
    let d = CallRequest::decode(m.encode_to_vec().as_slice()).unwrap();
    assert_eq!(d, m);
}

#[test]
fn call_request_all_fields_populated() {
    let m = CallRequest {
        function: "fn2".into(),
        args: vec![Arg {
            r#type: ArgType::Value as i32,
            value: vec![9],
            nested_refs: vec![],
        }],
        trace_id: "tr".into(),
        return_object_id: "ro".into(),
        is_create: true,
        sender_id: "snd".into(),
        request_id: "rq".into(),
        return_object_i_ds: vec!["a".into(), "b".into()],
        create_options: HashMap::from([("co".into(), "x".into())]),
        span_id: "sp".into(),
    };
    let d = CallRequest::decode(m.encode_to_vec().as_slice()).unwrap();
    assert_eq!(d.is_create, true);
    assert_eq!(d.sender_id, "snd");
    assert_eq!(d.return_object_i_ds, vec!["a", "b"]);
}

#[test]
fn call_result_with_payload_roundtrip() {
    let m = CallResult {
        code: ErrorCode::ErrNone as i32,
        message: "ok".into(),
        instance_id: "i".into(),
        request_id: "q".into(),
        small_objects: vec![SmallObject {
            id: "so1".into(),
            value: vec![1, 2],
        }],
        stack_trace_infos: vec![],
        runtime_info: None,
    };
    let d = CallResult::decode(m.encode_to_vec().as_slice()).unwrap();
    assert_eq!(d.code, ErrorCode::ErrNone as i32);
    assert_eq!(d.small_objects.len(), 1);
}

#[test]
fn streaming_message_create_req_variant() {
    let inner = CreateRequest {
        function: "h".into(),
        args: vec![],
        scheduling_ops: None,
        request_id: "r".into(),
        trace_id: "".into(),
        labels: vec![],
        designated_instance_id: "".into(),
        create_options: HashMap::new(),
    };
    let sm = StreamingMessage {
        message_id: "m1".into(),
        meta_data: HashMap::from([("tenant".into(), "t1".into())]),
        body: Some(streaming_message::Body::CreateReq(inner.clone())),
    };
    let d = StreamingMessage::decode(sm.encode_to_vec().as_slice()).unwrap();
    match d.body {
        Some(streaming_message::Body::CreateReq(cr)) => assert_eq!(cr.function, "h"),
        _ => panic!("wrong variant"),
    }
    assert_eq!(d.meta_data.get("tenant").map(String::as_str), Some("t1"));
}

#[test]
fn streaming_message_call_req_variant() {
    let cr = CallRequest {
        function: "c".into(),
        args: vec![],
        trace_id: "".into(),
        return_object_id: "".into(),
        is_create: false,
        sender_id: "".into(),
        request_id: "q".into(),
        return_object_i_ds: vec![],
        create_options: HashMap::new(),
        span_id: "".into(),
    };
    let sm = StreamingMessage {
        message_id: "m2".into(),
        meta_data: HashMap::new(),
        body: Some(streaming_message::Body::CallReq(cr)),
    };
    let d = StreamingMessage::decode(sm.encode_to_vec().as_slice()).unwrap();
    assert!(matches!(
        d.body,
        Some(streaming_message::Body::CallReq(_))
    ));
}

#[test]
fn streaming_message_call_result_variant() {
    let cr = CallResult {
        code: ErrorCode::ErrInstanceNotFound as i32,
        message: "missing".into(),
        instance_id: "".into(),
        request_id: "".into(),
        small_objects: vec![],
        stack_trace_infos: vec![],
        runtime_info: None,
    };
    let sm = StreamingMessage {
        message_id: "m3".into(),
        meta_data: HashMap::new(),
        body: Some(streaming_message::Body::CallResultReq(cr)),
    };
    let d = StreamingMessage::decode(sm.encode_to_vec().as_slice()).unwrap();
    if let Some(streaming_message::Body::CallResultReq(x)) = d.body {
        assert_eq!(x.code, ErrorCode::ErrInstanceNotFound as i32);
    } else {
        panic!("expected CallResultReq");
    }
}

#[test]
fn scheduling_options_resources_map_roundtrip() {
    let s = mk_scheduling_ops();
    let bytes = s.encode_to_vec();
    let d = SchedulingOptions::decode(bytes.as_slice()).unwrap();
    assert!((d.resources["cpu"] - 1.0).abs() < 1e-9);
    assert!((d.resources["memory"] - 512.0).abs() < 1e-9);
}

#[test]
fn scheduling_options_priority_negative_allowed_in_wire_format() {
    let s = SchedulingOptions {
        priority: -5,
        resources: HashMap::new(),
        extension: HashMap::new(),
        affinity: HashMap::new(),
        schedule_affinity: None,
        range: None,
        schedule_timeout_ms: 0,
        preempted_allowed: true,
        r_group_name: "".into(),
    };
    let d = SchedulingOptions::decode(s.encode_to_vec().as_slice()).unwrap();
    assert_eq!(d.priority, -5);
}

#[test]
fn create_options_empty_map_roundtrip() {
    let m = CreateRequest {
        function: "x".into(),
        args: vec![],
        scheduling_ops: None,
        request_id: "".into(),
        trace_id: "".into(),
        labels: vec![],
        designated_instance_id: "".into(),
        create_options: HashMap::new(),
    };
    let d = CreateRequest::decode(m.encode_to_vec().as_slice()).unwrap();
    assert!(d.create_options.is_empty());
}

#[test]
fn metadata_multiple_entries_preserved() {
    let sm = StreamingMessage {
        message_id: "mid".into(),
        meta_data: HashMap::from([
            ("a".into(), "1".into()),
            ("b".into(), "2".into()),
        ]),
        body: None,
    };
    let d = StreamingMessage::decode(sm.encode_to_vec().as_slice()).unwrap();
    assert_eq!(d.meta_data.len(), 2);
}

#[test]
fn call_request_boolean_false_explicit() {
    let m = CallRequest {
        function: "f".into(),
        args: vec![],
        trace_id: "".into(),
        return_object_id: "".into(),
        is_create: false,
        sender_id: "".into(),
        request_id: "".into(),
        return_object_i_ds: vec![],
        create_options: HashMap::new(),
        span_id: "".into(),
    };
    let d = CallRequest::decode(m.encode_to_vec().as_slice()).unwrap();
    assert!(!d.is_create);
}

#[test]
fn arg_value_empty_bytes() {
    let a = Arg {
        r#type: ArgType::Value as i32,
        value: vec![],
        nested_refs: vec![],
    };
    let m = CreateRequest {
        function: "f".into(),
        args: vec![a],
        scheduling_ops: None,
        request_id: "".into(),
        trace_id: "".into(),
        labels: vec![],
        designated_instance_id: "".into(),
        create_options: HashMap::new(),
    };
    let d = CreateRequest::decode(m.encode_to_vec().as_slice()).unwrap();
    assert!(d.args[0].value.is_empty());
}

#[test]
fn labels_duplicates_preserved() {
    let m = CreateRequest {
        function: "f".into(),
        args: vec![],
        scheduling_ops: None,
        request_id: "".into(),
        trace_id: "".into(),
        labels: vec!["x".into(), "x".into()],
        designated_instance_id: "".into(),
        create_options: HashMap::new(),
    };
    let d = CreateRequest::decode(m.encode_to_vec().as_slice()).unwrap();
    assert_eq!(d.labels.len(), 2);
}

#[test]
fn call_result_error_code_nonzero() {
    let m = CallResult {
        code: ErrorCode::ErrInstanceBusy as i32,
        message: "busy".into(),
        instance_id: "".into(),
        request_id: "".into(),
        small_objects: vec![],
        stack_trace_infos: vec![],
        runtime_info: None,
    };
    let d = CallResult::decode(m.encode_to_vec().as_slice()).unwrap();
    assert_ne!(d.code, 0);
}

#[test]
fn create_response_roundtrip() {
    use yr_proto::core_service::CreateResponse;
    let m = CreateResponse {
        code: ErrorCode::ErrNone as i32,
        message: "".into(),
        instance_id: "inst-9".into(),
    };
    let d = CreateResponse::decode(m.encode_to_vec().as_slice()).unwrap();
    assert_eq!(d.instance_id, "inst-9");
}

#[test]
fn invoke_request_roundtrip() {
    use yr_proto::core_service::InvokeRequest;
    let m = InvokeRequest {
        function: "f".into(),
        args: vec![],
        instance_id: "i".into(),
        request_id: "r".into(),
        trace_id: "t".into(),
        return_object_i_ds: vec!["o".into()],
        span_id: "s".into(),
        invoke_options: None,
    };
    let d = InvokeRequest::decode(m.encode_to_vec().as_slice()).unwrap();
    assert_eq!(d.instance_id, "i");
}

#[test]
fn kill_request_roundtrip() {
    use yr_proto::core_service::KillRequest;
    let m = KillRequest {
        instance_id: "k1".into(),
        signal: 15,
        payload: vec![1, 2],
        request_id: "r".into(),
    };
    let d = KillRequest::decode(m.encode_to_vec().as_slice()).unwrap();
    assert_eq!(d.signal, 15);
}

#[test]
fn exit_request_roundtrip() {
    use yr_proto::core_service::ExitRequest;
    let m = ExitRequest {
        code: ErrorCode::ErrInstanceExited as i32,
        message: "gone".into(),
    };
    let d = ExitRequest::decode(m.encode_to_vec().as_slice()).unwrap();
    assert_eq!(d.message, "gone");
}

#[test]
fn streaming_message_invoke_req_variant() {
    use yr_proto::core_service::InvokeRequest;
    let ir = InvokeRequest {
        function: "ifn".into(),
        args: vec![],
        instance_id: "ii".into(),
        request_id: "ir".into(),
        trace_id: "".into(),
        return_object_i_ds: vec![],
        span_id: "".into(),
        invoke_options: None,
    };
    let sm = StreamingMessage {
        message_id: "inv".into(),
        meta_data: HashMap::new(),
        body: Some(streaming_message::Body::InvokeReq(ir)),
    };
    let d = StreamingMessage::decode(sm.encode_to_vec().as_slice()).unwrap();
    assert!(matches!(d.body, Some(streaming_message::Body::InvokeReq(_))));
}

#[test]
fn streaming_message_kill_req_variant() {
    use yr_proto::core_service::KillRequest;
    let kr = KillRequest {
        instance_id: "ki".into(),
        signal: 9,
        payload: vec![],
        request_id: "kr".into(),
    };
    let sm = StreamingMessage {
        message_id: "kill".into(),
        meta_data: HashMap::new(),
        body: Some(streaming_message::Body::KillReq(kr)),
    };
    let d = StreamingMessage::decode(sm.encode_to_vec().as_slice()).unwrap();
    assert!(matches!(d.body, Some(streaming_message::Body::KillReq(_))));
}

#[test]
fn call_response_roundtrip() {
    use yr_proto::runtime_service::CallResponse;
    let m = CallResponse {
        code: ErrorCode::ErrParamInvalid as i32,
        message: "bad".into(),
    };
    let d = CallResponse::decode(m.encode_to_vec().as_slice()).unwrap();
    assert_eq!(d.code, ErrorCode::ErrParamInvalid as i32);
}

#[test]
fn call_result_ack_roundtrip() {
    use yr_proto::core_service::CallResultAck;
    let m = CallResultAck {
        code: ErrorCode::ErrNone as i32,
        message: "ack".into(),
    };
    let d = CallResultAck::decode(m.encode_to_vec().as_slice()).unwrap();
    assert_eq!(d.message, "ack");
}

#[test]
fn state_save_request_bytes_roundtrip() {
    use yr_proto::core_service::StateSaveRequest;
    let m = StateSaveRequest {
        state: vec![0u8, 1, 2, 255],
        request_id: "save-req".into(),
    };
    let d = StateSaveRequest::decode(m.encode_to_vec().as_slice()).unwrap();
    assert_eq!(d.state, vec![0, 1, 2, 255]);
    assert_eq!(d.request_id, "save-req");
}

#[test]
fn notify_request_roundtrip() {
    use yr_proto::runtime_service::NotifyRequest;
    let m = NotifyRequest {
        request_id: "n1".into(),
        code: ErrorCode::ErrNone as i32,
        message: "done".into(),
        small_objects: vec![],
        stack_trace_infos: vec![],
        runtime_info: None,
    };
    let d = NotifyRequest::decode(m.encode_to_vec().as_slice()).unwrap();
    assert_eq!(d.request_id, "n1");
}

#[test]
fn heartbeat_request_response_roundtrip() {
    use yr_proto::common::HealthCheckCode;
    use yr_proto::runtime_service::{HeartbeatRequest, HeartbeatResponse};
    let req = HeartbeatRequest {};
    let rsp = HeartbeatResponse {
        code: HealthCheckCode::Healthy as i32,
    };
    let _ = HeartbeatRequest::decode(req.encode_to_vec().as_slice()).unwrap();
    assert_eq!(
        HeartbeatResponse::decode(rsp.encode_to_vec().as_slice())
            .unwrap()
            .code,
        HealthCheckCode::Healthy as i32
    );
}
