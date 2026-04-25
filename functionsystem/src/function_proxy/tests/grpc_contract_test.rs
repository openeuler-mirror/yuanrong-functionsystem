//! Compile-time and structural checks for gRPC request/response shapes used by yr-proxy.

use async_trait::async_trait;
use yr_proto::bus_service::QueryInstanceRequest;
use yr_proto::bus_service::{DiscoverDriverRequest, DiscoverDriverResponse};
use yr_proto::exec_service::exec_message::Payload as ExecPayload;
use yr_proto::exec_service::exec_service_server::ExecService;
use yr_proto::exec_service::{ExecMessage, ExecOutputData, ExecStartRequest, ExecStatusResponse};
use yr_proto::inner_service::inner_service_server::InnerService;
use yr_proto::inner_service::{
    ForwardCallRequest, ForwardCallResponse, ForwardCallResultRequest, ForwardCallResultResponse,
    ForwardKillRequest, ForwardKillResponse, ForwardRecoverRequest, ForwardRecoverResponse,
    NotifyRequest, NotifyResponse,
};
use yr_proto::runtime_rpc::{streaming_message, StreamingMessage};
use yr_proto::runtime_service::CallRequest;

#[test]
fn discover_driver_request_fields_round_trip() {
    let r = DiscoverDriverRequest {
        driver_ip: "10.0.0.1".into(),
        driver_port: "1234".into(),
        job_id: "job".into(),
        instance_id: "iid".into(),
        function_name: "fn".into(),
    };
    assert_eq!(r.driver_port, "1234");
}

#[test]
fn discover_driver_response_includes_posix_port_node_host() {
    let r = DiscoverDriverResponse {
        server_version: "0.0.1".into(),
        posix_port: "8403".into(),
        node_id: "n".into(),
        host_ip: "0.0.0.0".into(),
    };
    assert!(!r.posix_port.is_empty());
    assert!(!r.node_id.is_empty());
}

#[test]
fn query_instance_request_shape() {
    let q = QueryInstanceRequest {
        instance_id: "probe".into(),
    };
    assert_eq!(q.instance_id, "probe");
}

#[test]
fn streaming_message_call_req_variant() {
    let _ = StreamingMessage {
        message_id: "c".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::CallReq(CallRequest {
            request_id: "r".into(),
            function: "f".into(),
            ..Default::default()
        })),
    };
}

#[test]
fn streaming_message_carries_all_core_driver_variants() {
    let _ = StreamingMessage {
        message_id: "m".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::CreateReq(Default::default())),
    };
    let _ = StreamingMessage {
        message_id: "m".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::InvokeReq(Default::default())),
    };
    let _ = StreamingMessage {
        message_id: "m".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::CallResultReq(Default::default())),
    };
    let _ = StreamingMessage {
        message_id: "m".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::KillReq(Default::default())),
    };
    let _ = StreamingMessage {
        message_id: "m".into(),
        meta_data: Default::default(),
        body: Some(streaming_message::Body::ExitReq(Default::default())),
    };
}

#[test]
fn forward_call_request_wraps_runtime_call() {
    let fc = ForwardCallRequest {
        req: Some(CallRequest {
            request_id: "r".into(),
            ..Default::default()
        }),
        instance_id: "i".into(),
        src_ip: "127.0.0.1".into(),
        src_node: "n".into(),
    };
    assert!(fc.req.is_some());
}

#[test]
fn forward_call_result_request_has_instance_and_result_slots() {
    let f = ForwardCallResultRequest {
        req: Some(Default::default()),
        instance_id: "i".into(),
        runtime_id: "rt".into(),
        function_proxy_id: "fp".into(),
        ready_instance: None,
    };
    assert_eq!(f.runtime_id, "rt");
}

#[test]
fn forward_kill_request_wraps_core_kill() {
    let k = ForwardKillRequest {
        request_id: "k".into(),
        instance_id: "i".into(),
        req: Some(Default::default()),
        ..Default::default()
    };
    assert!(k.req.is_some());
}

#[test]
fn forward_recover_carries_runtime_location_strings() {
    let r = ForwardRecoverRequest {
        instance_id: "i".into(),
        runtime_id: "r".into(),
        runtime_ip: "1.1.1.1".into(),
        runtime_port: "9000".into(),
        function: "f".into(),
        ..Default::default()
    };
    assert_eq!(r.runtime_port, "9000");
}

#[test]
fn inner_response_types_carry_codes() {
    let _ = ForwardCallResponse {
        code: 0,
        message: String::new(),
        request_id: "a".into(),
    };
    let _ = ForwardCallResultResponse {
        code: 0,
        message: String::new(),
        request_id: "b".into(),
        instance_id: "c".into(),
    };
    let _ = ForwardKillResponse {
        request_id: "d".into(),
        code: 0,
        message: String::new(),
    };
    let _ = ForwardRecoverResponse {
        code: 0,
        message: String::new(),
    };
    let _ = NotifyResponse {};
}

#[test]
fn notify_request_has_code_and_message() {
    let n = NotifyRequest {
        request_id: "n".into(),
        code: 0,
        message: "msg".into(),
    };
    assert_eq!(n.code, 0);
}

#[test]
fn exec_start_request_shape() {
    let s = ExecStartRequest {
        container_id: "c".into(),
        command: vec!["/bin/sh".into()],
        ..Default::default()
    };
    assert_eq!(s.command.len(), 1);
}

#[test]
fn exec_message_status_and_output_variants() {
    let _ = ExecMessage {
        session_id: "s".into(),
        payload: Some(ExecPayload::Status(ExecStatusResponse {
            status: 0,
            exit_code: 0,
            error_message: String::new(),
        })),
    };
    let _ = ExecMessage {
        session_id: "s".into(),
        payload: Some(ExecPayload::OutputData(ExecOutputData {
            stream_type: 0,
            data: vec![1, 2, 3],
        })),
    };
}

#[tokio::test]
async fn inner_service_trait_is_implementable() {
    struct Stub;
    #[async_trait]
    impl InnerService for Stub {
        async fn forward_recover(
            &self,
            _r: tonic::Request<ForwardRecoverRequest>,
        ) -> Result<tonic::Response<ForwardRecoverResponse>, tonic::Status> {
            Err(tonic::Status::unimplemented("stub"))
        }
        async fn notify_result(
            &self,
            _r: tonic::Request<NotifyRequest>,
        ) -> Result<tonic::Response<NotifyResponse>, tonic::Status> {
            Err(tonic::Status::unimplemented("stub"))
        }
        async fn forward_kill(
            &self,
            _r: tonic::Request<ForwardKillRequest>,
        ) -> Result<tonic::Response<ForwardKillResponse>, tonic::Status> {
            Err(tonic::Status::unimplemented("stub"))
        }
        async fn forward_call_result(
            &self,
            _r: tonic::Request<ForwardCallResultRequest>,
        ) -> Result<tonic::Response<ForwardCallResultResponse>, tonic::Status> {
            Err(tonic::Status::unimplemented("stub"))
        }
        async fn forward_call(
            &self,
            _r: tonic::Request<ForwardCallRequest>,
        ) -> Result<tonic::Response<ForwardCallResponse>, tonic::Status> {
            Err(tonic::Status::unimplemented("stub"))
        }
        async fn query_instance(
            &self,
            _r: tonic::Request<yr_proto::bus_service::QueryInstanceRequest>,
        ) -> Result<tonic::Response<yr_proto::bus_service::QueryInstanceResponse>, tonic::Status>
        {
            Err(tonic::Status::unimplemented("stub"))
        }
    }
    let _ = Stub;
}

type ExecOut =
    std::pin::Pin<Box<dyn tokio_stream::Stream<Item = Result<ExecMessage, tonic::Status>> + Send>>;

#[tokio::test]
async fn exec_service_trait_stream_associated_type_name() {
    struct ExecStub;
    #[async_trait]
    impl ExecService for ExecStub {
        type ExecStreamStream = ExecOut;
        async fn exec_stream(
            &self,
            _r: tonic::Request<tonic::Streaming<ExecMessage>>,
        ) -> Result<tonic::Response<Self::ExecStreamStream>, tonic::Status> {
            Err(tonic::Status::unimplemented("stub"))
        }
    }
    let _ = ExecStub;
}
