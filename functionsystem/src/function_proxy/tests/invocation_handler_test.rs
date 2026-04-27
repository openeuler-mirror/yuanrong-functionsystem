//! Unit-style coverage for [`yr_proxy::busproxy::invocation_handler::InvocationHandler`].

mod common;

use common::new_bus;
use prost::Message;
use tokio::sync::mpsc;
use yr_proto::common::{Arg, BindStrategy, ErrorCode};
use yr_proto::core_service::{
    BindOptions, CreateRequest, CreateRequests, CreateResourceGroupRequest, EventRequest,
    ExitRequest, GroupOptions, InvokeRequest, KillRequest,
};
use yr_proto::resources::MetaData;
use yr_proto::runtime_rpc::{streaming_message, StreamingMessage};
use yr_proto::runtime_service::{HeartbeatRequest, NotifyRequest, SignalResponse};
use yr_proxy::busproxy::invocation_handler::{InboundAction, InvocationHandler};
use yr_proxy::state_machine::{InstanceMetadata, InstanceState};

#[derive(Clone, PartialEq, ::prost::Message)]
struct TestInvocationMeta {
    #[prost(string, tag = "1")]
    pub invoker_runtime_id: String,
    #[prost(int64, tag = "2")]
    pub invocation_sequence_no: i64,
    #[prost(int64, tag = "3")]
    pub min_unfinished_sequence_no: i64,
}

#[derive(Clone, PartialEq, ::prost::Message)]
struct TestMetaData {
    #[prost(int32, tag = "1")]
    pub invoke_type: i32,
    #[prost(message, optional, tag = "4")]
    pub invocation_meta: Option<TestInvocationMeta>,
}

fn arg_with_sequence(seq: i64) -> Arg {
    let meta = TestMetaData {
        invoke_type: 0,
        invocation_meta: Some(TestInvocationMeta {
            invoker_runtime_id: String::new(),
            invocation_sequence_no: seq,
            min_unfinished_sequence_no: seq,
        }),
    };
    let mut value = Vec::new();
    meta.encode(&mut value).expect("encode invocation meta");
    Arg {
        value,
        ..Default::default()
    }
}

#[test]
fn invoke_to_call_copies_core_fields() {
    let inv = InvokeRequest {
        function: "foo".into(),
        args: vec![],
        trace_id: "t1".into(),
        request_id: "r1".into(),
        return_object_i_ds: vec!["o1".into()],
        span_id: "s1".into(),
        ..Default::default()
    };
    let msg = InvocationHandler::invoke_to_call(&inv, "mid", "driver-1");
    assert_eq!(msg.message_id, "mid");
    let body = msg.body.as_ref().expect("body");
    let streaming_message::Body::CallReq(call) = body else {
        panic!("expected CallReq");
    };
    assert_eq!(call.function, "foo");
    assert_eq!(call.trace_id, "t1");
    assert_eq!(call.request_id, "r1");
    assert_eq!(call.return_object_id, "mid");
    assert_eq!(call.sender_id, "driver-1");
    assert_eq!(call.return_object_i_ds, vec!["o1"]);
    assert_eq!(call.span_id, "s1");
}

#[tokio::test]
async fn create_req_scheduling_failure_returns_create_rsp_and_notify() {
    let bus = new_bus("node-a", 29001);
    let create = CreateRequest {
        function: "f".into(),
        request_id: "creq-1".into(),
        ..Default::default()
    };
    let msg = StreamingMessage {
        message_id: "m1".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::CreateReq(create)),
    };
    let act = InvocationHandler::handle_runtime_inbound("driver-1", msg, &bus).await;
    let InboundAction::Reply(outs) = act else {
        panic!("expected Reply");
    };
    assert_eq!(outs.len(), 2);
    let b0 = outs[0].body.as_ref().unwrap();
    let streaming_message::Body::CreateRsp(rsp) = b0 else {
        panic!("CreateRsp");
    };
    assert_eq!(rsp.code, ErrorCode::ErrInnerSystemError as i32);
    assert!(!rsp.instance_id.is_empty());
    let b1 = outs[1].body.as_ref().unwrap();
    let streaming_message::Body::NotifyReq(n) = b1 else {
        panic!("NotifyReq");
    };
    assert_eq!(n.request_id, "creq-1");
}

#[tokio::test]
async fn create_req_respects_designated_instance_id_on_failure() {
    let bus = new_bus("node-a", 29002);
    let create = CreateRequest {
        function: "f".into(),
        request_id: "r".into(),
        designated_instance_id: "deadbeef".into(),
        ..Default::default()
    };
    let msg = StreamingMessage {
        message_id: "mid".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::CreateReq(create)),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound("drv", msg, &bus).await
    else {
        panic!("Reply");
    };
    let streaming_message::Body::CreateRsp(rsp) = outs[0].body.as_ref().unwrap() else {
        panic!("CreateRsp");
    };
    assert_eq!(rsp.instance_id, "deadbeef");
}

#[tokio::test]
async fn duplicate_create_during_pending_init_only_returns_create_rsp() {
    let bus = new_bus("node-a", 29002);
    let create = CreateRequest {
        function: "f".into(),
        request_id: "inst-1".into(),
        ..Default::default()
    };
    bus.register_pending_instance("inst-1", "driver-1", &create);
    let (tx, _rx) = tokio::sync::mpsc::channel(4);
    bus.attach_runtime_stream("inst-1", tx);
    let bus2 = bus.clone();
    let connect = tokio::spawn(async move {
        bus2.on_runtime_connected("inst-1", "rt-inst-1").await;
    });
    tokio::time::sleep(std::time::Duration::from_millis(5)).await;
    assert!(bus.is_pending_init("inst-1"));

    let msg = StreamingMessage {
        message_id: "dup-mid".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::CreateReq(create.clone())),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound("driver-1", msg, &bus).await
    else {
        panic!("Reply");
    };
    assert_eq!(
        outs.len(),
        1,
        "duplicate create must not emit NotifyReq before init"
    );
    let streaming_message::Body::CreateRsp(rsp) = outs[0].body.as_ref().unwrap() else {
        panic!("CreateRsp");
    };
    assert_eq!(rsp.code, ErrorCode::ErrNone as i32);
    connect.abort();
}

#[tokio::test]
async fn duplicate_create_with_stream_before_init_result_does_not_notify() {
    let bus = new_bus("node-a", 29003);
    let create = CreateRequest {
        function: "f".into(),
        request_id: "inst-streaming".into(),
        ..Default::default()
    };
    let (tx, _rx) = tokio::sync::mpsc::channel(4);
    bus.attach_runtime_stream("inst-streaming", tx);
    assert!(!bus.is_init_completed("inst-streaming"));

    let msg = StreamingMessage {
        message_id: "dup-mid".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::CreateReq(create)),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound("driver-1", msg, &bus).await
    else {
        panic!("Reply");
    };
    assert_eq!(
        outs.len(),
        1,
        "duplicate create must wait for init CallResult before NotifyReq"
    );
    let streaming_message::Body::CreateRsp(rsp) = outs[0].body.as_ref().unwrap() else {
        panic!("CreateRsp");
    };
    assert_eq!(rsp.code, ErrorCode::ErrNone as i32);
}

#[tokio::test]
async fn group_create_empty_requests_succeeds() {
    let bus = new_bus("node-a", 29004);
    let batch = CreateRequests {
        requests: vec![],
        request_id: "batch".into(),
        ..Default::default()
    };
    let msg = StreamingMessage {
        message_id: "g0".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::CreateReqs(batch)),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound("driver", msg, &bus).await
    else {
        panic!("Reply");
    };
    let streaming_message::Body::CreateRsps(rsps) = outs[0].body.as_ref().unwrap() else {
        panic!("CreateRsps");
    };
    assert_eq!(rsps.code, ErrorCode::ErrNone as i32);
    assert!(rsps.instance_i_ds.is_empty());
    assert!(!rsps.group_id.is_empty());
}

#[tokio::test]
async fn group_create_first_schedule_failure_marks_batch_failed() {
    let bus = new_bus("node-a", 29004);
    let batch = CreateRequests {
        requests: vec![
            CreateRequest {
                function: "a".into(),
                request_id: "a1".into(),
                ..Default::default()
            },
            CreateRequest {
                function: "b".into(),
                request_id: "b1".into(),
                ..Default::default()
            },
        ],
        request_id: "batch2".into(),
        ..Default::default()
    };
    let msg = StreamingMessage {
        message_id: "g1".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::CreateReqs(batch)),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound("driver", msg, &bus).await
    else {
        panic!("Reply");
    };
    let streaming_message::Body::CreateRsps(rsps) = outs[0].body.as_ref().unwrap() else {
        panic!("CreateRsps");
    };
    assert_eq!(rsps.code, ErrorCode::ErrInnerSystemError as i32);
    assert!(!rsps.message.is_empty());
}

#[test]
fn group_bind_options_are_mapped_to_scheduling_extension() {
    let mut create = CreateRequest {
        function: "f".into(),
        request_id: "r".into(),
        ..Default::default()
    };
    let group = GroupOptions {
        bind: Some(BindOptions {
            resource: "NUMA".into(),
            policy: BindStrategy::BindPack as i32,
        }),
        ..Default::default()
    };

    InvocationHandler::apply_group_bind_options(&mut create, Some(&group));

    let ext = &create
        .scheduling_ops
        .as_ref()
        .expect("scheduling ops created")
        .extension;
    assert_eq!(ext.get("bind_resource").map(String::as_str), Some("NUMA"));
    assert_eq!(ext.get("bind_strategy").map(String::as_str), Some("BIND_Pack"));
}

#[test]
fn group_bind_unknown_policy_defaults_to_cpp_bind_none_string() {
    let mut create = CreateRequest::default();
    let group = GroupOptions {
        bind: Some(BindOptions {
            resource: "NUMA".into(),
            policy: 999,
        }),
        ..Default::default()
    };

    InvocationHandler::apply_group_bind_options(&mut create, Some(&group));

    let ext = &create.scheduling_ops.as_ref().unwrap().extension;
    assert_eq!(ext.get("bind_strategy").map(String::as_str), Some("BIND_None"));
}

#[tokio::test]
async fn r_group_req_returns_stub_ok() {
    let bus = new_bus("node-a", 29005);
    let msg = StreamingMessage {
        message_id: "rg".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::RGroupReq(
            CreateResourceGroupRequest {
                request_id: "rr".into(),
                trace_id: "tt".into(),
                ..Default::default()
            },
        )),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound("d", msg, &bus).await
    else {
        panic!("Reply");
    };
    assert!(matches!(
        outs[0].body,
        Some(streaming_message::Body::RGroupRsp(_))
    ));
}

#[tokio::test]
async fn invoke_req_without_target_stream_replies_call() {
    let bus = new_bus("node-a", 29006);
    let msg = StreamingMessage {
        message_id: "inv".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::InvokeReq(InvokeRequest {
            function: "g".into(),
            request_id: "ir1".into(),
            ..Default::default()
        })),
    };
    let act = InvocationHandler::handle_runtime_inbound("caller", msg, &bus).await;
    let InboundAction::Reply(outs) = act else {
        panic!("Reply");
    };
    assert!(matches!(
        outs[0].body,
        Some(streaming_message::Body::CallReq(_))
    ));
}

#[tokio::test]
async fn invoke_req_routes_to_named_runtime_stream() {
    let bus = new_bus("node-a", 29007);
    let (tx, mut rx) = tokio::sync::mpsc::channel(4);
    let target = "target-inst";
    bus.attach_runtime_stream(target, tx);
    bus.on_runtime_recover_response(
        target,
        &yr_proto::runtime_service::RecoverResponse {
            code: ErrorCode::ErrNone as i32,
            ..Default::default()
        },
    );
    let msg = StreamingMessage {
        message_id: "inv2".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::InvokeReq(InvokeRequest {
            function: "h".into(),
            request_id: "ir2".into(),
            instance_id: target.into(),
            ..Default::default()
        })),
    };
    let act = InvocationHandler::handle_runtime_inbound("driver-x", msg, &bus).await;
    assert!(matches!(act, InboundAction::None));
    let got = rx.recv().await.expect("msg").expect("ok");
    assert!(matches!(
        got.body,
        Some(streaming_message::Body::CallReq(_))
    ));
}

#[tokio::test]
async fn ordered_invokes_from_distinct_callers_start_at_sequence_one() {
    let bus = new_bus("node-a", 29021);
    let target = "ordered-target";
    let create = CreateRequest {
        function: "f".into(),
        request_id: "create-ordered-target".into(),
        create_options: [("Concurrency".to_string(), "1".to_string())]
            .into_iter()
            .collect(),
        ..Default::default()
    };
    bus.register_pending_instance(target, "driver-1", &create);
    let (tx, mut rx) = tokio::sync::mpsc::channel(8);
    bus.attach_runtime_stream(target, tx);
    bus.on_runtime_connected(target, "runtime-ordered").await;
    rx.recv().await.expect("init call").expect("init call ok");

    let init_result = StreamingMessage {
        message_id: "init-result".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::CallResultReq(
            yr_proto::core_service::CallResult {
                request_id: "create-ordered-target".into(),
                code: ErrorCode::ErrNone as i32,
                message: "ok".into(),
                ..Default::default()
            },
        )),
    };
    InvocationHandler::handle_runtime_inbound(target, init_result, &bus).await;
    rx.recv()
        .await
        .expect("init result ack")
        .expect("init result ack ok");
    assert!(bus.is_init_completed(target));

    for (caller, request_id) in [("driver-1", "driver-seq-1"), ("actor-1", "actor-seq-1")] {
        let msg = StreamingMessage {
            message_id: format!("mid-{request_id}"),
            meta_data: Default::default(),
            body: Some(streaming_message::Body::InvokeReq(InvokeRequest {
                function: "f".into(),
                request_id: request_id.into(),
                instance_id: target.into(),
                args: vec![arg_with_sequence(1)],
                ..Default::default()
            })),
        };
        assert!(matches!(
            InvocationHandler::handle_runtime_inbound(caller, msg, &bus).await,
            InboundAction::Reply(_)
        ));
        let got = tokio::time::timeout(std::time::Duration::from_millis(100), rx.recv())
            .await
            .expect("ordered call should dispatch")
            .expect("ordered call")
            .expect("ordered call ok");
        let Some(streaming_message::Body::CallReq(call)) = got.body else {
            panic!("expected CallReq");
        };
        assert_eq!(call.request_id, request_id);

        let result = StreamingMessage {
            message_id: format!("result-{request_id}"),
            meta_data: Default::default(),
            body: Some(streaming_message::Body::CallResultReq(
                yr_proto::core_service::CallResult {
                    request_id: request_id.into(),
                    code: ErrorCode::ErrNone as i32,
                    message: "ok".into(),
                    ..Default::default()
                },
            )),
        };
        InvocationHandler::handle_runtime_inbound(target, result, &bus).await;
        if request_id == "driver-seq-1" {
            let driver_scope = bus
                .sequence_scope_for_invocation("driver-1", target)
                .expect("driver sequence scope");
            assert_eq!(bus.expected_sequence_for_scope(&driver_scope), 2);
            tokio::time::sleep(std::time::Duration::from_millis(20)).await;
        }
    }
}

#[tokio::test]
async fn call_result_from_runtime_returns_none_action() {
    let bus = new_bus("node-a", 29008);
    let msg = StreamingMessage {
        message_id: "cr".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::CallResultReq(
            yr_proto::core_service::CallResult {
                request_id: "x".into(),
                code: 0,
                message: "ok".into(),
                ..Default::default()
            },
        )),
    };
    let act = InvocationHandler::handle_runtime_inbound("run-1", msg, &bus).await;
    assert!(matches!(act, InboundAction::None));
}

#[tokio::test]
async fn notify_req_returns_ack() {
    let bus = new_bus("node-a", 29009);
    let msg = StreamingMessage {
        message_id: "n1".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::NotifyReq(NotifyRequest {
            request_id: "nr".into(),
            ..Default::default()
        })),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound("inst", msg, &bus).await
    else {
        panic!("Reply");
    };
    assert!(matches!(
        outs[0].body,
        Some(streaming_message::Body::NotifyRsp(_))
    ));
}

#[tokio::test]
async fn kill_req_local_empty_target_uses_caller() {
    let bus = new_bus("node-a", 29010);
    let caller = "caller-iid";
    let meta = InstanceMetadata {
        id: caller.into(),
        function_name: "f".into(),
        tenant: String::new(),
        node_id: "node-a".into(),
        runtime_id: "".into(),
        runtime_port: 0,
        state: InstanceState::Running,
        created_at_ms: InstanceMetadata::now_ms(),
        updated_at_ms: InstanceMetadata::now_ms(),
        group_id: None,
        trace_id: String::new(),
        resources: Default::default(),
        etcd_kv_version: None,
        etcd_mod_revision: None,
    };
    bus.instance_ctrl_ref().insert_metadata(meta);
    let msg = StreamingMessage {
        message_id: "k1".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::KillReq(KillRequest {
            instance_id: String::new(),
            signal: 1,
            ..Default::default()
        })),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound(caller, msg, &bus).await
    else {
        panic!("Reply");
    };
    assert!(matches!(
        outs[0].body,
        Some(streaming_message::Body::KillRsp(_))
    ));
}

#[tokio::test]
async fn kill_req_local_explicit_target() {
    let bus = new_bus("node-a", 29011);
    let tid = "tgt-kill";
    let meta = InstanceMetadata {
        id: tid.into(),
        function_name: "f".into(),
        tenant: String::new(),
        node_id: "node-a".into(),
        runtime_id: "r".into(),
        runtime_port: 0,
        state: InstanceState::Running,
        created_at_ms: InstanceMetadata::now_ms(),
        updated_at_ms: InstanceMetadata::now_ms(),
        group_id: None,
        trace_id: String::new(),
        resources: Default::default(),
        etcd_kv_version: None,
        etcd_mod_revision: None,
    };
    bus.instance_ctrl_ref().insert_metadata(meta);
    let msg = StreamingMessage {
        message_id: "k2".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::KillReq(KillRequest {
            instance_id: tid.into(),
            signal: 2,
            ..Default::default()
        })),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound("other", msg, &bus).await
    else {
        panic!("Reply");
    };
    let streaming_message::Body::KillRsp(k) = outs[0].body.as_ref().unwrap() else {
        panic!("KillRsp");
    };
    assert_eq!(k.code, ErrorCode::ErrNone as i32);
}

#[tokio::test]
async fn user_signal_forwards_signal_req_and_returns_signal_payload() {
    let bus = new_bus("node-a", 29012);
    let caller = "driver-signal";
    let target = "target-signal";
    let (caller_tx, mut caller_rx) = mpsc::channel(4);
    let (target_tx, mut target_rx) = mpsc::channel(4);
    bus.attach_runtime_stream(caller, caller_tx);
    bus.attach_runtime_stream(target, target_tx);

    let msg = StreamingMessage {
        message_id: "kill-msg".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::KillReq(KillRequest {
            instance_id: target.into(),
            signal: 99,
            payload: b"signal-in".to_vec(),
            ..Default::default()
        })),
    };

    let act = InvocationHandler::handle_runtime_inbound(caller, msg, &bus).await;
    assert!(matches!(act, InboundAction::None));

    let forwarded = target_rx.recv().await.expect("target signal request").unwrap();
    let signal_msg_id = forwarded.message_id.clone();
    let streaming_message::Body::SignalReq(req) = forwarded.body.expect("SignalReq") else {
        panic!("user signals must be forwarded as runtime SignalReq, not KillReq");
    };
    assert_eq!(req.signal, 99);
    assert_eq!(req.payload, b"signal-in");

    let rsp = StreamingMessage {
        message_id: signal_msg_id,
        meta_data: Default::default(),
        body: Some(streaming_message::Body::SignalRsp(SignalResponse {
            code: ErrorCode::ErrNone as i32,
            message: "ok".into(),
            payload: b"signal-out".to_vec(),
        })),
    };
    let act = InvocationHandler::handle_runtime_inbound(target, rsp, &bus).await;
    assert!(matches!(act, InboundAction::None));

    let reply = caller_rx.recv().await.expect("caller kill response").unwrap();
    assert_eq!(reply.message_id, "kill-msg");
    let streaming_message::Body::KillRsp(kill_rsp) = reply.body.expect("KillRsp") else {
        panic!("SignalRsp must be bridged back to KillRsp");
    };
    assert_eq!(kill_rsp.code, ErrorCode::ErrNone as i32);
    assert_eq!(kill_rsp.message, "ok");
    assert_eq!(kill_rsp.payload, b"signal-out");
}

#[tokio::test]
async fn event_req_forwards_to_target_runtime_stream() {
    let bus = new_bus("node-a", 29013);
    let (target_tx, mut target_rx) = mpsc::channel(4);
    bus.attach_runtime_stream("event-target", target_tx);

    let msg = StreamingMessage {
        message_id: "event-mid".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::EventReq(EventRequest {
            request_id: "event-req".into(),
            message: "payload".into(),
            instance_id: "event-target".into(),
        })),
    };

    let act = InvocationHandler::handle_runtime_inbound("event-source", msg, &bus).await;
    assert!(matches!(act, InboundAction::None));

    let forwarded = tokio::time::timeout(std::time::Duration::from_secs(1), target_rx.recv())
        .await
        .expect("event should be forwarded without hanging")
        .expect("forwarded event")
        .unwrap();
    assert_eq!(forwarded.message_id, "event-mid");
    let streaming_message::Body::EventReq(event) = forwarded.body.expect("EventReq") else {
        panic!("expected EventReq forwarded to target runtime");
    };
    assert_eq!(event.request_id, "event-req");
    assert_eq!(event.message, "payload");
    assert_eq!(event.instance_id, "event-target");
}

#[tokio::test]
async fn exit_req_normal_triggers_kill_signal_1() {
    let bus = new_bus("node-a", 29012);
    let iid = "exit-ok";
    bus.instance_ctrl_ref().insert_metadata(InstanceMetadata {
        id: iid.into(),
        function_name: "f".into(),
        tenant: String::new(),
        node_id: "node-a".into(),
        runtime_id: "".into(),
        runtime_port: 0,
        state: InstanceState::Running,
        created_at_ms: InstanceMetadata::now_ms(),
        updated_at_ms: InstanceMetadata::now_ms(),
        group_id: None,
        trace_id: String::new(),
        resources: Default::default(),
        etcd_kv_version: None,
        etcd_mod_revision: None,
    });
    let msg = StreamingMessage {
        message_id: "e0".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::ExitReq(ExitRequest {
            code: 0,
            message: String::new(),
            ..Default::default()
        })),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound(iid, msg, &bus).await
    else {
        panic!("Reply");
    };
    assert!(matches!(
        outs[0].body,
        Some(streaming_message::Body::ExitRsp(_))
    ));
}

#[tokio::test]
async fn exit_req_error_applies_failed_exit() {
    let bus = new_bus("node-a", 29013);
    let iid = "exit-bad";
    bus.instance_ctrl_ref().insert_metadata(InstanceMetadata {
        id: iid.into(),
        function_name: "f".into(),
        tenant: String::new(),
        node_id: "node-a".into(),
        runtime_id: "".into(),
        runtime_port: 0,
        state: InstanceState::Running,
        created_at_ms: InstanceMetadata::now_ms(),
        updated_at_ms: InstanceMetadata::now_ms(),
        group_id: None,
        trace_id: String::new(),
        resources: Default::default(),
        etcd_kv_version: None,
        etcd_mod_revision: None,
    });
    let msg = StreamingMessage {
        message_id: "e1".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::ExitReq(ExitRequest {
            code: 1,
            message: "boom".into(),
            ..Default::default()
        })),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound(iid, msg, &bus).await
    else {
        panic!("Reply");
    };
    assert!(matches!(
        outs[0].body,
        Some(streaming_message::Body::ExitRsp(_))
    ));
    let st = bus.instance_ctrl_ref().get(iid).expect("meta").state;
    assert_eq!(st, InstanceState::Failed);
}

#[tokio::test]
async fn heartbeat_req_replies() {
    let bus = new_bus("node-a", 29014);
    let msg = StreamingMessage {
        message_id: "hb".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::HeartbeatReq(
            HeartbeatRequest::default(),
        )),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound("x", msg, &bus).await
    else {
        panic!("Reply");
    };
    assert!(matches!(
        outs[0].body,
        Some(streaming_message::Body::HeartbeatRsp(_))
    ));
}

#[tokio::test]
async fn call_rsp_and_call_result_ack_are_noops() {
    let bus = new_bus("node-a", 29015);
    for body in [
        streaming_message::Body::CallRsp(Default::default()),
        streaming_message::Body::CallResultAck(Default::default()),
    ] {
        let msg = StreamingMessage {
            message_id: "z".into(),
            meta_data: Default::default(),
            body: Some(body),
        };
        let act = InvocationHandler::handle_runtime_inbound("z", msg, &bus).await;
        assert!(matches!(act, InboundAction::None));
    }
}

#[tokio::test]
async fn notify_rsp_is_noop() {
    let bus = new_bus("node-a", 29016);
    let msg = StreamingMessage {
        message_id: "q".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::NotifyRsp(Default::default())),
    };
    let act = InvocationHandler::handle_runtime_inbound("q", msg, &bus).await;
    assert!(matches!(act, InboundAction::None));
}

#[tokio::test]
async fn checkpoint_req_is_unhandled_none() {
    let bus = new_bus("node-a", 29017);
    let msg = StreamingMessage {
        message_id: "cp".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::CheckpointReq(Default::default())),
    };
    let act = InvocationHandler::handle_runtime_inbound("cp", msg, &bus).await;
    assert!(matches!(act, InboundAction::None));
}

#[tokio::test]
async fn recover_req_is_unhandled_none() {
    let bus = new_bus("node-a", 29018);
    let msg = StreamingMessage {
        message_id: "rv".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::RecoverReq(Default::default())),
    };
    let act = InvocationHandler::handle_runtime_inbound("rv", msg, &bus).await;
    assert!(matches!(act, InboundAction::None));
}

#[tokio::test]
async fn empty_body_yields_none() {
    let bus = new_bus("node-a", 29019);
    let msg = StreamingMessage {
        message_id: "eb".into(),
        meta_data: Default::default(),
        body: None,
    };
    let act = InvocationHandler::handle_runtime_inbound("eb", msg, &bus).await;
    assert!(matches!(act, InboundAction::None));
}

#[tokio::test]
async fn preserves_nonempty_message_id_on_invoke() {
    let bus = new_bus("node-a", 29020);
    let msg = StreamingMessage {
        message_id: "stable-mid".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::InvokeReq(InvokeRequest {
            function: "f".into(),
            request_id: "r".into(),
            ..Default::default()
        })),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound("c", msg, &bus).await
    else {
        panic!("Reply");
    };
    assert_eq!(outs[0].message_id, "stable-mid");
}

#[test]
fn metadata_wire_decode_invoke_type_create_instance_zero() {
    let md = MetaData::decode(&[0x08, 0x00][..]).expect("decode");
    assert_eq!(md.invoke_type, 0);
}

#[test]
fn metadata_wire_decode_invoke_type_value_two_stateless_init_tag() {
    let md = MetaData::decode(&[0x08, 0x02][..]).expect("decode");
    assert_eq!(md.invoke_type, 2);
}

#[test]
fn metadata_round_trip_invoke_type_via_prost() {
    let mut buf = Vec::new();
    let md = MetaData {
        invoke_type: yr_proto::resources::InvokeType::CreateInstance as i32,
        function_meta: None,
        config: None,
    };
    md.encode(&mut buf).expect("encode");
    let got = MetaData::decode(&buf[..]).expect("decode");
    assert_eq!(
        got.invoke_type,
        yr_proto::resources::InvokeType::CreateInstance as i32
    );
}

#[test]
fn metadata_round_trip_preserves_numeric_invoke_type_extensions() {
    let mut buf = Vec::new();
    MetaData {
        invoke_type: 7,
        function_meta: None,
        config: None,
    }
    .encode(&mut buf)
    .unwrap();
    let md = MetaData::decode(&buf[..]).expect("decode");
    assert_eq!(md.invoke_type, 7);
}
