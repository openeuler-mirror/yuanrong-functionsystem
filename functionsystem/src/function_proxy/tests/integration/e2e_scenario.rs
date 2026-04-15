//! End-to-end style scenarios over the in-memory bus (no TCP/gRPC servers).

use yr_proto::common::ErrorCode;
use yr_proto::core_service::{CallResult, CreateRequest, CreateRequests, InvokeRequest};
use yr_proto::runtime_rpc::{streaming_message, StreamingMessage};
use yr_proxy::busproxy::invocation_handler::{InboundAction, InvocationHandler};
use yr_proxy::state_machine::{InstanceMetadata, InstanceState};

use super::new_bus;

#[tokio::test]
async fn stateless_invoke_create_connect_init_notify_invoke_result() {
    let bus = new_bus("e2e-node", 30301);
    let (tx_drv, mut rx_drv) = tokio::sync::mpsc::channel::<Result<StreamingMessage, tonic::Status>>(32);
    let (tx_rt, mut rx_rt) = tokio::sync::mpsc::channel::<Result<StreamingMessage, tonic::Status>>(32);

    bus.attach_runtime_stream("driver-e2e", tx_drv.clone());

    let create = CreateRequest {
        function: "hello".into(),
        request_id: "creq-e2e".into(),
        trace_id: "tr-e2e".into(),
        ..Default::default()
    };
    bus.register_pending_instance("inst-e2e", "driver-e2e", &create);
    bus.attach_runtime_stream("inst-e2e", tx_rt);

    bus.on_runtime_connected("inst-e2e", "rt-e2e").await;

    let init = rx_rt.recv().await.expect("init").expect("ok");
    let Some(streaming_message::Body::CallReq(init_call)) = init.body else {
        panic!("init CallReq");
    };
    assert!(init_call.is_create);

    bus.on_runtime_call_result(
        "inst-e2e",
        CallResult {
            request_id: create.request_id.clone(),
            code: 0,
            message: String::new(),
            ..Default::default()
        },
    )
    .await;

    let ack_init = rx_rt.recv().await.expect("init CallResultAck").expect("ok");
    assert!(matches!(
        ack_init.body,
        Some(streaming_message::Body::CallResultAck(_))
    ));

    let notify = rx_drv.recv().await.expect("notify").expect("ok");
    let Some(streaming_message::Body::NotifyReq(n)) = notify.body else {
        panic!("NotifyReq");
    };
    assert_eq!(n.request_id, "creq-e2e");
    assert_eq!(n.code, ErrorCode::ErrNone as i32);

    let inv = StreamingMessage {
        message_id: "inv-e2e".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::InvokeReq(InvokeRequest {
            function: "hello".into(),
            request_id: "inv-req-1".into(),
            instance_id: "inst-e2e".into(),
            ..Default::default()
        })),
    };
    assert!(matches!(
        InvocationHandler::handle_runtime_inbound("driver-e2e", inv, &bus).await,
        InboundAction::None
    ));

    let call = rx_rt.recv().await.expect("invoke call").expect("ok");
    let Some(streaming_message::Body::CallReq(c)) = call.body else {
        panic!("CallReq from invoke");
    };
    assert!(!c.is_create);
    assert_eq!(c.request_id, "inv-req-1");

    bus.on_runtime_call_result(
        "inst-e2e",
        CallResult {
            request_id: "inv-req-1".into(),
            code: 0,
            message: "result-payload".into(),
            ..Default::default()
        },
    )
    .await;

    let _maybe_ack = rx_rt.recv().await;
    let result_to_driver = rx_drv.recv().await.expect("result").expect("ok");
    let Some(streaming_message::Body::CallResultReq(cr)) = result_to_driver.body else {
        panic!("CallResultReq to driver");
    };
    assert_eq!(cr.request_id, "inv-req-1");
    assert_eq!(cr.message, "result-payload");
}

#[tokio::test]
async fn running_instance_exit_req_zero_cleans_up_stream_attachment() {
    let bus = new_bus("e2e-node", 30302);
    let iid = "inst-exit";
    bus.instance_ctrl_ref().insert_metadata(InstanceMetadata {
        id: iid.into(),
        function_name: "f".into(),
        tenant: String::new(),
        node_id: "e2e-node".into(),
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
    let (tx, _rx) = tokio::sync::mpsc::channel::<Result<StreamingMessage, tonic::Status>>(4);
    bus.attach_runtime_stream(iid, tx);
    assert!(bus.has_runtime_stream(iid));

    let msg = StreamingMessage {
        message_id: "exit".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::ExitReq(yr_proto::core_service::ExitRequest {
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
    assert!(
        !bus.has_runtime_stream(iid),
        "ExitReq(0) kill path should detach runtime stream"
    );
}

#[tokio::test]
async fn group_create_three_designated_ids_proto_and_partial_schedule_response() {
    let bus = new_bus("e2e-node", 30303);
    let batch = CreateRequests {
        request_id: "group-req".into(),
        requests: vec![
            CreateRequest {
                function: "f".into(),
                request_id: "r0".into(),
                designated_instance_id: "g0".into(),
                ..Default::default()
            },
            CreateRequest {
                function: "f".into(),
                request_id: "r1".into(),
                designated_instance_id: "g1".into(),
                ..Default::default()
            },
            CreateRequest {
                function: "f".into(),
                request_id: "r2".into(),
                designated_instance_id: "g2".into(),
                ..Default::default()
            },
        ],
        ..Default::default()
    };
    assert_eq!(batch.requests.len(), 3);

    let msg = StreamingMessage {
        message_id: "grp".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::CreateReqs(batch)),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound("driver-g", msg, &bus).await
    else {
        panic!("Reply");
    };
    let Some(streaming_message::Body::CreateRsps(rsps)) = outs[0].body.as_ref() else {
        panic!("CreateRsps");
    };
    assert!(!rsps.group_id.is_empty(), "group_id must be set for batch");
    assert_eq!(
        rsps.code,
        ErrorCode::ErrInnerSystemError as i32,
        "without agent/RM scheduling fails after first instance"
    );
}

#[tokio::test]
async fn multi_tenant_instances_isolated_call_result_routing_per_driver() {
    let bus = new_bus("e2e-node", 30304);
    let (tx_da, mut rx_da) = tokio::sync::mpsc::channel::<Result<StreamingMessage, tonic::Status>>(16);
    let (tx_db, mut rx_db) = tokio::sync::mpsc::channel::<Result<StreamingMessage, tonic::Status>>(16);
    let (tx_ra, mut rx_ra) = tokio::sync::mpsc::channel::<Result<StreamingMessage, tonic::Status>>(16);
    let (tx_rb, _rx_rb) = tokio::sync::mpsc::channel::<Result<StreamingMessage, tonic::Status>>(16);

    bus.attach_runtime_stream("driver-a", tx_da);
    bus.attach_runtime_stream("driver-b", tx_db);

    bus.register_pending_instance(
        "inst-tenant-a",
        "driver-a",
        &CreateRequest {
            function: "fa".into(),
            request_id: "ra".into(),
            trace_id: "ta".into(),
            ..Default::default()
        },
    );
    bus.register_pending_instance(
        "inst-tenant-b",
        "driver-b",
        &CreateRequest {
            function: "fb".into(),
            request_id: "rb".into(),
            trace_id: "tb".into(),
            ..Default::default()
        },
    );

    if let Some(mut m) = bus.instance_ctrl_ref().instances().get_mut("inst-tenant-a") {
        m.tenant = "tenant-a".into();
    }
    if let Some(mut m) = bus.instance_ctrl_ref().instances().get_mut("inst-tenant-b") {
        m.tenant = "tenant-b".into();
    }

    assert_ne!(
        bus.instance_ctrl_ref().get("inst-tenant-a").unwrap().tenant,
        bus.instance_ctrl_ref().get("inst-tenant-b").unwrap().tenant
    );

    bus.attach_runtime_stream("inst-tenant-a", tx_ra);
    bus.attach_runtime_stream("inst-tenant-b", tx_rb);

    bus.on_runtime_call_result(
        "inst-tenant-a",
        CallResult {
            request_id: "only-a".into(),
            code: 0,
            message: String::new(),
            ..Default::default()
        },
    )
    .await;

    let _ack = rx_ra.recv().await.expect("ack to runtime a").expect("ok");
    let to_a = rx_da.recv().await.expect("to driver a").expect("ok");
    let Some(streaming_message::Body::CallResultReq(r)) = to_a.body else {
        panic!("CallResultReq");
    };
    assert_eq!(r.request_id, "only-a");

    assert!(
        rx_db.try_recv().is_err(),
        "driver B must not observe instance A CallResult"
    );
}
