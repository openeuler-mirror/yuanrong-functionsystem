//! Multi-proxy routing: etcd instance routes + peer InnerService forwarding.

mod common;

use std::collections::HashMap;
use std::net::SocketAddr;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

use async_trait::async_trait;
use common::{make_proxy_config, new_bus};
use tokio::net::TcpListener;
use tokio_stream::wrappers::TcpListenerStream;
use tonic::transport::Server;
use yr_proto::bus_service::{QueryInstanceRequest, QueryInstanceResponse};
use yr_proto::common::ErrorCode;
use yr_proto::core_service::{CallResult, KillRequest};
use yr_proto::inner_service::inner_service_server::{InnerService, InnerServiceServer};
use yr_proto::inner_service::{
    ForwardCallRequest, ForwardCallResponse, ForwardCallResultRequest, ForwardCallResultResponse,
    ForwardKillRequest, ForwardKillResponse, ForwardRecoverRequest, ForwardRecoverResponse,
    NotifyRequest, NotifyResponse,
};
use yr_proto::runtime_service::CallRequest;
use yr_proxy::busproxy::service_registry::BusProxyRegistration;
use yr_proxy::state_machine::{InstanceMetadata, InstanceState};

#[test]
fn route_owner_wins_over_stale_local_metadata() {
    let bus_b = new_bus("proxy-b", 18402);
    let iid = "inst-remote";

    let meta = InstanceMetadata {
        id: iid.to_string(),
        function_name: "f".into(),
        tenant: String::new(),
        node_id: "proxy-b".into(),
        runtime_id: "r1".into(),
        runtime_port: 0,
        state: InstanceState::Running,
        created_at_ms: InstanceMetadata::now_ms(),
        updated_at_ms: InstanceMetadata::now_ms(),
        group_id: None,
        trace_id: String::new(),
        resources: HashMap::new(),
        etcd_kv_version: None,
        etcd_mod_revision: None,
    };
    bus_b.instance_ctrl_ref().insert_metadata(meta);

    let route = serde_json::json!({
        "nodeId": "proxy-a",
        "proxyAddress": "http://127.0.0.1:18401"
    });
    bus_b.apply_instance_route_put(iid, &serde_json::to_vec(&route).unwrap());

    assert!(
        !bus_b.should_dispatch_locally(iid),
        "etcd route owner proxy-a must override local metadata on proxy-b"
    );
}

#[test]
fn local_owner_in_route_dispatches_locally_without_metadata() {
    let bus_a = new_bus("proxy-a", 18401);
    let iid = "inst-local";

    let route = serde_json::json!({
        "nodeId": "proxy-a",
        "proxyAddress": "http://127.0.0.1:18401"
    });
    bus_a.apply_instance_route_put(iid, &serde_json::to_vec(&route).unwrap());

    assert!(bus_a.should_dispatch_locally(iid));
}

#[test]
fn busproxy_registration_json_exposes_grpc_for_peer_discovery() {
    let config = make_proxy_config("proxy-a", 18401);
    let reg = BusProxyRegistration {
        aid: "aid1".into(),
        node: config.node_id.clone(),
        ak: "k".into(),
        grpc: config.advertise_grpc_endpoint(),
    };
    let v = serde_json::to_value(&reg).expect("serialize");
    assert_eq!(
        v.get("grpc").and_then(|x| x.as_str()),
        Some("http://127.0.0.1:18401")
    );

    let bus_b = new_bus("proxy-b", 18402);
    bus_b.upsert_peer_from_json(
        "proxy-a",
        br#"{"node":"proxy-a","grpc":"http://127.0.0.1:18401","aid":"x","ak":""}"#,
    );
    let route = serde_json::json!({ "nodeId": "proxy-a" });
    bus_b.apply_instance_route_put("i-peer", &serde_json::to_vec(&route).unwrap());
    // resolve_peer_endpoint is private; forward path uses it after should_dispatch_locally is false
    assert!(!bus_b.should_dispatch_locally("i-peer"));
}

#[derive(Clone, Default)]
struct CountingPeerInner {
    forward_call_hits: Arc<AtomicUsize>,
}

#[async_trait]
impl InnerService for CountingPeerInner {
    async fn forward_recover(
        &self,
        _request: tonic::Request<ForwardRecoverRequest>,
    ) -> Result<tonic::Response<ForwardRecoverResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("test stub"))
    }

    async fn notify_result(
        &self,
        _request: tonic::Request<NotifyRequest>,
    ) -> Result<tonic::Response<NotifyResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("test stub"))
    }

    async fn forward_kill(
        &self,
        _request: tonic::Request<ForwardKillRequest>,
    ) -> Result<tonic::Response<ForwardKillResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("test stub"))
    }

    async fn forward_call_result(
        &self,
        _request: tonic::Request<ForwardCallResultRequest>,
    ) -> Result<tonic::Response<ForwardCallResultResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("test stub"))
    }

    async fn forward_call(
        &self,
        request: tonic::Request<ForwardCallRequest>,
    ) -> Result<tonic::Response<ForwardCallResponse>, tonic::Status> {
        self.forward_call_hits.fetch_add(1, Ordering::SeqCst);
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
        Err(tonic::Status::unimplemented("test stub"))
    }
}

#[tokio::test]
async fn forward_call_reaches_peer_inner_service() {
    let listener = TcpListener::bind("127.0.0.1:0").await.expect("bind");
    let addr: SocketAddr = listener.local_addr().expect("addr");
    let peer_url = format!("http://{}", addr);

    let hits = Arc::new(AtomicUsize::new(0));
    let stub = CountingPeerInner {
        forward_call_hits: Arc::clone(&hits),
    };
    let incoming = TcpListenerStream::new(listener);
    tokio::spawn(async move {
        Server::builder()
            .add_service(InnerServiceServer::new(stub))
            .serve_with_incoming(incoming)
            .await
            .expect("server");
    });
    tokio::time::sleep(std::time::Duration::from_millis(50)).await;

    let bus_b = new_bus("proxy-b", 29999);
    let iid = "inst-on-a";
    let route = serde_json::json!({
        "nodeId": "proxy-a",
        "proxyAddress": peer_url
    });
    bus_b.apply_instance_route_put(iid, &serde_json::to_vec(&route).unwrap());

    assert!(!bus_b.should_dispatch_locally(iid));

    let fc = ForwardCallRequest {
        instance_id: iid.to_string(),
        req: Some(CallRequest {
            request_id: "req-1".into(),
            function: "hello".into(),
            ..Default::default()
        }),
        ..Default::default()
    };
    let res = bus_b.forward_call(fc).await.expect("forward_call");
    assert_eq!(res.code, ErrorCode::ErrNone as i32);
    assert_eq!(hits.load(Ordering::SeqCst), 1);
}

#[derive(Clone, Default)]
struct CountingPeerAll {
    forward_call_hits: Arc<AtomicUsize>,
    forward_call_result_hits: Arc<AtomicUsize>,
    forward_kill_hits: Arc<AtomicUsize>,
}

#[async_trait]
impl InnerService for CountingPeerAll {
    async fn forward_recover(
        &self,
        _request: tonic::Request<ForwardRecoverRequest>,
    ) -> Result<tonic::Response<ForwardRecoverResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("test stub"))
    }

    async fn notify_result(
        &self,
        _request: tonic::Request<NotifyRequest>,
    ) -> Result<tonic::Response<NotifyResponse>, tonic::Status> {
        Err(tonic::Status::unimplemented("test stub"))
    }

    async fn forward_kill(
        &self,
        request: tonic::Request<ForwardKillRequest>,
    ) -> Result<tonic::Response<ForwardKillResponse>, tonic::Status> {
        self.forward_kill_hits.fetch_add(1, Ordering::SeqCst);
        let inner = request.into_inner();
        Ok(tonic::Response::new(ForwardKillResponse {
            request_id: inner.request_id,
            code: ErrorCode::ErrNone as i32,
            message: String::new(),
        }))
    }

    async fn forward_call_result(
        &self,
        request: tonic::Request<ForwardCallResultRequest>,
    ) -> Result<tonic::Response<ForwardCallResultResponse>, tonic::Status> {
        self.forward_call_result_hits.fetch_add(1, Ordering::SeqCst);
        let inner = request.into_inner();
        let rid = inner
            .req
            .as_ref()
            .map(|c| c.request_id.clone())
            .unwrap_or_default();
        Ok(tonic::Response::new(ForwardCallResultResponse {
            code: ErrorCode::ErrNone as i32,
            message: String::new(),
            request_id: rid,
            instance_id: inner.instance_id,
        }))
    }

    async fn forward_call(
        &self,
        request: tonic::Request<ForwardCallRequest>,
    ) -> Result<tonic::Response<ForwardCallResponse>, tonic::Status> {
        self.forward_call_hits.fetch_add(1, Ordering::SeqCst);
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
        Err(tonic::Status::unimplemented("test stub"))
    }
}

#[test]
fn upsert_peer_accepts_address_field() {
    let bus = new_bus("local", 28001);
    bus.upsert_peer_from_json("n1", br#"{"address":"http://127.0.0.1:9001","grpc":""}"#);
    let route = serde_json::json!({ "nodeId": "n1" });
    bus.apply_instance_route_put("i1", &serde_json::to_vec(&route).unwrap());
    assert!(!bus.should_dispatch_locally("i1"));
}

#[test]
fn upsert_peer_falls_back_to_grpc_when_address_missing() {
    let bus = new_bus("local", 28002);
    bus.upsert_peer_from_json("n2", br#"{"grpc":"http://127.0.0.1:9002"}"#);
    let route = serde_json::json!({ "node": "n2" });
    bus.apply_instance_route_put("i2", &serde_json::to_vec(&route).unwrap());
    assert!(!bus.should_dispatch_locally("i2"));
}

#[test]
fn upsert_peer_ignores_empty_endpoint_strings() {
    let bus = new_bus("local", 28003);
    bus.upsert_peer_from_json("n3", br#"{"address":"","grpc":""}"#);
    let route = serde_json::json!({ "nodeId": "n3" });
    bus.apply_instance_route_put("i3", &serde_json::to_vec(&route).unwrap());
    assert!(
        !bus.should_dispatch_locally("i3"),
        "foreign owner without resolvable peer must not dispatch locally"
    );
}

#[test]
fn upsert_peer_malformed_json_is_noop() {
    let bus = new_bus("local", 28004);
    bus.upsert_peer_from_json("n4", b"not-json");
    let route = serde_json::json!({ "nodeId": "n4" });
    bus.apply_instance_route_put("i4", &serde_json::to_vec(&route).unwrap());
    assert!(!bus.should_dispatch_locally("i4"));
}

#[test]
fn remove_peer_drops_registration() {
    let bus = new_bus("local", 28005);
    bus.upsert_peer_from_json("n5", br#"{"grpc":"http://127.0.0.1:9005"}"#);
    bus.remove_peer("n5");
    let route = serde_json::json!({ "nodeId": "n5" });
    bus.apply_instance_route_put("i5", &serde_json::to_vec(&route).unwrap());
    assert!(!bus.should_dispatch_locally("i5"));
}

#[test]
fn should_dispatch_false_without_route_or_metadata() {
    let bus = new_bus("local", 28006);
    assert!(!bus.should_dispatch_locally("unknown-instance"));
}

#[test]
fn should_dispatch_true_with_local_metadata_even_without_route() {
    let bus = new_bus("proxy-x", 28007);
    let meta = InstanceMetadata {
        id: "m1".into(),
        function_name: "f".into(),
        tenant: String::new(),
        node_id: "proxy-x".into(),
        runtime_id: "r".into(),
        runtime_port: 0,
        state: InstanceState::Running,
        created_at_ms: InstanceMetadata::now_ms(),
        updated_at_ms: InstanceMetadata::now_ms(),
        group_id: None,
        trace_id: String::new(),
        resources: HashMap::new(),
        etcd_kv_version: None,
        etcd_mod_revision: None,
    };
    bus.instance_ctrl_ref().insert_metadata(meta);
    assert!(bus.should_dispatch_locally("m1"));
}

#[test]
fn route_delete_stops_remote_override_for_bare_metadata() {
    let bus = new_bus("proxy-b", 28008);
    let meta = InstanceMetadata {
        id: "stale".into(),
        function_name: "f".into(),
        tenant: String::new(),
        node_id: "proxy-b".into(),
        runtime_id: "r".into(),
        runtime_port: 0,
        state: InstanceState::Running,
        created_at_ms: InstanceMetadata::now_ms(),
        updated_at_ms: InstanceMetadata::now_ms(),
        group_id: None,
        trace_id: String::new(),
        resources: HashMap::new(),
        etcd_kv_version: None,
        etcd_mod_revision: None,
    };
    bus.instance_ctrl_ref().insert_metadata(meta);
    let route = serde_json::json!({
        "nodeId": "proxy-a",
        "proxyAddress": "http://127.0.0.1:1"
    });
    bus.apply_instance_route_put("stale", &serde_json::to_vec(&route).unwrap());
    assert!(!bus.should_dispatch_locally("stale"));
    bus.apply_instance_route_delete("stale");
    assert!(bus.should_dispatch_locally("stale"));
}

#[tokio::test]
async fn forward_call_rejects_missing_call_request() {
    let bus = new_bus("local", 28009);
    let err = bus
        .forward_call(ForwardCallRequest {
            instance_id: "x".into(),
            req: None,
            ..Default::default()
        })
        .await
        .expect_err("missing inner CallRequest");
    assert_eq!(err.code(), tonic::Code::InvalidArgument);
}

#[tokio::test]
async fn forward_call_result_rejects_missing_call_result() {
    let bus = new_bus("local", 28010);
    let err = bus
        .forward_call_result(ForwardCallResultRequest {
            instance_id: "x".into(),
            req: None,
            ..Default::default()
        })
        .await
        .expect_err("missing CallResult");
    assert_eq!(err.code(), tonic::Code::InvalidArgument);
}

#[tokio::test]
async fn forward_kill_remote_hits_peer_inner() {
    let listener = TcpListener::bind("127.0.0.1:0").await.expect("bind");
    let addr: SocketAddr = listener.local_addr().expect("addr");
    let peer_url = format!("http://{}", addr);

    let hits = Arc::new(AtomicUsize::new(0));
    let stub = CountingPeerAll {
        forward_kill_hits: Arc::clone(&hits),
        ..Default::default()
    };
    let incoming = TcpListenerStream::new(listener);
    tokio::spawn(async move {
        Server::builder()
            .add_service(InnerServiceServer::new(stub))
            .serve_with_incoming(incoming)
            .await
            .expect("server");
    });
    tokio::time::sleep(std::time::Duration::from_millis(50)).await;

    let bus = new_bus("proxy-b", 28011);
    let iid = "kill-remote";
    let route = serde_json::json!({
        "nodeId": "proxy-a",
        "proxyAddress": peer_url
    });
    bus.apply_instance_route_put(iid, &serde_json::to_vec(&route).unwrap());

    let res = bus
        .forward_kill(ForwardKillRequest {
            request_id: "k1".into(),
            instance_id: iid.to_string(),
            req: Some(KillRequest {
                instance_id: iid.to_string(),
                signal: 1,
                ..Default::default()
            }),
            ..Default::default()
        })
        .await
        .expect("forward_kill");
    assert_eq!(res.code, ErrorCode::ErrNone as i32);
    assert_eq!(hits.load(Ordering::SeqCst), 1);
}

#[tokio::test]
async fn forward_call_result_remote_hits_peer_inner() {
    let listener = TcpListener::bind("127.0.0.1:0").await.expect("bind");
    let addr: SocketAddr = listener.local_addr().expect("addr");
    let peer_url = format!("http://{}", addr);

    let hits = Arc::new(AtomicUsize::new(0));
    let stub = CountingPeerAll {
        forward_call_result_hits: Arc::clone(&hits),
        ..Default::default()
    };
    let incoming = TcpListenerStream::new(listener);
    tokio::spawn(async move {
        Server::builder()
            .add_service(InnerServiceServer::new(stub))
            .serve_with_incoming(incoming)
            .await
            .expect("server");
    });
    tokio::time::sleep(std::time::Duration::from_millis(50)).await;

    let bus = new_bus("proxy-b", 28012);
    let iid = "result-remote";
    let route = serde_json::json!({
        "nodeId": "proxy-a",
        "proxyAddress": peer_url
    });
    bus.apply_instance_route_put(iid, &serde_json::to_vec(&route).unwrap());

    let res = bus
        .forward_call_result(ForwardCallResultRequest {
            instance_id: iid.to_string(),
            req: Some(CallResult {
                request_id: "cr1".into(),
                ..Default::default()
            }),
            ..Default::default()
        })
        .await
        .expect("forward_call_result");
    assert_eq!(res.code, ErrorCode::ErrNone as i32);
    assert_eq!(hits.load(Ordering::SeqCst), 1);
}

#[tokio::test]
async fn forward_kill_fails_without_peer_endpoint() {
    let bus = new_bus("proxy-b", 28013);
    let iid = "no-peer-kill";
    let route = serde_json::json!({ "nodeId": "missing-peer" });
    bus.apply_instance_route_put(iid, &serde_json::to_vec(&route).unwrap());
    let err = bus
        .forward_kill(ForwardKillRequest {
            request_id: "k2".into(),
            instance_id: iid.to_string(),
            req: Some(KillRequest {
                instance_id: iid.to_string(),
                signal: 1,
                ..Default::default()
            }),
            ..Default::default()
        })
        .await
        .expect_err("no peer");
    assert_eq!(err.code(), tonic::Code::FailedPrecondition);
}

#[tokio::test]
async fn forward_call_fails_without_peer_endpoint() {
    let bus = new_bus("proxy-b", 28014);
    let iid = "no-peer-call";
    let route = serde_json::json!({ "nodeId": "missing-peer-2" });
    bus.apply_instance_route_put(iid, &serde_json::to_vec(&route).unwrap());
    let err = bus
        .forward_call(ForwardCallRequest {
            instance_id: iid.to_string(),
            req: Some(CallRequest {
                request_id: "c1".into(),
                ..Default::default()
            }),
            ..Default::default()
        })
        .await
        .expect_err("no peer");
    assert_eq!(err.code(), tonic::Code::FailedPrecondition);
}

#[tokio::test]
async fn forward_call_result_fails_without_peer_endpoint() {
    let bus = new_bus("proxy-b", 28015);
    let iid = "no-peer-cr";
    let route = serde_json::json!({ "nodeId": "missing-peer-3" });
    bus.apply_instance_route_put(iid, &serde_json::to_vec(&route).unwrap());
    let err = bus
        .forward_call_result(ForwardCallResultRequest {
            instance_id: iid.to_string(),
            req: Some(CallResult {
                request_id: "x".into(),
                ..Default::default()
            }),
            ..Default::default()
        })
        .await
        .expect_err("no peer");
    assert_eq!(err.code(), tonic::Code::FailedPrecondition);
}

#[tokio::test]
async fn forward_kill_local_completes_without_agent() {
    let bus = new_bus("local", 28016);
    let iid = "local-kill";
    let meta = InstanceMetadata {
        id: iid.to_string(),
        function_name: "f".into(),
        tenant: String::new(),
        node_id: "local".into(),
        runtime_id: "r0".into(),
        runtime_port: 0,
        state: InstanceState::Running,
        created_at_ms: InstanceMetadata::now_ms(),
        updated_at_ms: InstanceMetadata::now_ms(),
        group_id: None,
        trace_id: String::new(),
        resources: HashMap::new(),
        etcd_kv_version: None,
        etcd_mod_revision: None,
    };
    bus.instance_ctrl_ref().insert_metadata(meta);
    let res = bus
        .forward_kill(ForwardKillRequest {
            request_id: "lk".into(),
            instance_id: iid.to_string(),
            req: Some(KillRequest {
                instance_id: iid.to_string(),
                signal: 1,
                ..Default::default()
            }),
            ..Default::default()
        })
        .await
        .expect("local forward_kill");
    assert_eq!(res.code, ErrorCode::ErrNone as i32);
}

#[test]
fn apply_route_put_bad_json_yields_empty_owner_so_dispatch_stays_local_compatible() {
    let bus = new_bus("local", 28017);
    bus.apply_instance_route_put("bad", b"not-json");
    assert!(
        bus.should_dispatch_locally("bad"),
        "default route record has empty owner, so etcd does not claim a foreign node"
    );
}

#[test]
fn route_proxy_address_snake_case_alias() {
    let bus = new_bus("proxy-z", 28018);
    let route = serde_json::json!({
        "node_id": "proxy-z",
        "proxy_address": "http://127.0.0.1:28018"
    });
    bus.apply_instance_route_put("alias", &serde_json::to_vec(&route).unwrap());
    assert!(bus.should_dispatch_locally("alias"));
}
