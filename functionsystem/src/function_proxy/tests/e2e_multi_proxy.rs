//! Scenario 8 — multi-proxy: InnerService forward, route by instance location, retry on transient failure.

mod common;

use std::net::SocketAddr;
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;

use async_trait::async_trait;
use common::{make_proxy_config, new_bus};
use tokio::net::TcpListener;
use tokio_stream::wrappers::TcpListenerStream;
use tonic::transport::Server;
use yr_proto::bus_service::{QueryInstanceRequest, QueryInstanceResponse};
use yr_proto::common::ErrorCode;
use yr_proto::inner_service::inner_service_server::{InnerService, InnerServiceServer};
use yr_proto::inner_service::{
    ForwardCallRequest, ForwardCallResponse, ForwardCallResultRequest, ForwardCallResultResponse,
    ForwardKillRequest, ForwardKillResponse, ForwardRecoverRequest, ForwardRecoverResponse,
    NotifyRequest, NotifyResponse,
};
use yr_proto::runtime_service::CallRequest;

#[derive(Clone, Default)]
struct OkPeerInner {
    pub forward_hits: Arc<AtomicU32>,
}

#[async_trait]
impl InnerService for OkPeerInner {
    async fn forward_recover(
        &self,
        _request: tonic::Request<ForwardRecoverRequest>,
    ) -> Result<tonic::Response<ForwardRecoverResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("stub"))
    }

    async fn notify_result(
        &self,
        _request: tonic::Request<NotifyRequest>,
    ) -> Result<tonic::Response<NotifyResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("stub"))
    }

    async fn forward_kill(
        &self,
        _request: tonic::Request<ForwardKillRequest>,
    ) -> Result<tonic::Response<ForwardKillResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("stub"))
    }

    async fn forward_call_result(
        &self,
        _request: tonic::Request<ForwardCallResultRequest>,
    ) -> Result<tonic::Response<ForwardCallResultResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("stub"))
    }

    async fn forward_call(
        &self,
        request: tonic::Request<ForwardCallRequest>,
    ) -> Result<tonic::Response<ForwardCallResponse>, tonic::Status> {
        self.forward_hits.fetch_add(1, Ordering::SeqCst);
        let inner = request.into_inner();
        let rid = inner
            .req
            .as_ref()
            .map(|c| c.request_id.clone())
            .unwrap_or_default();
        Ok(tonic::Response::new(ForwardCallResponse {
            code: ErrorCode::ErrNone as i32,
            message: String::new(),
            request_id: rid,
        }))
    }

    async fn query_instance(
        &self,
        _request: tonic::Request<QueryInstanceRequest>,
    ) -> Result<tonic::Response<QueryInstanceResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("stub"))
    }
}

#[tokio::test]
async fn e2e_multi_proxy_forward_call_between_two_proxy_configs() {
    let listener = TcpListener::bind("127.0.0.1:0").await.expect("bind");
    let addr: SocketAddr = listener.local_addr().expect("addr");
    let peer_url = format!("http://{}", addr);

    let hits = Arc::new(AtomicU32::new(0));
    let stub = OkPeerInner {
        forward_hits: Arc::clone(&hits),
    };
    let incoming = TcpListenerStream::new(listener);
    tokio::spawn(async move {
        Server::builder()
            .add_service(InnerServiceServer::new(stub))
            .serve_with_incoming(incoming)
            .await
            .expect("peer server");
    });
    tokio::time::sleep(std::time::Duration::from_millis(60)).await;

    let cfg_alpha = make_proxy_config("proxy-alpha", 39001);
    let cfg_beta = make_proxy_config("proxy-beta", 39002);
    assert_ne!(cfg_alpha.node_id, cfg_beta.node_id);

    let bus_beta = new_bus("proxy-beta", 39002);
    let route = serde_json::json!({
        "nodeId": "proxy-alpha",
        "proxyAddress": peer_url
    });
    bus_beta.apply_instance_route_put("inst-on-alpha", &serde_json::to_vec(&route).unwrap());
    assert!(!bus_beta.should_dispatch_locally("inst-on-alpha"));

    let res = bus_beta
        .forward_call(ForwardCallRequest {
            instance_id: "inst-on-alpha".into(),
            req: Some(CallRequest {
                request_id: "mp-req-1".into(),
                function: "f".into(),
                ..Default::default()
            }),
            ..Default::default()
        })
        .await
        .expect("forward");
    assert_eq!(res.code, ErrorCode::ErrNone as i32);
    assert_eq!(hits.load(Ordering::SeqCst), 1);
}

#[tokio::test]
async fn e2e_multi_proxy_routes_local_when_instance_owned_locally() {
    let bus = new_bus("proxy-local-owner", 39003);
    let route = serde_json::json!({
        "nodeId": "proxy-local-owner",
        "proxyAddress": "http://127.0.0.1:39003"
    });
    bus.apply_instance_route_put("local-owned", &serde_json::to_vec(&route).unwrap());
    assert!(bus.should_dispatch_locally("local-owned"));
}

#[derive(Clone, Default)]
struct FlakyThenOkInner {
    attempt: Arc<AtomicU32>,
    ok_hits: Arc<AtomicU32>,
}

#[async_trait]
impl InnerService for FlakyThenOkInner {
    async fn forward_recover(
        &self,
        _request: tonic::Request<ForwardRecoverRequest>,
    ) -> Result<tonic::Response<ForwardRecoverResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("stub"))
    }

    async fn notify_result(
        &self,
        _request: tonic::Request<NotifyRequest>,
    ) -> Result<tonic::Response<NotifyResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("stub"))
    }

    async fn forward_kill(
        &self,
        _request: tonic::Request<ForwardKillRequest>,
    ) -> Result<tonic::Response<ForwardKillResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("stub"))
    }

    async fn forward_call_result(
        &self,
        _request: tonic::Request<ForwardCallResultRequest>,
    ) -> Result<tonic::Response<ForwardCallResultResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("stub"))
    }

    async fn forward_call(
        &self,
        request: tonic::Request<ForwardCallRequest>,
    ) -> Result<tonic::Response<ForwardCallResponse>, tonic::Status> {
        let prev = self.attempt.fetch_add(1, Ordering::SeqCst);
        if prev < 2 {
            return Err(tonic::Status::unavailable("transient peer failure"));
        }
        self.ok_hits.fetch_add(1, Ordering::SeqCst);
        let inner = request.into_inner();
        let rid = inner
            .req
            .as_ref()
            .map(|c| c.request_id.clone())
            .unwrap_or_default();
        Ok(tonic::Response::new(ForwardCallResponse {
            code: ErrorCode::ErrNone as i32,
            message: String::new(),
            request_id: rid,
        }))
    }

    async fn query_instance(
        &self,
        _request: tonic::Request<QueryInstanceRequest>,
    ) -> Result<tonic::Response<QueryInstanceResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("stub"))
    }
}

#[tokio::test]
async fn e2e_multi_proxy_forward_retries_then_succeeds() {
    let listener = TcpListener::bind("127.0.0.1:0").await.expect("bind");
    let addr: SocketAddr = listener.local_addr().expect("addr");
    let peer_url = format!("http://{}", addr);

    let stub = FlakyThenOkInner {
        attempt: Arc::new(AtomicU32::new(0)),
        ok_hits: Arc::new(AtomicU32::new(0)),
    };
    let ok_hits = Arc::clone(&stub.ok_hits);
    let incoming = TcpListenerStream::new(listener);
    tokio::spawn(async move {
        Server::builder()
            .add_service(InnerServiceServer::new(stub))
            .serve_with_incoming(incoming)
            .await
            .expect("peer server");
    });
    tokio::time::sleep(std::time::Duration::from_millis(60)).await;

    let bus = new_bus("proxy-retry-client", 39004);
    let route = serde_json::json!({
        "nodeId": "peer-flaky",
        "proxyAddress": peer_url
    });
    bus.apply_instance_route_put("inst-retry", &serde_json::to_vec(&route).unwrap());

    let res = bus
        .forward_call(ForwardCallRequest {
            instance_id: "inst-retry".into(),
            req: Some(CallRequest {
                request_id: "retry-rid".into(),
                ..Default::default()
            }),
            ..Default::default()
        })
        .await
        .expect("retries should land on success");
    assert_eq!(res.code, ErrorCode::ErrNone as i32);
    assert_eq!(res.request_id, "retry-rid");
    assert_eq!(ok_hits.load(Ordering::SeqCst), 1);
}
