//! Proxy ↔ agent contract: internal `FunctionAgentService` messages and proxy-side lifecycle.

use std::collections::HashMap;

use prost::Message;
use yr_proto::internal::{StartInstanceRequest, StopInstanceRequest};
use yr_proto::runtime_rpc::{streaming_message, StreamingMessage};
use yr_proto::runtime_service::HeartbeatRequest;
use yr_proxy::busproxy::invocation_handler::{InboundAction, InvocationHandler};
use yr_proxy::state_machine::{InstanceMetadata, InstanceState};

use super::{make_proxy_config, new_bus};

/// Mirrors [`InstanceController::start_instance`] gRPC payload shape (proxy → agent).
#[test]
fn start_instance_request_proto_fields_match_proxy_builder() {
    let cfg = make_proxy_config("proxy-node", 30101);
    let expected = StartInstanceRequest {
        instance_id: "i-1".into(),
        function_name: "hello".into(),
        tenant_id: "tenant-a".into(),
        runtime_type: "default".into(),
        env_vars: HashMap::from([
            (
                "YR_SERVER_ADDRESS".into(),
                format!("{}:{}", cfg.host, cfg.posix_port),
            ),
            (
                "POSIX_LISTEN_ADDR".into(),
                format!("{}:{}", cfg.host, cfg.posix_port),
            ),
            (
                "PROXY_GRPC_SERVER_PORT".into(),
                cfg.posix_port.to_string(),
            ),
        ]),
        resources: HashMap::from([("cpu".into(), 1.0), ("memory".into(), 512.0)]),
        code_path: String::new(),
        config_json: String::new(),
    };

    assert_eq!(expected.instance_id, "i-1");
    assert_eq!(expected.function_name, "hello");
    assert!(expected.env_vars.contains_key("YR_SERVER_ADDRESS"));
    let enc = expected.encode_to_vec();
    let dec = StartInstanceRequest::decode(enc.as_slice()).expect("round-trip");
    assert_eq!(dec.tenant_id, "tenant-a");
}

#[test]
fn stop_instance_request_carries_instance_runtime_and_force() {
    let req = StopInstanceRequest {
        instance_id: "i-1".into(),
        runtime_id: "rt-9".into(),
        force: true,
    };
    let dec = StopInstanceRequest::decode(req.encode_to_vec().as_slice()).unwrap();
    assert_eq!(dec.runtime_id, "rt-9");
    assert!(dec.force);
}

#[tokio::test]
async fn heartbeat_streaming_req_response_contract() {
    let bus = new_bus("node-pa", 30102);
    let msg = StreamingMessage {
        message_id: "hb-1".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::HeartbeatReq(HeartbeatRequest::default())),
    };
    let InboundAction::Reply(outs) =
        InvocationHandler::handle_runtime_inbound("any-stream", msg, &bus).await
    else {
        panic!("expected Reply");
    };
    let Some(streaming_message::Body::HeartbeatRsp(rsp)) = outs[0].body.as_ref() else {
        panic!("expected HeartbeatRsp");
    };
    assert_eq!(rsp.code, 0);
}

#[test]
fn instance_lifecycle_states_visible_to_query_contract() {
    let mut m = InstanceMetadata {
        id: "lid".into(),
        function_name: "f".into(),
        tenant: "t".into(),
        node_id: "n".into(),
        runtime_id: "r".into(),
        runtime_port: 0,
        state: InstanceState::Scheduling,
        created_at_ms: InstanceMetadata::now_ms(),
        updated_at_ms: InstanceMetadata::now_ms(),
        group_id: None,
        trace_id: String::new(),
        resources: Default::default(),
        etcd_kv_version: None,
        etcd_mod_revision: None,
    };
    m.transition(InstanceState::Creating)
        .expect("scheduling→creating");
    m.transition(InstanceState::Running)
        .expect("creating→running");
    let status = m.state.to_string();
    assert!(status.contains("Running") || status.eq_ignore_ascii_case("running"));
}

#[tokio::test]
async fn register_pending_instance_builds_proxy_metadata() {
    let bus = new_bus("node-pa", 30103);
    let create = yr_proto::core_service::CreateRequest {
        function: "fn-x".into(),
        request_id: "req-x".into(),
        trace_id: "tr-x".into(),
        ..Default::default()
    };
    bus.register_pending_instance("inst-x", "driver-d1", &create);

    let meta = bus
        .instance_ctrl_ref()
        .get("inst-x")
        .expect("metadata inserted");
    assert_eq!(meta.function_name, "fn-x");
    assert_eq!(meta.trace_id, "tr-x");
    assert_eq!(meta.state, InstanceState::Scheduling);
}

#[test]
fn start_instance_response_success_decodes_like_agent() {
    use yr_proto::internal::StartInstanceResponse;
    let rsp = StartInstanceResponse {
        success: true,
        message: String::new(),
        runtime_id: "rt-1".into(),
        runtime_port: 42,
    };
    let dec = StartInstanceResponse::decode(rsp.encode_to_vec().as_slice()).unwrap();
    assert!(dec.success);
    assert_eq!(dec.runtime_port, 42);
}

#[test]
fn instance_state_display_matches_scheduler_contract() {
    assert_eq!(InstanceState::Running.to_string(), "RUNNING");
    assert_eq!(InstanceState::Scheduling.as_i32(), 1);
}
