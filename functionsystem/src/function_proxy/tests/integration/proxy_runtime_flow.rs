//! Proxy bus ↔ runtime streaming contract (`StreamingMessage` / `CallRequest`).

use yr_proto::common::ErrorCode;
use yr_proto::core_service::{CallResult, CreateRequest, ExitRequest, InvokeRequest, KillRequest};
use yr_proto::runtime_rpc::{streaming_message, StreamingMessage};
use yr_proxy::busproxy::invocation_handler::{InboundAction, InvocationHandler};
use yr_proxy::state_machine::{InstanceMetadata, InstanceState};

use super::new_bus;

#[tokio::test]
async fn create_req_triggers_is_create_call_request_shape_on_runtime_connect() {
    let bus = new_bus("node-rt", 30201);
    let (tx, mut rx) = tokio::sync::mpsc::channel::<Result<StreamingMessage, tonic::Status>>(8);
    bus.attach_runtime_stream("driver-d2", tx.clone());

    let create = CreateRequest {
        function: "stateless-fn".into(),
        request_id: "create-req-1".into(),
        trace_id: "tr-1".into(),
        ..Default::default()
    };
    bus.register_pending_instance("inst-rc", "driver-d2", &create);
    bus.attach_runtime_stream("inst-rc", tx);

    bus.on_runtime_connected("inst-rc", "runtime-z").await;

    let msg = rx.recv().await.expect("init message").expect("ok");
    let Some(streaming_message::Body::CallReq(call)) = msg.body else {
        panic!("expected CallReq");
    };
    assert!(call.is_create, "runtime handshake must use is_create=true");
    assert_eq!(call.function, "stateless-fn");
    assert_eq!(call.request_id, "create-req-1");
    assert_eq!(call.sender_id, "driver-d2");
    assert!(
        !call.args.is_empty(),
        "stateless path injects default MetaData args"
    );
}

#[tokio::test]
async fn invoke_req_builds_call_request_on_same_stream() {
    let bus = new_bus("node-rt", 30202);
    let inv = InvokeRequest {
        function: "add".into(),
        args: vec![yr_proto::common::Arg {
            value: vec![1, 2, 3],
            ..Default::default()
        }],
        trace_id: "t-inv".into(),
        request_id: "r-inv".into(),
        return_object_i_ds: vec!["o1".into()],
        span_id: "sp-1".into(),
        ..Default::default()
    };
    let msg = StreamingMessage {
        message_id: "mid-inv".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::InvokeReq(inv.clone())),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound("driver-only", msg, &bus).await
    else {
        panic!("Reply");
    };
    let Some(streaming_message::Body::CallReq(call)) = outs[0].body.as_ref() else {
        panic!("CallReq");
    };
    assert_eq!(call.function, "add");
    assert_eq!(call.trace_id, "t-inv");
    assert_eq!(call.request_id, "r-inv");
    assert_eq!(call.return_object_i_ds, vec!["o1"]);
    assert_eq!(call.span_id, "sp-1");
    assert_eq!(call.args.len(), 1);
}

#[tokio::test]
async fn call_result_routes_to_driver_stream() {
    let bus = new_bus("node-rt", 30203);
    let (tx_drv, mut rx_drv) = tokio::sync::mpsc::channel::<Result<StreamingMessage, tonic::Status>>(8);
    let (tx_rt, mut rx_rt) = tokio::sync::mpsc::channel::<Result<StreamingMessage, tonic::Status>>(8);
    bus.attach_runtime_stream("driver-d3", tx_drv);
    bus.register_pending_instance(
        "inst-cr",
        "driver-d3",
        &CreateRequest {
            function: "f".into(),
            request_id: "rid-cr".into(),
            ..Default::default()
        },
    );
    bus.attach_runtime_stream("inst-cr", tx_rt);

    let res = CallResult {
        request_id: "invoke-1".into(),
        code: 0,
        message: "done".into(),
        ..Default::default()
    };
    bus.on_runtime_call_result("inst-cr", res.clone()).await;

    let ack = rx_rt.recv().await.expect("runtime ack").expect("ok");
    assert!(matches!(
        ack.body,
        Some(streaming_message::Body::CallResultAck(_))
    ));

    let forwarded = rx_drv.recv().await.expect("driver msg").expect("ok");
    let Some(streaming_message::Body::CallResultReq(got)) = forwarded.body else {
        panic!("CallResultReq");
    };
    assert_eq!(got.request_id, "invoke-1");
    assert_eq!(got.message, "done");
}

#[tokio::test]
async fn kill_req_local_moves_instance_to_exiting() {
    let bus = new_bus("node-rt", 30204);
    let iid = "kill-me";
    bus.instance_ctrl_ref().insert_metadata(InstanceMetadata {
        id: iid.into(),
        function_name: "f".into(),
        tenant: String::new(),
        node_id: "node-rt".into(),
        runtime_id: String::new(),
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
        message_id: "k".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::KillReq(KillRequest {
            instance_id: String::new(),
            signal: 1,
            ..Default::default()
        })),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound(iid, msg, &bus).await
    else {
        panic!("Reply");
    };
    let Some(streaming_message::Body::KillRsp(rsp)) = outs[0].body.as_ref() else {
        panic!("KillRsp");
    };
    assert_eq!(rsp.code, ErrorCode::ErrNone as i32);
    let st = bus.instance_ctrl_ref().get(iid).unwrap().state;
    assert_eq!(st, InstanceState::Exiting);
}

#[tokio::test]
async fn exit_req_zero_triggers_exiting_via_kill_shim() {
    let bus = new_bus("node-rt", 30205);
    let iid = "exit-me";
    bus.instance_ctrl_ref().insert_metadata(InstanceMetadata {
        id: iid.into(),
        function_name: "f".into(),
        tenant: String::new(),
        node_id: "node-rt".into(),
        runtime_id: String::new(),
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
        message_id: "e".into(),
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
    assert!(matches!(outs[0].body, Some(streaming_message::Body::ExitRsp(_))));
    assert_eq!(
        bus.instance_ctrl_ref().get(iid).unwrap().state,
        InstanceState::Exiting
    );
}
